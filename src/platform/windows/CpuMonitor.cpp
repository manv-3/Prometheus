#include "prometheus/platform/windows/CpuMonitor.hpp"

// Win32 / PDH headers are confined to this translation unit. tech.md module
// dependency rule: brain/ and metabolism/ must never include windows.h or pdh.h
// — only platform/windows/ owns the Win32 calls.
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <memory>

namespace prometheus::platform::windows {

// TASK-1.1 — real CPU utilisation via the Performance Data Helper (PDH).
//
// "\Processor(_Total)\% Processor Time" is a *rate* counter: PDH needs two
// collected samples to integrate over the interval between them. The first
// sample() therefore primes the counter and returns valid=false ("no interval
// yet"). Subsequent calls collect a new sample and read the formatted value,
// which PDH computes as the average utilisation over the last interval.
//
// On *any* PDH API failure we return valid=false — missing telemetry is never
// substituted with a fabricated number (tech.md hard rule / AGENTCONTEXT #2).
//
// NOTE: the counter path uses English names. On a non-English Windows install
// the path may fail to resolve, in which case sample() returns valid=false —
// the correct degradation. PdhAddEnglishCounter is locale-independent but not
// declared across every toolchain, so we keep the documented path here.
struct CpuMonitor::Impl {
    PDH_HQUERY   query{nullptr};
    PDH_HCOUNTER counter{nullptr};
    bool         firstCall{true};
    bool         ready{false};   // false until query + counter are successfully opened
};

CpuMonitor::CpuMonitor() : impl_(std::make_unique<Impl>()) {
    PDH_STATUS status = PdhOpenQueryA(nullptr, 0, &impl_->query);
    if (status != ERROR_SUCCESS) {
        impl_->query = nullptr;
        return;
    }

    // Average % over all logical processors, expressed as [0,100] by PDH.
    status = PdhAddCounterA(impl_->query,
                            "\\Processor(_Total)\\% Processor Time",
                            0, &impl_->counter);
    if (status != ERROR_SUCCESS) {
        PdhCloseQuery(impl_->query);
        impl_->query   = nullptr;
        impl_->counter = nullptr;
        return;
    }

    impl_->ready = true;
}

CpuMonitor::~CpuMonitor() {
    if (impl_ && impl_->query) {
        PdhCloseQuery(impl_->query);
        impl_->query = nullptr;
    }
}

CpuReading CpuMonitor::sample() const noexcept {
    if (!impl_->ready || impl_->query == nullptr || impl_->counter == nullptr) {
        return CpuReading{0.0f, false};
    }

    // Collect the current sample into the query's internal buffer.
    PDH_STATUS status = PdhCollectQueryData(impl_->query);
    if (status != ERROR_SUCCESS) {
        // e.g. PDH_NO_DATA on the very first collect — leave firstCall untouched
        // so the next successful collect still primes before we read a value.
        return CpuReading{0.0f, false};
    }

    // First successful collect only primes the counter — no interval exists yet.
    if (impl_->firstCall) {
        impl_->firstCall = false;
        return CpuReading{0.0f, false};
    }

    PDH_FMT_COUNTERVALUE value{};
    status = PdhGetFormattedCounterValue(impl_->counter, PDH_FMT_DOUBLE,
                                          nullptr, &value);
    if (status != ERROR_SUCCESS) {
        return CpuReading{0.0f, false};
    }

    // CStatus high bit set => the value is not trustworthy (e.g. negative delta
    // from a too-short interval). Treat as missing, never as zero.
    if ((value.CStatus & 0x80000000u) != 0u) {
        return CpuReading{0.0f, false};
    }

    // % Processor Time is [0,100]; normalise to [0,1] and clamp for safety.
    double pct = value.doubleValue;
    if (pct < 0.0)   pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    return CpuReading{static_cast<float>(pct / 100.0), true};
}

} // namespace prometheus::platform::windows
