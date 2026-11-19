#pragma once

#include "prometheus/sensory/SensoryProvider.hpp"
#include <memory>

namespace prometheus::sensory {

/**
 * @brief Background thread that samples a SensoryProvider at a configured
 *        interval and maintains a mutex-protected latest HostObservation.
 *
 * The simulation thread calls latest() without blocking on I/O. Staleness
 * detection lives here: an observation whose timestamp is older than
 * staleThresholdMs has cpu_valid and memory_valid forced false before it is
 * returned (missing data is never zero; stale ⇒ conservative invalid).
 *
 * For a SyntheticProvider the worker also calls advance() each tick so the
 * scripted curve progresses at the sampling rate.
 *
 * STATUS: TASK-2.1 (partial).
 */
class SensorWorker {
public:
    /// @param provider          the sensory source (real or synthetic).
    /// @param sampleIntervalMs  how often the worker pulls a fresh sample.
    /// @param staleThresholdMs  observations older than this are marked invalid.
    explicit SensorWorker(std::shared_ptr<SensoryProvider> provider,
                           int sampleIntervalMs = 1000,
                           int staleThresholdMs = 5000);
    ~SensorWorker();

    SensorWorker(const SensorWorker&) = delete;
    SensorWorker& operator=(const SensorWorker&) = delete;

    void start();
    void stop();

    /// Thread-safe and non-blocking. Returns the latest observation, with
    /// staleness applied (old timestamps ⇒ valid flags cleared).
    [[nodiscard]] common::HostObservation latest() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace prometheus::sensory
