#include "prometheus/platform/windows/MemoryMonitor.hpp"

// Win32 headers confined to this translation unit (tech.md module dependency
// rule: only platform/windows/ owns Win32 calls).
#include <windows.h>

#include <memory>

namespace prometheus::platform::windows {

// TASK-1.2 — real RAM availability via GlobalMemoryStatusEx.
//
//   usageFraction = 1 - (ullAvailPhys / ullTotalPhys)   (fraction of physical RAM in use)
//   availableMB   = ullAvailPhys / (1024*1024)         (absolute headroom in MB)
//
// GlobalMemoryStatusEx is stateless (needs no handle), so Impl is empty. On API
// failure every field returns valid=false — missing telemetry is never zero
// (tech.md hard rule / AGENTCONTEXT #2).
struct MemoryMonitor::Impl {};  // stateless

MemoryMonitor::MemoryMonitor() : impl_(std::make_unique<Impl>()) {}
MemoryMonitor::~MemoryMonitor() = default;

MemoryReading MemoryMonitor::sample() const noexcept {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);

    if (!GlobalMemoryStatusEx(&status)) {
        // GetLastError() is available but we do not throw or return it across
        // the hot-loop boundary; just signal absence on every field.
        return MemoryReading{0.0f, false, 0.0f, false};
    }

    const DWORDLONG total = status.ullTotalPhys;
    const DWORDLONG avail = status.ullAvailPhys;

    if (total == 0) {
        // Degenerate host report — avoid divide-by-zero.
        return MemoryReading{0.0f, false, 0.0f, false};
    }

    const double usageFraction = 1.0 - (static_cast<double>(avail) / static_cast<double>(total));
    const double availableMB   = static_cast<double>(avail) / (1024.0 * 1024.0);

    // Clamp usage to [0,1] defensively against implausible host reports.
    float usage = static_cast<float>(usageFraction);
    if (usage < 0.0f) usage = 0.0f;
    if (usage > 1.0f) usage = 1.0f;

    return MemoryReading{
        usage, true,
        static_cast<float>(availableMB), true
    };
}

} // namespace prometheus::platform::windows
