#pragma once

#include "prometheus/common/OrganismSnapshot.hpp"
#include "prometheus/brain/BehaviourFSM.hpp"
#include "prometheus/core/SimulationThread.hpp"
#include "prometheus/metabolism/Metabolism.hpp"
#include <memory>
#include <functional>
#include <optional>

namespace prometheus::core {

/**
 * @brief Owns the organism lifecycle: starts/stops threads, drives the
 *        decision loop, and publishes immutable OrganismSnapshots.
 */
class RuntimeCoordinator {
public:
    using SnapshotCallback = SimulationThread::SnapshotCallback;

    RuntimeCoordinator();
    ~RuntimeCoordinator();

    RuntimeCoordinator(const RuntimeCoordinator&) = delete;
    RuntimeCoordinator& operator=(const RuntimeCoordinator&) = delete;

    /**
     * @brief Start the organism.
     * @param syntheticMode true = scripted telemetry (REQ-SENSE-03).
     * @param fixedTier     When set, bypasses adaptive Metabolism and pins
     *                      the budget to this tier (TASK-4.2 / REQ-BENCH-01).
     *                      Used for the fixed-complexity baseline in benchmarks.
     */
    void start(bool syntheticMode = false,
               std::optional<common::QualityTier> fixedTier = std::nullopt);

    void stop();

    void setSnapshotCallback(SnapshotCallback callback);

    [[nodiscard]] std::shared_ptr<const common::OrganismSnapshot> latestSnapshot() const noexcept;
    [[nodiscard]] const brain::ActiveEmergencies& activeEmergencies() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace prometheus::core
