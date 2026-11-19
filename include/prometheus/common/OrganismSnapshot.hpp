#pragma once

#include "HostObservation.hpp"
#include "OrganismState.hpp"
#include "ComputeBudget.hpp"
#include <cstdint>

namespace prometheus::common {

/// Identifies the telemetry source for display and logging (REQ-UI-02).
enum class TelemetrySource : uint8_t { Real, Synthetic };

/**
 * @brief PROVISIONAL TEAM CONTRACT (Shared across Dev A, Dev B, Dev C).
 *
 * Immutable snapshot of the organism and host at a single point in time.
 * Consumed by Developer C (UI/Rendering) and Developer A (Memory/Persistence).
 *
 * Published once per simulation tick by the simulation thread.
 * Never mutated after publication.
 *
 * STATUS: PROVISIONAL — kContractVersion bumped to 2 for telemetry_source addition.
 */
struct OrganismSnapshot {
    HostObservation observation;
    OrganismState   state;
    ComputeBudget   budget;

    float environmental_pressure{0.0f};  // smoothed pressure [0, 1]
    float sim_time_seconds{0.0f};        // total elapsed simulation time

    uint32_t active_particles{0};        // current particle pool active count
    float    measured_sim_hz{0.0f};      // actual simulation tick rate this period

    TelemetrySource telemetry_source{TelemetrySource::Real}; // REQ-UI-02
    bool dormant{false};                 // true when organism is in Dormant behaviour
};

/// Bump when any breaking change is made to this struct.
static constexpr int kContractVersion = 2;

} // namespace prometheus::common
