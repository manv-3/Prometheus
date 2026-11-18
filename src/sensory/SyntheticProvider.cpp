#include "prometheus/sensory/SyntheticProvider.hpp"
#include <chrono>

namespace prometheus::sensory {

// STUB — Phase 0 skeleton.
// Dev A owns the full implementation (TASK-1.3).
// This stub returns a single safe default observation so the app target links
// and the simulation thread has something to feed Brain::update with.

SyntheticProvider::SyntheticProvider(std::vector<common::HostObservation> sequence)
    : sequence_(std::move(sequence))
{
    // If no sequence provided, seed with one safe default frame
    if (sequence_.empty()) {
        common::HostObservation defaultObs;
        defaultObs.cpu_load = 0.10f;
        defaultObs.cpu_valid = true;
        defaultObs.memory_pressure = 0.20f;
        defaultObs.memory_valid = true;
        // source field: TODO set TelemetrySource::Synthetic once Dev A adds the field
        sequence_.push_back(defaultObs);
    }
}

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
