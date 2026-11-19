#include "prometheus/core/RuntimeCoordinator.hpp"
#include "prometheus/core/SimulationThread.hpp"
#include "prometheus/sensory/SyntheticProvider.hpp"
#include "prometheus/sensory/RealSensorProvider.hpp"

#include <QMetaType>

// Must be at global scope, not inside a namespace
Q_DECLARE_METATYPE(std::shared_ptr<const prometheus::common::OrganismSnapshot>)

namespace prometheus::core {

struct RuntimeCoordinator::State {
    SnapshotCallback callback;
    std::unique_ptr<SimulationThread> simThread;
    brain::ActiveEmergencies activeEmergencies{};
};

RuntimeCoordinator::RuntimeCoordinator()
    : state_(std::make_unique<State>())
{
    qRegisterMetaType<std::shared_ptr<const common::OrganismSnapshot>>();
}

RuntimeCoordinator::~RuntimeCoordinator() { stop(); }

void RuntimeCoordinator::setSnapshotCallback(SnapshotCallback callback) {
    state_->callback = std::move(callback);
}

void RuntimeCoordinator::start(bool syntheticMode) {
    if (state_->simThread) return; // already running

    // Choose telemetry provider based on mode
    std::shared_ptr<sensory::SensoryProvider> provider;
    if (syntheticMode) {
        provider = std::make_shared<sensory::SyntheticProvider>("ramp-up");
    } else {
        provider = std::make_shared<sensory::RealSensorProvider>();
    }

    // Create simulation thread with snapshot publishing callback
    state_->simThread = std::make_unique<SimulationThread>(
        state_->callback,
        provider
    );

    state_->simThread->start();
}

void RuntimeCoordinator::stop() {
    if (state_->simThread) {
        state_->simThread->stop();
        state_->simThread.reset();
    }
}

std::shared_ptr<const common::OrganismSnapshot> RuntimeCoordinator::latestSnapshot() const noexcept {
    // TODO: atomic load of published snapshot (Phase 2 TASK-2.5)
    return std::make_shared<const common::OrganismSnapshot>();
}

const brain::ActiveEmergencies& RuntimeCoordinator::activeEmergencies() const noexcept {
    return state_->activeEmergencies;
}

} // namespace prometheus::core
