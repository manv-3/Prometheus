#pragma once

#include <memory>

namespace prometheus::platform::windows {

/**
 * @brief Result of a RAM availability sample.
 * Uses separate value/valid fields to match HostObservation's convention.
 */
struct MemoryReading {
    float usageFraction{0.0f};   // [0.0, 1.0]
    bool  usageValid{false};
    float availableMB{0.0f};      // absolute headroom in megabytes
    bool  availableValid{false};
};

/**
 * @brief Samples available RAM via GlobalMemoryStatusEx.
 *
 * API failure returns all fields with valid=false.
 *
 * STATUS: STUB — owned by Dev A (TASK-1.2). Dev C provides a working stub.
 */
class MemoryMonitor {
public:
    MemoryMonitor();
    ~MemoryMonitor();

    MemoryMonitor(const MemoryMonitor&) = delete;
    MemoryMonitor& operator=(const MemoryMonitor&) = delete;

    /// Sample RAM. Returns valid=false on API failure.
    [[nodiscard]] MemoryReading sample() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace prometheus::platform::windows
