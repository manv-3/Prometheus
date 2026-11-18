#include "prometheus/platform/windows/CpuMonitor.hpp"
#include <memory>

namespace prometheus::platform::windows {

// STUB — Phase 0 skeleton. Dev A owns the real implementation (TASK-1.1).
// Real version: PDH counter init, interval-based sampling, Metric{value, valid}.

struct CpuMonitor::Impl {
    bool firstCall{true};
};

CpuMonitor::CpuMonitor() : impl_(std::make_unique<Impl>()) {}
CpuMonitor::~CpuMonitor() = default;

common::Metric CpuMonitor::sample() noexcept {
    // STUB: first call always invalid (mimics real PDH behaviour).
    // Subsequent calls return a hardcoded synthetic value so the
    // simulation thread has something to work with until Dev A ships.
    if (impl_->firstCall) {
        impl_->firstCall = false;
        return common::Metric{0.0f, false};
    }
    return common::Metric{0.10f, true}; // placeholder — not real data
}

} // namespace prometheus::platform::windows
