#include "prometheus/core/RuntimeCoordinator.hpp"

namespace prometheus::core {

// STUB — Phase 0 skeleton.
// Real implementation: TASK-2.5 (wire sensor worker, simulation thread,
// persistence worker; publish snapshots via queued signal).
//
// ActiveEmergencies ownership: this coordinator is the ONLY place that mutates
// the active_emergencies_ state. BehaviourFSM::evaluate() returns
// BehaviourDecision.detected_emergencies and cleared_emergencies deltas;
// the coordinator applies them here each tick (BehaviourFSM design invariant 2).

RuntimeCoordinator::RuntimeCoordinator() = default;
RuntimeCoordinator::~RuntimeCoordinator() { stop(); }

void RuntimeCoordinator::start() {
    // TODO: start sensor worker, simulation thread, persistence worker
}

void RuntimeCoordinator::stop() {
    // TODO: signal threads to stop, join, save final checkpoint
}

std::shared_ptr<const common::OrganismSnapshot> RuntimeCoordinator::latestSnapshot() const noexcept {
    // TODO: atomic load of published snapshot
    return std::make_shared<const common::OrganismSnapshot>();
}

const brain::ActiveEmergencies& RuntimeCoordinator::activeEmergencies() const noexcept {
    // TODO: return the live-mutated emergencies struct
    static const brain::ActiveEmergencies kEmpty{};
    return kEmpty;
}

} // namespace prometheus::core
