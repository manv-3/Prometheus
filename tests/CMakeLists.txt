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
