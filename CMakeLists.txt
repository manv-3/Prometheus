cmake_minimum_required(VERSION 3.20)
project(prometheus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PROMETHEUS_BUILD_TESTS "Build GoogleTest unit tests" ON)

# ---------------------------------------------------------------------------
# Compiler warnings
# /WX (warnings-as-errors) is enabled once the Phase 0 skeleton builds clean.
# Until then /W4 without /WX so stubs compile without noise.
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    # Uncomment when clean: add_compile_options(/WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Qt 6 — required for ui and app modules
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Quick Qml Svg)
qt_standard_project_setup()

# ---------------------------------------------------------------------------
# Shared include root (all modules use the same include/ tree)
# ---------------------------------------------------------------------------
set(PROMETHEUS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Module: prometheus_core
# Coordinator, scheduler, snapshot publication, shared types.
# Must NOT contain biology rules or Win32 calls.
# ---------------------------------------------------------------------------
add_library(prometheus_core STATIC
    src/core/RuntimeCoordinator.cpp
    src/core/SimulationThread.cpp
    src/core/RuntimeScheduler.cpp
)
target_include_directories(prometheus_core PUBLIC ${PROMETHEUS_INCLUDE_DIR})
# Core links to submodules so SimulationThread can drive the full pipeline:
# sensor -> brain -> metabolism -> body -> snapshot
target_link_libraries(prometheus_core PUBLIC
    Qt6::Core
    prometheus_sensory
    prometheus_brain
    prometheus_metabolism
    prometheus_body
)

# ---------------------------------------------------------------------------
# Module: prometheus_sensory
# HostObservation collection and normalization (pure interface layer).
# Platform-specific code lives in prometheus_platform_windows.
# ---------------------------------------------------------------------------
add_library(prometheus_sensory STATIC
    src/sensory/SyntheticProvider.cpp
    src/sensory/SensorWorker.cpp
    src/sensory/RealSensorProvider.cpp
)
target_include_directories(prometheus_sensory PUBLIC ${PROMETHEUS_INCLUDE_DIR})
# Sensory uses shared types from include/ — does NOT link to core
# (dependency direction: core → sensory, not the reverse)
# Sensory links to platform_windows for RealSensorProvider (adapter for CpuMonitor/MemoryMonitor)
target_link_libraries(prometheus_sensory PUBLIC $<$<BOOL:${WIN32}>:prometheus_platform_windows>)

# ---------------------------------------------------------------------------
# Module: prometheus_platform_windows
# Win32 PDH, GlobalMemoryStatusEx, optional NVML.
# Only built on Windows.
# ---------------------------------------------------------------------------
if(WIN32)
    add_library(prometheus_platform_windows STATIC
        src/platform/windows/CpuMonitor.cpp
        src/platform/windows/MemoryMonitor.cpp
    )
    target_include_directories(prometheus_platform_windows PUBLIC ${PROMETHEUS_INCLUDE_DIR})
    # Platform uses shared types from include/ — does NOT link to sensory
    # (dependency direction: sensory → platform, not the reverse)
    target_link_libraries(prometheus_platform_windows PRIVATE pdh)
endif()

# ---------------------------------------------------------------------------
# Module: prometheus_brain
# OrganismState update, pressure estimate, Behaviour selection.
# Must NOT include Qt or Windows headers.
# ---------------------------------------------------------------------------
add_library(prometheus_brain STATIC
    src/brain/EmaFilter.cpp
    src/brain/PressureEstimator.cpp
    src/brain/HomeostasisEngine.cpp
    src/brain/BehaviourFSM.cpp          # Dev B's real implementation (PR #2)
)
target_include_directories(prometheus_brain PUBLIC ${PROMETHEUS_INCLUDE_DIR})
# No Qt, no platform dependency — stdlib + shared types only.

# ---------------------------------------------------------------------------
# Module: prometheus_metabolism
# ComputeBudget, QualityTier, hard limits, degrade/recover pacing.
# Must NOT own visuals or persistence.
# ---------------------------------------------------------------------------
add_library(prometheus_metabolism STATIC
    src/metabolism/Metabolism.cpp
)
target_include_directories(prometheus_metabolism PUBLIC ${PROMETHEUS_INCLUDE_DIR})
# No Qt, no platform dependency.

# ---------------------------------------------------------------------------
# Module: prometheus_body
# Bounded particle simulation, runs within ComputeBudget.
# Must NOT read telemetry or touch DB.
# ---------------------------------------------------------------------------
add_library(prometheus_body STATIC
    src/body/ParticleSystem.cpp
)
target_include_directories(prometheus_body PUBLIC ${PROMETHEUS_INCLUDE_DIR})
# Body uses shared types from include/ — does NOT link to core

# ---------------------------------------------------------------------------
# Module: prometheus_memory
# SQLite identity, events, checkpoints.
# Must NOT make real-time simulation decisions.
# ---------------------------------------------------------------------------
add_library(prometheus_memory STATIC
    src/memory/MemoryManager.cpp
)
target_include_directories(prometheus_memory PUBLIC ${PROMETHEUS_INCLUDE_DIR})
# Memory uses shared types from include/ — does NOT link to core
# SQLite for persistence (TASK-2.4)
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    target_link_libraries(prometheus_memory PRIVATE SQLite::SQLite3)
else()
    # Fallback: manual path for MinGW bundled SQLite
    target_include_directories(prometheus_memory PRIVATE "E:/qt6/Tools/mingw1310_64/opt/include")
    target_link_directories(prometheus_memory PRIVATE "E:/qt6/Tools/mingw1310_64/opt/lib")
    target_link_libraries(prometheus_memory PRIVATE sqlite3)
endif()
# SQLite: add sqlite3 target once D6 (SQLite integration method) is resolved.
# target_link_libraries(prometheus_memory PRIVATE sqlite3)

# ---------------------------------------------------------------------------
# Module: prometheus_ui
# QML + thin ViewModel over immutable OrganismSnapshot.
# Must NOT hold mutable access to engine internals.
# ---------------------------------------------------------------------------
add_library(prometheus_ui STATIC
    src/ui/OrganismViewModel.cpp
    include/prometheus/ui/OrganismViewModel.hpp
)
target_include_directories(prometheus_ui PUBLIC ${PROMETHEUS_INCLUDE_DIR})
set_target_properties(prometheus_ui PROPERTIES AUTOMOC ON)
target_link_libraries(prometheus_ui PUBLIC prometheus_core Qt6::Core Qt6::Quick Qt6::Qml)

# ---------------------------------------------------------------------------
# Executable: prometheus_app
# Entry point, lifecycle.
# Dependency order: app -> ui -> core -> {sensory, brain, metabolism, body, memory}
# ---------------------------------------------------------------------------
qt_add_executable(prometheus_app WIN32
    src/app/main.cpp
)
qt_add_qml_module(prometheus_app
    URI Prometheus
    VERSION 1.0
    QML_FILES
        resources/qml/Main.qml
    RESOURCES
        resources/organism.svg
)
target_include_directories(prometheus_app PRIVATE ${PROMETHEUS_INCLUDE_DIR})

# prometheus_ui must be whole-archive so the moc-generated vtable and signal
# implementations are pulled in from the static library. MSVC uses /WHOLEARCHIVE.
if(MSVC)
    target_link_libraries(prometheus_app PRIVATE
        prometheus_core
        prometheus_brain
        prometheus_metabolism
        prometheus_body
        prometheus_memory
        prometheus_sensory
        $<$<BOOL:${WIN32}>:prometheus_platform_windows>
        /WHOLEARCHIVE:prometheus_ui
        Qt6::Core
        Qt6::Quick
        Qt6::Qml
        Qt6::Svg
    )
else()
    target_link_libraries(prometheus_app PRIVATE
        prometheus_core
        prometheus_brain
        prometheus_metabolism
        prometheus_body
        prometheus_memory
        prometheus_sensory
        $<$<BOOL:${WIN32}>:prometheus_platform_windows>
        -Wl,--whole-archive
        prometheus_ui
        -Wl,--no-whole-archive
        Qt6::Core
        Qt6::Quick
        Qt6::Qml
        Qt6::Svg
    )
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PROMETHEUS_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    # Prevent overriding the parent project's runtime library settings on MSVC
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()
