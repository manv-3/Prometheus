#pragma once

#include "prometheus/sensory/SensoryProvider.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>

namespace prometheus::sensory {

/**
 * @brief Scripted SensoryProvider for deterministic tests and demos (TASK-1.3).
 *
 * Replays a named, pre-defined pressure curve as a sequence of HostObservation
 * frames. When the sequence is exhausted, advance() wraps around so the curve
 * loops indefinitely.
 *
 * Available curves:
 *   steady-low, ramp-up, spike, plateau-high, ramp-down,
 *   noisy-around-threshold, stale, dropout
 *
 * Every frame has valid CPU and memory data except:
 *   - `stale`     : drops to cpu_valid=memory_valid=false after N frames
 *   - `dropout`   : intermittently marks frames invalid
 *
 * Each frame's timestamp is steady_clock::now() + index*1s, so the sequence is
 * monotonic and ~1 Hz-aligned for the default SensorWorker interval.
 *
 * NOTE: HostObservation has no TelemetrySource field yet; synthetic data is
 * distinguished by construction (it flows through SyntheticProvider), not by a
 * per-frame tag. Do not add such a field without Dev B's contract approval.
 */
class SyntheticProvider final : public SensoryProvider {
public:
    /// Build a provider that replays the named curve (default "steady-low").
    explicit SyntheticProvider(std::string curveName = "steady-low");

    [[nodiscard]] common::HostObservation latest() const override;

    /// Advance to the next frame (wraps around at the end). Called by SensorWorker.
    void advance() noexcept;

    [[nodiscard]] std::size_t frameIndex() const noexcept { return index_; }

    /// Build a named pressure curve as a vector of frames (>=60 frames, ~1 Hz).
    /// Exposed so tests can inspect individual frames without a running worker.
    [[nodiscard]] static std::vector<common::HostObservation> buildCurve(std::string_view curveName);

private:
    std::vector<common::HostObservation> sequence_;
    std::size_t index_{0};
};

} // namespace prometheus::sensory
