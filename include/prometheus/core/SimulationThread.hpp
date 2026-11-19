#pragma once

#include "prometheus/common/OrganismSnapshot.hpp"
#include "prometheus/sensory/SensoryProvider.hpp"
#include "prometheus/brain/BehaviourFSM.hpp"
#include <memory>
#include <functional>

namespace prometheus::core {

/**
 * @brief Runs Brain + Metabolism + Body on a dedicated thread at ~1 Hz decision
 *        rate and ~10-30 Hz simulation rate (budget-controlled).
 *
 * Owns the full simulation pipeline internally:
 *   SensorWorker → PressureEstimator → BehaviourFSM → HomeostasisEngine
 *                → Metabolism → ParticleSystem → OrganismSnapshot
 *
 * The coordinator (this thread) is the exclusive owner of:
 *   - dwell_time_seconds advancement (BehaviourFSM design invariant 2)
 *   - ActiveEmergencies mutation (applies detected/cleared deltas from FSM)
 *
 * Publishes an immutable OrganismSnapshot after each tick via a
 * caller-supplied callback. The callback is invoked on the simulation thread;
 * use a queued signal or atomic swap to hand off to the GUI thread.
 *
 * The render clock is completely independent — a rendered frame must never
 * trigger a simulation step (REQ-BODY-02).
 */
class SimulationThread {
public:
    using SnapshotCallback = std::function<void(std::shared_ptr<const common::OrganismSnapshot>)>;

    /// Construct with a telemetry provider (synthetic or real).
    /// If nullptr, defaults to a SyntheticProvider("steady-low").
    explicit SimulationThread(
        SnapshotCallback onSnapshot = nullptr,
        std::shared_ptr<sensory::SensoryProvider> provider = nullptr
    );
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
