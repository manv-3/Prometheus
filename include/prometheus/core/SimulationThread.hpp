#pragma once

#include "prometheus/common/OrganismSnapshot.hpp"
#include <memory>
#include <functional>

namespace prometheus::core {

/**
 * @brief Runs Brain + Metabolism + Body on a dedicated thread at ~1 Hz decision
 *        rate and ~10-30 Hz simulation rate (budget-controlled).
 *
 * Publishes an immutable OrganismSnapshot after each decision tick via a
 * caller-supplied callback. The callback is invoked on the simulation thread;
 * use a queued signal or atomic swap to hand off to the GUI thread.
 *
 * The render clock is completely independent — a rendered frame must never
 * trigger a simulation step (REQ-BODY-02).
 *
 * STATUS: STUB — Phase 0 skeleton. Real implementation: TASK-1.8 / TASK-2.5.
 */
class SimulationThread {
public:
    using SnapshotCallback = std::function<void(std::shared_ptr<const common::OrganismSnapshot>)>;

    explicit SimulationThread(SnapshotCallback onSnapshot = nullptr);
    ~SimulationThread();

    SimulationThread(const SimulationThread&) = delete;
    SimulationThread& operator=(const SimulationThread&) = delete;

    void start();
    void stop();

    [[nodiscard]] bool isRunning() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace prometheus::core
