#include "prometheus/sensory/SyntheticProvider.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prometheus::sensory {

namespace {

using common::HostObservation;
using Clock = std::chrono::steady_clock;

// Curve length: >=60 frames so a 1 Hz replay covers at least 60 seconds.
constexpr std::size_t kFrameCount = 64;

// Fixed seeds so the noisy/dropout curves are reproducible (deterministic
// baseline — AGENTCONTEXT #6; randomness here is seeded, never hidden state).
constexpr unsigned kNoisySeed  = 0x5EEDu;
constexpr unsigned kDropoutSeed = 0xC0FFEEu;

float clamp01(float v) noexcept {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

Clock::time_point tsAt(const Clock::time_point base, std::size_t i) {
    return base + std::chrono::seconds(i);
}

// Build one frame. cpu/mem are [0,1]; validity flags default true.
HostObservation makeFrame(Clock::time_point timestamp,
                          float cpuLoad,
                          float memPressure,
                          bool cpuValid = true,
                          bool memValid = true) noexcept {
    HostObservation o;
    o.timestamp        = timestamp;
    o.cpu_load          = cpuLoad;
    o.cpu_valid         = cpuValid;
    o.memory_pressure   = memPressure;
    o.memory_valid      = memValid;
    o.is_user_active    = true;
    return o;
}

// --- Named pressure curves -------------------------------------------------

std::vector<HostObservation> steadyLow() {
    const auto base = Clock::now();
    std::vector<HostObservation> v;
    v.reserve(kFrameCount);
    for (std::size_t i = 0; i < kFrameCount; ++i) {
        v.push_back(makeFrame(tsAt(base, i), 0.15f, 0.30f));
    }
    return v;
}

std::vector<HostObservation> rampUp() {
    const auto base = Clock::now();
    std::vector<HostObservation> v;
    v.reserve(kFrameCount);
    for (std::size_t i = 0; i < kFrameCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kFrameCount - 1);
        const float cpu = 0.15f + t * (0.90f - 0.15f);
        const float mem = 0.25f + t * (0.85f - 0.25f);
        v.push_back(makeFrame(tsAt(base, i), cpu, mem));
    }
    return v;
}

std::vector<HostObservation> spike() {
    const auto base = Clock::now();
    std::vector<HostObservation> v;
    v.reserve(kFrameCount);
    const std::size_t spikeIdx = kFrameCount / 2;
    for (std::size_t i = 0; i < kFrameCount; ++i) {
        if (i == spikeIdx) {
            v.push_back(makeFrame(tsAt(base, i), 0.95f, 0.90f));
        } else {
            v.push_back(makeFrame(tsAt(base, i), 0.18f, 0.30f));
        }
    }
    return v;
}

std::vector<HostObservation> plateauHigh() {
    const auto base = Clock::now();
    std::vector<HostObservation> v;
    v.reserve(kFrameCount);
    for (std::size_t i = 0; i < kFrameCount; ++i) {
        // Brief low headroom, then sustained high pressure.
        const float cpu = (i < 8) ? 0.15f : 0.85f;
        const float mem = (i < 8) ? 0.30f : 0.80f;
        v.push_back(makeFrame(tsAt(base, i), cpu, mem));
    }
    return v;
}

std::vector<HostObservation> rampDown() {
    const auto base = Clock::now();
    std::vector<HostObservation> v;
    v.reserve(kFrameCount);
    for (std::size_t i = 0; i < kFrameCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kFrameCount - 1);
        const float cpu = 0.90f - t * (0.90f - 0.15f);
        const float mem = 0.85f - t * (0.85f - 0.25f);
        v.push_back(makeFrame(tsAt(base, i), cpu, mem));
    }
    return v;
}

std::vector<HostObservation> noisyAroundThreshold() {
    const auto base = Clock::now();
    std::vector<HostObservation> v;
    v.reserve(kFrameCount);
    std::mt19937 rng(kNoisySeed);
    std::uniform_real_distribution<float> noise(-0.12f, 0.12f);
    for (std::size_t i = 0; i < kFrameCount; ++i) {
        // Slow sinusoid around the ~0.5 pressure threshold plus seeded noise,
        // so the organism repeatedly crosses Explore/Conserve boundaries.
        const float cpuBase = 0.50f + 0.18f * std::sin(static_cast<float>(i) * 0.40f);
        const float memBase = 0.50f + 0.10f * std::sin(static_cast<float>(i) * 0.30f);
        const float cpu = clamp01(cpuBase + noise(rng));
        const float mem = clamp01(memBase + noise(rng));
        v.push_back(makeFrame(tsAt(base, i), cpu, mem));
    }
    return v;
}

std::vector<HostObservation> stale() {
    const auto base = Clock::now();
    std::vector<HostObservation> v;
    v.reserve(kFrameCount);
    // After this many frames the host stops reporting; both metrics go invalid.
    constexpr std::size_t kStaleAfter = 12;
    for (std::size_t i = 0; i < kFrameCount; ++i) {
        const bool valid = i < kStaleAfter;
        const float cpu = clamp01(0.20f + 0.40f * static_cast<float>(i) / static_cast<float>(kFrameCount));
        const float mem = clamp01(0.30f + 0.30f * static_cast<float>(i) / static_cast<float>(kFrameCount));
        v.push_back(makeFrame(tsAt(base, i), cpu, mem, valid, valid));
    }
    return v;
}

std::vector<HostObservation> dropout() {
    const auto base = Clock::now();
    std::vector<HostObservation> v;
    v.reserve(kFrameCount);
    std::mt19937 rng(kDropoutSeed);
    std::uniform_real_distribution<float> draw(0.0f, 1.0f);
    for (std::size_t i = 0; i < kFrameCount; ++i) {
        // ~25% of frames intermittently lose both metrics.
        const bool valid = draw(rng) >= 0.25f;
        const float cpu = clamp01(0.30f + 0.50f * static_cast<float>(i) / static_cast<float>(kFrameCount));
        const float mem = clamp01(0.35f + 0.40f * static_cast<float>(i) / static_cast<float>(kFrameCount));
        v.push_back(makeFrame(tsAt(base, i), cpu, mem, valid, valid));
    }
    return v;
}

std::vector<HostObservation> dispatch(std::string_view name) {
    if (name == "steady-low")             return steadyLow();
    if (name == "ramp-up")                 return rampUp();
    if (name == "spike")                   return spike();
    if (name == "plateau-high")            return plateauHigh();
    if (name == "ramp-down")               return rampDown();
    if (name == "noisy-around-threshold")  return noisyAroundThreshold();
    if (name == "stale")                   return stale();
    if (name == "dropout")                 return dropout();
    // Unknown curve name: fall back to the safe low baseline rather than error.
    return steadyLow();
}

} // anonymous namespace

// ---- SyntheticProvider -----------------------------------------------------

std::vector<common::HostObservation>
SyntheticProvider::buildCurve(std::string_view curveName) {
    return dispatch(curveName);
}

SyntheticProvider::SyntheticProvider(std::string curveName)
    : sequence_(buildCurve(curveName)) {}

common::HostObservation SyntheticProvider::latest() const {
    if (sequence_.empty()) {
        return common::HostObservation{};
    }
    return sequence_[index_ % sequence_.size()];
}

void SyntheticProvider::advance() noexcept {
    if (!sequence_.empty()) {
        index_ = (index_ + 1) % sequence_.size();
    }
}

} // namespace prometheus::sensory
