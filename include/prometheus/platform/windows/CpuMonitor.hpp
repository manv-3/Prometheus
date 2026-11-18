#pragma once

#include "prometheus/common/HostObservation.hpp"

namespace prometheus::platform::windows {

/**
 * @brief Samples CPU utilisation via Windows PDH.
 *
 * Requires two samples to produce a valid interval-based reading.
 * First call always returns Metric{valid=false} (no interval yet).
 * API failure returns Metric{valid=false} — never substitutes zero.
 *
 * Lives exclusively under platform/windows/ — must never be included
 * from brain/ or metabolism/ (tech.md module dependency rule).
 *
 * STATUS: STUB — owned by Dev A (TASK-1.1).
 */
class CpuMonitor {
public:
    CpuMonitor();
    ~CpuMonitor();

    CpuMonitor(const CpuMonitor&) = delete;
    CpuMonitor& operator=(const CpuMonitor&) = delete;

    /// Sample CPU utilisation [0, 1]. Returns valid=false on first call or API failure.
    [[nodiscard]] common::Metric sample() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace prometheus::platform::windows
