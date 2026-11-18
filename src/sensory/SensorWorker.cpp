#include "prometheus/sensory/SensorWorker.hpp"

namespace prometheus::sensory {

// STUB — Phase 0 skeleton. Dev A owns the real implementation (TASK-2.1).

struct SensorWorker::Impl {
    std::shared_ptr<SensoryProvider> provider;
    int sampleIntervalMs{1000};
    bool running{false};
};

SensorWorker::SensorWorker(std::shared_ptr<SensoryProvider> provider, int sampleIntervalMs)
    : impl_(std::make_unique<Impl>())
{
    impl_->provider = std::move(provider);
    impl_->sampleIntervalMs = sampleIntervalMs;
}

SensorWorker::~SensorWorker() { stop(); }

void SensorWorker::start()  { impl_->running = true;  /* TODO: launch thread */ }
void SensorWorker::stop()   { impl_->running = false; /* TODO: join thread   */ }

common::HostObservation SensorWorker::latest() const {
    // STUB: delegate directly — no staleness check yet
    if (impl_->provider) {
        return impl_->provider->latest();
    }
    return common::HostObservation{};
}

} // namespace prometheus::sensory
