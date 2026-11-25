# ---- Brain tests (Dev B) ----
add_executable(prometheus_brain_tests
    brain/test_ema_filter.cpp
    brain/test_pressure_estimator.cpp
    brain/test_homeostasis_engine.cpp
    brain/test_behaviour_fsm.cpp
)
target_link_libraries(prometheus_brain_tests PRIVATE
    prometheus_brain
    GTest::gtest
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(prometheus_brain_tests)

# ---- Body tests (Dev C) ----
add_executable(prometheus_body_tests
    body/test_particle_system.cpp
)
target_link_libraries(prometheus_body_tests PRIVATE
    prometheus_body
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(prometheus_body_tests)

# ---- Metabolism tests ----
add_executable(prometheus_metabolism_tests
    metabolism/test_metabolism.cpp
)
target_link_libraries(prometheus_metabolism_tests PRIVATE
    prometheus_metabolism
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(prometheus_metabolism_tests)

# ---- Memory tests ----
add_executable(prometheus_memory_tests
    memory/test_memory_manager.cpp
)
target_link_libraries(prometheus_memory_tests PRIVATE
    prometheus_memory
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(prometheus_memory_tests)

# ---- UI / ViewModel tests (Dev C) ----
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(prometheus_ui_tests
    ui/test_organism_viewmodel.cpp
    ui/test_locomotion_engine.cpp
)
target_link_libraries(prometheus_ui_tests PRIVATE
    prometheus_ui
    Qt6::Test
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(prometheus_ui_tests)
