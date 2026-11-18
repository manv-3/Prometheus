#pragma once

#include "prometheus/sensory/SensoryProvider.hpp"
#include <vector>
#include <cstddef>

namespace prometheus::sensory {

/**
 * @brief Scripted SensoryProvider for deterministic tests and demos.
 *
 * Replays a pre-defined sequence of HostObservations. When the sequence
 * is exhausted it loops. Every observation is labelled
 * TelemetrySource::Synthetic so it can never be mistaken for real data
 * (REQ-SENSE-03, REQ-UI-02).
 *
 * STATUS: STUB — owned by Dev A (TASK-1.3). This header lets Dev C compile
 * the app target while Dev A implements the real version.
 */
class SyntheticProvider final : public SensoryProvider {
public:
    /// Build a provider that replays the given sequence.
    explicit SyntheticProvider(std::vector<common::HostObservation> sequence = {});

    [[nodiscard]] common::HostObservation latest() const override;

    /// Advance to the next frame in the sequence (called by SensorWorker).
    void advance() noexcept;

    [[nodiscard]] std::size_t frameIndex() const noexcept { return index_; }

private:
    std::vector<common::HostObservation> sequence_;
    std::size_t index_{0};
};

} // namespace prometheus::sensory
