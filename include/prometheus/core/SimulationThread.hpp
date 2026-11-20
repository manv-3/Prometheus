#pragma once

#include "prometheus/common/OrganismSnapshot.hpp"
#include "prometheus/sensory/SensoryProvider.hpp"
#include "prometheus/brain/BehaviourFSM.hpp"
#include <memory>
#include <functional>
#include <string>

namespace prometheus::core {

/**
 * @brief Runs the full organism simulation pipeline on a dedicated thread.
 *
 * Pipeline per tick:
 *   SensorWorker → PressureEstimator → BehaviourFSM → HomeostasisEngine
 *                → Metabolism → ParticleSystem → OrganismSnapshot
 *
 * Also handles:
 *   - Identity load from SQLite before the first tick (REQ-MEMORY-01)
 *   - Checkpoint saves on behaviour transitions and clean shutdown (REQ-MEMORY-02)
 *   - Real dormancy: parks the loop, heartbeats, wakes on pressure recovery (REQ-DORMANCY-01)
 *   - Budget-driven tick rate: sleeps for 1/budget.target_sim_hz between ticks
 *   - Synthetic-mode labelling on every published snapshot (REQ-UI-02)
 *
 * Coordinator invariants (BehaviourFSM design notes):
 *   - dwell_time_seconds is advanced here, NOT in HomeostasisEngine
 *   - ActiveEmergencies is mutated here from FSM decision deltas
 */
class SimulationThread {
public:
    using SnapshotCallback = std::function<void(std::shared_ptr<const common::OrganismSnapshot>)>;

    explicit SimulationThread(
        SnapshotCallback onSnapshot = nullptr,
        std::shared_ptr<sensory::SensoryProvider> provider = nullptr,
        bool syntheticMode = false,
        const std::string& dbPath = "prometheus.db",
        std::optional<common::QualityTier> fixedTier = std::nullopt
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
