#pragma once

#include "prometheus/sensory/SensoryProvider.hpp"
#include <memory>

namespace prometheus::sensory {

/**
 * @brief Background thread that samples a SensoryProvider at a configured
 *        interval and maintains a mutex-protected latest HostObservation.
 *
 * The simulation thread calls latest() without blocking on I/O.
 * Staleness detection lives here: observations older than the configured
 * threshold have their metrics marked valid=false before being returned.
 *
 * STATUS: STUB — owned by Dev A (TASK-2.1).
 */
class SensorWorker {
public:
    explicit SensorWorker(std::shared_ptr<SensoryProvider> provider,
                          int sampleIntervalMs = 1000);
    ~SensorWorker();

    SensorWorker(const SensorWorker&) = delete;
    SensorWorker& operator=(const SensorWorker&) = delete;

    void start();
    void stop();

    /// Thread-safe. Returns the latest observation (with staleness applied).
    [[nodiscard]] common::HostObservation latest() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace prometheus::sensory
