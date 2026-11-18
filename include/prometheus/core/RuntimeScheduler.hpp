#pragma once

#include <chrono>
#include <cstdint>

namespace prometheus::core {

/**
 * @brief Tracks independent clocks for the decision loop, simulation loop,
 *        and render loop using std::chrono::steady_clock.
 *
 * Guarantees that render ticks never drive simulation steps. Each subsystem
 * queries its own elapsed time and receives a deltaTime in seconds.
 *
 * All timestamps use steady_clock (monotonic). Wall-clock time is never
 * used for control logic (tech.md convention).
 *
 * STATUS: STUB — Phase 0 skeleton. Real implementation: TASK-1.8.
 */
class RuntimeScheduler {
public:
    RuntimeScheduler();

    /// Call at the start of each decision loop iteration.
    /// Returns deltaTime in seconds since last decision tick.
    [[nodiscard]] float decisionTick() noexcept;

    /// Call at the start of each simulation sub-step.
    /// Returns deltaTime in seconds since last simulation tick.
    [[nodiscard]] float simulationTick() noexcept;

    /// Reset all clocks (call before starting the loops).
    void reset() noexcept;

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint lastDecision_;
    TimePoint lastSimulation_;
};

} // namespace prometheus::core
