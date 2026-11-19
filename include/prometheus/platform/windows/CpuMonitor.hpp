#pragma once

#include <memory>

namespace prometheus::platform::windows {

/**
 * @brief Result of a CPU utilisation sample.
 * Uses separate value/valid fields to match HostObservation's convention
 * (missing data is never zero — valid=false signals absence).
 */
struct CpuReading {
    float load{0.0f};     // [0.0, 1.0]
    bool  valid{false};
};

/**
 * @brief Samples CPU utilisation via Windows PDH.
 *
 * Requires two samples to produce a valid interval-based reading.
 * First call always returns valid=false (no interval yet).
 * API failure returns valid=false — never substitutes zero.
 *
 * Lives exclusively under platform/windows/ — must never be included
 * from brain/ or metabolism/ (tech.md module dependency rule).
 *
 * STATUS: STUB — owned by Dev A (TASK-1.1). Dev C provides a working stub
 * so the app target links and runs without real PDH wiring.
 */
class CpuMonitor {
public:
    CpuMonitor();
    ~CpuMonitor();

    CpuMonitor(const CpuMonitor&) = delete;
    CpuMonitor& operator=(const CpuMonitor&) = delete;

    /// Sample CPU utilisation [0, 1]. Returns valid=false on first call or API failure.
    [[nodiscard]] CpuReading sample() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace prometheus::platform::windows
