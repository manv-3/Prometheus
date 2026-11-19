#include "prometheus/core/SimulationThread.hpp"
#include "prometheus/core/RuntimeScheduler.hpp"
#include "prometheus/sensory/SensorWorker.hpp"
#include "prometheus/sensory/SyntheticProvider.hpp"
#include "prometheus/brain/PressureEstimator.hpp"
#include "prometheus/brain/HomeostasisEngine.hpp"
#include "prometheus/brain/BehaviourFSM.hpp"
#include "prometheus/metabolism/Metabolism.hpp"
#include "prometheus/body/ParticleSystem.hpp"

#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

namespace prometheus::core {

// ---------------------------------------------------------------------------
// Simulation pipeline (all owned by this thread — no shared mutable state):
//
//   SensorWorker → PressureEstimator → BehaviourFSM → HomeostasisEngine
//                → Metabolism → ParticleSystem → OrganismSnapshot
//
// Coordinator responsibilities (BehaviourFSM design invariants 2 & 4):
//   - dwell_time_seconds: advanced here, NOT in HomeostasisEngine
//   - ActiveEmergencies: mutated here from FSM decision deltas
// ---------------------------------------------------------------------------
struct SimulationThread::Impl {
    // Pipeline components
    std::shared_ptr<sensory::SensoryProvider> provider;
    std::unique_ptr<sensory::SensorWorker>    sensorWorker;
    brain::PressureEstimator                  pressureEstimator;
    brain::HomeostasisEngine                  homeostasis;
    brain::BehaviourFSM                       behaviourFSM;
    metabolism::Metabolism                    metabolism;
    body::ParticleSystem                       particles;

    // Organism state (owned by this thread)
    common::OrganismState      state;
    brain::ActiveEmergencies   activeEmergencies;
    float                      simTimeSeconds{0.0f};

    // Threading
    SnapshotCallback           onSnapshot;
    std::thread                thread;
    std::atomic<bool>          running{false};

    Impl(SnapshotCallback cb, std::shared_ptr<sensory::SensoryProvider> prov)
        : onSnapshot(std::move(cb))
    {
        // Default to synthetic telemetry if no provider given
        if (prov) {
            provider = std::move(prov);
        } else {
            provider = std::make_shared<sensory::SyntheticProvider>("ramp-up");
        }

        sensorWorker = std::make_unique<sensory::SensorWorker>(provider, 1000);
    }

    void run() {
        RuntimeScheduler scheduler;
        scheduler.reset();

        // Prime the sensor worker
        sensorWorker->start();

        while (running.load(std::memory_order_relaxed)) {
            const float dt = scheduler.simulationTick();

            // 1. Get latest host observation
            common::HostObservation obs = sensorWorker->latest();

            // 2. Update pressure estimator
            brain::PressureResult pressure = pressureEstimator.update(obs, dt);

            // 3. Evaluate behaviour FSM (pure — does not mutate state)
            brain::BehaviourDecision decision = behaviourFSM.evaluate(
                state, pressure, obs, activeEmergencies
            );

            // 4. Apply behaviour decision
            //    Clear-then-assert: detected emergencies override clearance
            if (decision.transition_occurred) {
                state.current_behaviour = decision.behaviour;
                state.dwell_time_seconds = 0.0f;
            }

            // Apply emergency deltas (clear-then-assert per BehaviourFSM invariant 3)
            activeEmergencies.thermal_throttling  &= !decision.cleared_emergencies.thermal_throttling;
            activeEmergencies.critical_battery     &= !decision.cleared_emergencies.critical_battery;
            activeEmergencies.biological_collapse  &= !decision.cleared_emergencies.biological_collapse;
            activeEmergencies.thermal_throttling  |= decision.detected_emergencies.thermal_throttling;
            activeEmergencies.critical_battery     |= decision.detected_emergencies.critical_battery;
            activeEmergencies.biological_collapse  |= decision.detected_emergencies.biological_collapse;

            // 5. Advance dwell time (coordinator owns this — HomeostasisEngine does NOT)
            state.dwell_time_seconds = std::min(
                state.dwell_time_seconds + dt,
                315360000.0f // ~10 years overflow guard
            );

            // 6. Update homeostasis drives (preserves behaviour + dwell_time)
            state = homeostasis.update(state, pressure, dt, obs.is_user_active);

            // 7. Compute budget from behaviour + smoothed pressure + dt
            //    The pressure → behaviour → budget → workload chain is completed here.
            common::ComputeBudget budget = metabolism.calculate(
                obs, state, pressure.smoothed_pressure, dt
            );

            // 8. Update particle simulation within budget
            particles.update(budget, dt);

            // 9. Advance simulation time
            simTimeSeconds += dt;

            // 10. Assemble and publish immutable snapshot
            auto snap = std::make_shared<common::OrganismSnapshot>();
            snap->observation = obs;
            snap->state       = state;
            snap->budget      = budget;
            snap->environmental_pressure = pressure.smoothed_pressure;
            snap->sim_time_seconds = simTimeSeconds;

            if (onSnapshot) {
                onSnapshot(std::shared_ptr<const common::OrganismSnapshot>(std::move(snap)));
            }

            // Target ~20 Hz simulation tick (Nominal tier default).
            // TODO: replace with budget.target_sim_hz-driven sleep in Phase 2.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        sensorWorker->stop();
    }
};

// ---- Public interface ----

SimulationThread::SimulationThread(
    SnapshotCallback onSnapshot,
    std::shared_ptr<sensory::SensoryProvider> provider
)
    : impl_(std::make_unique<Impl>(std::move(onSnapshot), std::move(provider)))
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
