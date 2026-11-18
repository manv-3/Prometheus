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
