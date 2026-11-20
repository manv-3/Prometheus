#include "prometheus/core/SimulationThread.hpp"
#include "prometheus/core/RuntimeScheduler.hpp"
#include "prometheus/sensory/SensorWorker.hpp"
#include "prometheus/sensory/SyntheticProvider.hpp"
#include "prometheus/brain/PressureEstimator.hpp"
#include "prometheus/brain/HomeostasisEngine.hpp"
#include "prometheus/brain/BehaviourFSM.hpp"
#include "prometheus/metabolism/Metabolism.hpp"
#include "prometheus/memory/MemoryManager.hpp"
#include "prometheus/body/ParticleSystem.hpp"

#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <optional>

namespace prometheus::core {

// ---------------------------------------------------------------------------
// Simulation pipeline with persistence and dormancy (Phase 2/3):
//
//   SensorWorker → PressureEstimator → BehaviourFSM → HomeostasisEngine
//                → Metabolism → ParticleSystem → OrganismSnapshot
//                              ↕
//                         MemoryManager (checkpoints)
//
// Coordinator responsibilities (BehaviourFSM design notes):
//   - dwell_time_seconds: advanced here, NOT in HomeostasisEngine
//   - ActiveEmergencies: mutated here from FSM decision deltas
//   - Dormancy: park the loop, heartbeat, wake on pressure recovery
//   - Persistence: checkpoint on transitions and clean shutdown
// ---------------------------------------------------------------------------
struct SimulationThread::Impl {
    // Pipeline components (member order matches initializer list)
    std::shared_ptr<sensory::SensoryProvider> provider;
    std::unique_ptr<sensory::SensorWorker>    sensorWorker;
    brain::PressureEstimator                  pressureEstimator;
    brain::HomeostasisEngine                  homeostasis;
    brain::BehaviourFSM                       behaviourFSM;
    metabolism::Metabolism                    metabolism;
    memory::MemoryManager                     memoryManager;
    body::ParticleSystem                      particles;

    // Organism state (owned by this thread)
    common::OrganismState    state{};
    brain::ActiveEmergencies activeEmergencies{};
    float                    simTimeSeconds{0.0f};

    // Tick-rate measurement
    uint32_t tickCount{0};
    float    tickAccumSeconds{0.0f};
    float    measuredSimHz{0.0f};

    // Threading (declared after non-trivial members so they're initialised last)
    bool             syntheticMode{false};
    SnapshotCallback onSnapshot;
    std::atomic<bool> running{false};
    std::thread      thread;

    Impl(SnapshotCallback cb,
         std::shared_ptr<sensory::SensoryProvider> prov,
         bool synthetic,
         const std::string& dbPath,
         std::optional<common::QualityTier> fixedTier)
        : memoryManager(dbPath)
        , syntheticMode(synthetic)
        , onSnapshot(std::move(cb))
    {
        if (prov) {
            provider = std::move(prov);
        } else {
            provider = std::make_shared<sensory::SyntheticProvider>("ramp-up");
        }
        sensorWorker = std::make_unique<sensory::SensorWorker>(provider, 1000);

        // Apply fixed-tier mode (TASK-4.2) — pins Metabolism to constant budget
        if (fixedTier.has_value()) {
            metabolism.setFixedTier(fixedTier);
        }
    }

    // ---- Persistence helpers ----

    void saveCheckpoint(memory::SaveTrigger trigger) {
        memoryManager.saveCheckpoint(state, trigger);
    }

    void loadCheckpoint() {
        auto result = memoryManager.loadCheckpoint();
        if (result.found) {
            state = result.state;
        }
        // Not found → default-initialised state (new organism)
    }

    // ---- Tick sleep driven by budget ----
    static void sleepForBudget(const common::ComputeBudget& budget) {
        float hz = static_cast<float>(
            std::clamp(budget.target_sim_hz,
                       common::ComputeBudget::HARD_MIN_SIM_HZ,
                       common::ComputeBudget::HARD_MAX_SIM_HZ));
        auto ms = static_cast<int>(1000.0f / hz);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    // ---- Dormancy heartbeat ----
    // Returns true when the organism should wake (pressure recovered enough).
    bool dormancyHeartbeat() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5000)); // 0.2 Hz

        common::HostObservation obs = sensorWorker->latest();
        brain::PressureResult pressure = pressureEstimator.update(obs, 5.0f);

        const auto& cfg = behaviourFSM.config();
        bool pressureOk = pressure.has_smoothed_value
            && pressure.smoothed_pressure < cfg.pressure_stress_exit;
        bool healthOk  = state.health  >= cfg.health_dormant_exit;
        bool energyOk  = state.energy  >= cfg.energy_dormant_exit;

        return pressureOk && healthOk && energyOk;
    }

    // ---- Main loop ----
    void run() {
        RuntimeScheduler scheduler;
        scheduler.reset();

        sensorWorker->start();
        loadCheckpoint(); // restore identity before first tick (REQ-MEMORY-01)

        common::Behaviour prevBehaviour = state.current_behaviour;

        while (running.load(std::memory_order_relaxed)) {

            // === DORMANCY: park the loop, heartbeat until recovery ===
            if (state.current_behaviour == common::Behaviour::Dormant) {
                saveCheckpoint(memory::SaveTrigger::DormancyEntry);

                bool awake = false;
                while (running.load(std::memory_order_relaxed) && !awake) {
                    awake = dormancyHeartbeat();
                }
                if (!running.load(std::memory_order_relaxed)) break;

                // Wake: reset state, reschedule, save
                state.current_behaviour  = common::Behaviour::Idle;
                state.dwell_time_seconds = 0.0f;
                scheduler.reset();
                saveCheckpoint(memory::SaveTrigger::DormancyExit);
                prevBehaviour = common::Behaviour::Idle;
                continue;
            }

            // === NORMAL TICK ===
            const float dt = scheduler.simulationTick();

            // 1. Host observation
            common::HostObservation obs = sensorWorker->latest();

            // 2. Pressure
            brain::PressureResult pressure = pressureEstimator.update(obs, dt);

            // 3. Behaviour FSM (pure — does not mutate state)
            brain::BehaviourDecision decision = behaviourFSM.evaluate(
                state, pressure, obs, activeEmergencies
            );

            // 4. Apply transition
            if (decision.transition_occurred) {
                state.current_behaviour  = decision.behaviour;
                state.dwell_time_seconds = 0.0f;
            }

            // Emergency deltas (clear-then-assert per BehaviourFSM invariant 3)
            activeEmergencies.thermal_throttling &= !decision.cleared_emergencies.thermal_throttling;
            activeEmergencies.critical_battery   &= !decision.cleared_emergencies.critical_battery;
            activeEmergencies.biological_collapse &= !decision.cleared_emergencies.biological_collapse;
            activeEmergencies.thermal_throttling |= decision.detected_emergencies.thermal_throttling;
            activeEmergencies.critical_battery   |= decision.detected_emergencies.critical_battery;
            activeEmergencies.biological_collapse |= decision.detected_emergencies.biological_collapse;

            // 5. Advance dwell time (coordinator owns this)
            constexpr float kMaxDwell = 315360000.0f; // ~10 years
            state.dwell_time_seconds = std::min(state.dwell_time_seconds + dt, kMaxDwell);

            // 6. Homeostasis drives
            state = homeostasis.update(state, pressure, dt, obs.is_user_active);

            // 7. Budget (pressure → behaviour → budget chain complete)
            common::ComputeBudget budget = metabolism.calculate(
                obs, state, pressure.smoothed_pressure, dt
            );

            // 8. Particles
            particles.update(budget, dt);

            // 9. Tick-rate measurement
            simTimeSeconds += dt;
            tickCount++;
            tickAccumSeconds += dt;
            if (tickAccumSeconds >= 1.0f) {
                measuredSimHz  = static_cast<float>(tickCount) / tickAccumSeconds;
                tickCount      = 0;
                tickAccumSeconds = 0.0f;
            }

            // 10. Checkpoint on behaviour transition (REQ-MEMORY-02)
            if (state.current_behaviour != prevBehaviour) {
                saveCheckpoint(memory::SaveTrigger::BehaviourTransition);
                prevBehaviour = state.current_behaviour;
            }

            // 11. Publish immutable snapshot
            auto snap = std::make_shared<common::OrganismSnapshot>();
            snap->observation            = obs;
            snap->state                  = state;
            snap->budget                 = budget;
            snap->environmental_pressure = pressure.smoothed_pressure;
            snap->sim_time_seconds       = simTimeSeconds;
            snap->active_particles       = particles.activeCount();
            snap->measured_sim_hz        = measuredSimHz;
            snap->telemetry_source       = syntheticMode
                ? common::TelemetrySource::Synthetic
                : common::TelemetrySource::Real;
            snap->dormant                = (state.current_behaviour == common::Behaviour::Dormant);

            if (onSnapshot) {
                onSnapshot(std::shared_ptr<const common::OrganismSnapshot>(std::move(snap)));
            }

            // 12. Sleep driven by budget
            sleepForBudget(budget);
        }

        // Clean shutdown: save final checkpoint (REQ-MEMORY-02)
        saveCheckpoint(memory::SaveTrigger::CleanShutdown);
        sensorWorker->stop();
    }
};

// ---- Public interface ----

SimulationThread::SimulationThread(
    SnapshotCallback onSnapshot,
    std::shared_ptr<sensory::SensoryProvider> provider,
    bool syntheticMode,
    const std::string& dbPath,
    std::optional<common::QualityTier> fixedTier
)
    : impl_(std::make_unique<Impl>(
        std::move(onSnapshot),
        std::move(provider),
        syntheticMode,
        dbPath,
        fixedTier))
{}

SimulationThread::~SimulationThread() { stop(); }

void SimulationThread::start() {
    if (impl_->running.load()) return;
    impl_->running.store(true, std::memory_order_relaxed);
    impl_->thread = std::thread([this] { impl_->run(); });
}

void SimulationThread::stop() {
    if (!impl_->running.load()) return;
    impl_->running.store(false, std::memory_order_relaxed);
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

bool SimulationThread::isRunning() const noexcept {
    return impl_->running.load(std::memory_order_relaxed);
}

} // namespace prometheus::core
