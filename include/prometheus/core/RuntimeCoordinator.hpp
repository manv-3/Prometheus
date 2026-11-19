#pragma once

#include "prometheus/common/OrganismSnapshot.hpp"
#include "prometheus/brain/BehaviourFSM.hpp"
#include "prometheus/core/SimulationThread.hpp"
#include <memory>
#include <functional>

namespace prometheus::core {

/**
 * @brief Owns the organism lifecycle: starts/stops threads, drives the
 *        decision loop, and publishes immutable OrganismSnapshots.
 *
 * Also owns ActiveEmergencies mutation — BehaviourFSM::evaluate() is pure
 * and returns detected/cleared emergency deltas; the Coordinator applies them.
 * (BehaviourFSM.hpp, design invariant 2: "Coordinator Ownership".)
 *
 * Threading: lives on the GUI thread but coordinates the sensor worker,
 * simulation thread, and persistence worker. Never blocks the GUI thread
 * on I/O, DB, or simulation work.
 */
class RuntimeCoordinator {
public:
    using SnapshotCallback = SimulationThread::SnapshotCallback;

    RuntimeCoordinator();
    ~RuntimeCoordinator();

    // Non-copyable, non-movable (owns threads and Qt objects)
    RuntimeCoordinator(const RuntimeCoordinator&) = delete;
    RuntimeCoordinator& operator=(const RuntimeCoordinator&) = delete;

    /// Start the organism. syntheticMode=true uses scripted telemetry.
    void start(bool syntheticMode = false);

    /// Stop all threads and clean up.
    void stop();

    /// Set the callback that receives published snapshots.
    /// Must be called before start().
    void setSnapshotCallback(SnapshotCallback callback);

    /// Latest published snapshot. Thread-safe (atomic shared_ptr swap).
    [[nodiscard]] std::shared_ptr<const common::OrganismSnapshot> latestSnapshot() const noexcept;

    /// Current active emergencies — mutated exclusively by this coordinator
    /// based on BehaviourDecision.detected_emergencies / cleared_emergencies.
    [[nodiscard]] const brain::ActiveEmergencies& activeEmergencies() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace prometheus::core
