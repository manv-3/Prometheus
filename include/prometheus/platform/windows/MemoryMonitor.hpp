#pragma once

#include "prometheus/common/HostObservation.hpp"

namespace prometheus::platform::windows {

/**
 * @brief Samples available RAM via GlobalMemoryStatusEx.
 *
 * Returns ramUsageFraction [0,1] and availableRamMB.
 * API failure returns both metrics with valid=false.
 *
 * STATUS: STUB — owned by Dev A (TASK-1.2).
 */
class MemoryMonitor {
public:
    MemoryMonitor();
    ~MemoryMonitor();

    MemoryMonitor(const MemoryMonitor&) = delete;
    MemoryMonitor& operator=(const MemoryMonitor&) = delete;

    struct MemoryReading {
        common::Metric usageFraction;   // [0, 1]
        common::Metric availableMB;
    };

    /// Sample RAM. Returns valid=false on API failure.
    [[nodiscard]] MemoryReading sample() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace prometheus::platform::windows
