#include "prometheus/core/SimulationThread.hpp"
#include "prometheus/core/RuntimeScheduler.hpp"
#include <thread>
#include <atomic>
#include <chrono>

namespace prometheus::core {

// TASK-1.8 — independent simulation clock.
//
// The simulation thread ticks at its own rate (~1 Hz decision loop, ~10-30 Hz
// sim loop per budget) driven entirely by steady_clock. It never fires in
// response to a rendered frame. The GUI thread's QQuickWindow::frameSwapped
// signal does NOT connect to anything here.
//
// Real integration of Brain + Metabolism + Body happens in Phase 2 (TASK-2.5).
// This skeleton proves the clock independence by running a timed loop and
// publishing empty snapshots.

struct SimulationThread::Impl {
    SnapshotCallback onSnapshot;
    std::thread thread;
    std::atomic<bool> running{false};

    // Simulation loop: runs until running == false
    void run() {
        RuntimeScheduler scheduler;
        scheduler.reset();

        while (running.load(std::memory_order_relaxed)) {
            const float dt = scheduler.simulationTick();

            // --- Phase 2: Brain::update, Metabolism::calculate, Body::update go here ---
            // For now publish an empty snapshot so the ViewModel has something to receive.
            (void)dt;

            if (onSnapshot) {
                onSnapshot(std::make_shared<const common::OrganismSnapshot>());
            }

            // Target ~20 Hz simulation tick (Nominal tier default).
            // Phase 2: replace with budget.target_sim_hz-driven sleep.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
};

SimulationThread::SimulationThread(SnapshotCallback onSnapshot)
    : impl_(std::make_unique<Impl>())
{
    impl_->onSnapshot = std::move(onSnapshot);
}

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
