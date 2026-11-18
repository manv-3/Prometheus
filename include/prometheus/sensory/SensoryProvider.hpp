#pragma once

#include "prometheus/common/HostObservation.hpp"

namespace prometheus::sensory {

/**
 * @brief Portable interface for host observation providers.
 *
 * Both the real Windows sensor stack (CpuMonitor + MemoryMonitor) and the
 * SyntheticProvider implement this interface. Brain and Metabolism never
 * depend on a concrete provider — only on this interface.
 *
 * latest() must be thread-safe and must never block on I/O.
 */
class SensoryProvider {
public:
    virtual ~SensoryProvider() = default;

    /// Returns the most recent HostObservation. Thread-safe; non-blocking.
    [[nodiscard]] virtual common::HostObservation latest() const = 0;
};

} // namespace prometheus::sensory
