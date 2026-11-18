#include "prometheus/platform/windows/MemoryMonitor.hpp"
#include <memory>

namespace prometheus::platform::windows {

// STUB — Phase 0 skeleton. Dev A owns the real implementation (TASK-1.2).

struct MemoryMonitor::Impl {};

MemoryMonitor::MemoryMonitor() : impl_(std::make_unique<Impl>()) {}
MemoryMonitor::~MemoryMonitor() = default;

MemoryMonitor::MemoryReading MemoryMonitor::sample() noexcept {
    // STUB: return a safe placeholder so the simulation thread links and runs.
    return MemoryReading{
        common::Metric{0.30f, true},  // placeholder usage fraction
        common::Metric{8192.0f, true} // placeholder available MB
    };
}

} // namespace prometheus::platform::windows
