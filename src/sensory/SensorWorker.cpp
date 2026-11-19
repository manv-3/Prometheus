#include "prometheus/sensory/SensorWorker.hpp"
#include "prometheus/sensory/SyntheticProvider.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace prometheus::sensory {

// TASK-2.1 (partial) — background sampling thread.
//
// The worker owns a std::thread that wakes every sampleIntervalMs, pulls the
// latest HostObservation from the provider, and stores it under a mutex. For a
// SyntheticProvider it also calls advance() so the scripted curve progresses at
// the sampling rate. We deliberately keep advance() off the base SensoryProvider
// interface so real providers don't have to fake it.
//
// latest() is non-blocking: it copies the stored observation under the mutex,
// then applies staleness detection — if (now - observation.timestamp) exceeds
// staleThresholdMs, cpu_valid and memory_valid are forced false before return
// (missing data is never zero; stale ⇒ conservative invalid).

struct SensorWorker::Impl {
    std::shared_ptr<SensoryProvider> provider;
    int sampleIntervalMs{1000};
    std::chrono::milliseconds staleThreshold{5000};

    std::thread thread;
    std::atomic<bool> running{false};

    mutable std::mutex mtx;
    common::HostObservation latestObs;          // guarded by mtx
    std::atomic<bool> hasObservation{false};

    void sampleOnce() {
        if (!provider) return;

        common::HostObservation obs;

        // If the provider is a scripted synthetic curve, advance it each tick so
        // the frame index tracks the sampling rate. Real providers only expose
        // latest(); they have no advance() (and we must not force one onto the
        // portable SensoryProvider interface).
        if (auto synth = std::dynamic_pointer_cast<SyntheticProvider>(provider)) {
            obs = synth->latest();
            synth->advance();
        } else {
            obs = provider->latest();
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            latestObs = obs;
        }
        hasObservation.store(true, std::memory_order_release);
    }

    void run() {
        while (running.load(std::memory_order_relaxed)) {
            sampleOnce();
            std::this_thread::sleep_for(std::chrono::milliseconds(sampleIntervalMs));
        }
    }
};

SensorWorker::SensorWorker(std::shared_ptr<SensoryProvider> provider,
                           int sampleIntervalMs,
                           int staleThresholdMs)
    : impl_(std::make_unique<Impl>()) {
    impl_->provider = std::move(provider);
    impl_->sampleIntervalMs = sampleIntervalMs;
    if (staleThresholdMs > 0) {
        impl_->staleThreshold = std::chrono::milliseconds(staleThresholdMs);
    }
}

SensorWorker::~SensorWorker() { stop(); }

void SensorWorker::start() {
    if (impl_->running.load(std::memory_order_relaxed)) return;

    // Prime an initial observation immediately so consumers don't see the
    // default-constructed (all-invalid) frame on the very first read after start.
    impl_->sampleOnce();

    impl_->running.store(true, std::memory_order_relaxed);
    impl_->thread = std::thread([this] { impl_->run(); });
}

void SensorWorker::stop() {
    impl_->running.store(false, std::memory_order_relaxed);
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

common::HostObservation SensorWorker::latest() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    common::HostObservation obs = impl_->latestObs;

    if (!impl_->hasObservation.load(std::memory_order_acquire)) {
        // Never sampled yet: return the default observation (all valid flags
        // already false by HostObservation's default-initialised fields).
        return obs;
    }

    const auto age = std::chrono::steady_clock::now() - obs.timestamp;
    if (age > impl_->staleThreshold) {
        obs.cpu_valid = false;
        obs.memory_valid = false;
    }
    return obs;
}

} // namespace prometheus::sensory
