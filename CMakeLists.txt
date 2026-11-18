cmake_minimum_required(VERSION 3.20)
project(prometheus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings (Rigorous warnings enabled, Werror postponed until clean build verified)
if (MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core Prometheus Brain Library
add_library(prometheus_brain
    src/brain/EmaFilter.cpp
    src/brain/PressureEstimator.cpp
    src/brain/HomeostasisEngine.cpp
    src/brain/BehaviourFSM.cpp
)

target_include_directories(prometheus_brain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Test Harness: Genuine GoogleTest via FetchContent
enable_testing()

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
# For Windows MSVC: Prevent overriding the parent project's runtime library settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_subdirectory(tests)
