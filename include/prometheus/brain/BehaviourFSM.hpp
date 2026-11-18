#pragma once

#include "prometheus/common/OrganismState.hpp"
#include "prometheus/common/HostObservation.hpp"
#include "prometheus/brain/PressureEstimator.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace prometheus::brain {

/**
 * @brief PROPOSED: Active hardware and existential emergency flags tracked across frames.
 * Owned and updated by Developer C's Runtime Coordinator.
 * Evaluated as read-only context by BehaviourFSM::evaluate().
 */
struct ActiveEmergencies {
    bool thermal_throttling{false};
    bool critical_battery{false};
    bool biological_collapse{false};

    [[nodiscard]] constexpr bool has_any() const noexcept {
        return thermal_throttling || critical_battery || biological_collapse;
    }
};

/**
 * @brief Configurable parameters governing behavioural selection, hysteresis bands, and dwell timers.
 */
struct BehaviourFSMConfig {
    // Priority 1: Dormant Thresholds & Hardware Protection
    float health_dormant_enter{0.10f};          // H <= enter -> Dormant
    float health_dormant_exit{0.25f};           // H >= exit  -> Can exit Dormant
    float energy_dormant_enter{0.05f};          // E <= enter -> Dormant
    float energy_dormant_exit{0.20f};           // E >= exit  -> Can exit Dormant
    float thermal_headroom_exit{0.30f};         // Required thermal headroom to exit Dormant
    float critical_battery_threshold{0.05f};    // Discharging level <= 0.05 -> Dormant
    float critical_battery_exit{0.15f};         // Level >= 0.15 required to exit Dormant

    // Priority 2: Stressed Hysteresis
    float stress_enter_threshold{0.70f};        // S >= enter -> Stressed
    float stress_exit_threshold{0.45f};         // S <= exit  -> Can exit Stressed
    float pressure_stress_enter{0.75f};         // Fresh P >= enter -> Stressed
    float pressure_stress_exit{0.50f};          // P <= exit  -> Can exit Stressed

    // Priority 3: Resting Hysteresis
    float energy_rest_enter{0.20f};             // E <= enter -> Resting
    float energy_rest_exit{0.50f};              // E >= exit  -> Can exit Resting
    float low_battery_threshold{0.20f};         // Discharging level <= 0.20 is low battery
    float low_battery_energy_rest_boost{0.35f}; // Enters rest at E <= 0.35 under low battery

    // Priority 4: Observing Parameters
    float min_energy_to_observe{0.20f};         // Minimum energy to observe
    float max_stress_to_observe{0.65f};         // Maximum stress permitting observation
    float max_pressure_to_observe{0.60f};       // Maximum pressure permitting observation
    float curiosity_observe_trigger{0.75f};     // Spontaneous curiosity trigger
    float curiosity_observe_exit{0.50f};        // Curiosity exit threshold

    // Priority 5: Exploring Hysteresis & Power Constraints
    float energy_explore_enter{0.65f};          // E >= enter -> Exploring (Present not discharging)
    float energy_explore_exit{0.45f};           // E <= exit  -> Exit Exploring
    float curiosity_explore_enter{0.60f};       // C >= enter -> Exploring
    float curiosity_explore_exit{0.35f};        // C <= exit  -> Exit Exploring
    float stress_explore_max{0.25f};            // S <= max   -> Enter Exploring
    float stress_explore_exit{0.40f};           // S >= exit  -> Exit Exploring
    float pressure_explore_max{0.30f};          // P <= max   -> Enter Exploring (Present not discharging)
    float pressure_explore_exit{0.45f};         // P >= exit  -> Exit Exploring
    float energy_explore_discharging{0.75f};    // Tighter E required on discharging battery
    float pressure_explore_discharging{0.20f};  // Tighter P required on discharging battery
    float energy_explore_unknown{0.75f};        // Conservative tightened E for Unknown battery
    float pressure_explore_unknown{0.20f};      // Conservative tightened P for Unknown battery

    // Minimum Dwell Times per State (seconds)
    float min_dwell_dormant{5.0f};              // Minimum cooldown from emergency
    float min_dwell_stressed{2.0f};             // Defensive posture inertia
    float min_dwell_resting{3.0f};              // Metabolic replenishment inertia
    float min_dwell_observing{2.0f};            // Attention focus inertia
    float min_dwell_exploring{3.0f};            // Playful simulation inertia
    float min_dwell_idle{1.0f};                 // Baseline equilibrium inertia

    // Sensor Fallbacks
    float fallback_pressure{0.50f};             // Assumed P when no sensor is available
};

/**
 * @brief Result struct returned by BehaviourFSM::evaluate().
 */
struct BehaviourDecision {
    common::Behaviour behaviour{common::Behaviour::Idle};
    bool transition_occurred{false};
    common::Behaviour previous_behaviour{common::Behaviour::Idle};
    const char* reason{"Default"};

    // Emergency context deltas for the Runtime Coordinator
    ActiveEmergencies detected_emergencies{};
    ActiveEmergencies cleared_emergencies{};
};

/**
 * @brief Deterministic Behaviour Finite State Machine (FSM).
 * 
 * Evaluates high-level behavioural transitions according to a strict 6-tier priority cascade:
 *   Dormant > Stressed > Resting > Observing > Exploring > Idle
 * 
 * Safety & Design Invariants:
 * 1. Pure Function: evaluate() does NOT mutate OrganismState, dwell_time_seconds, or ActiveEmergencies.
 * 2. Coordinator Ownership: Developer C's Runtime Coordinator is the exclusive owner of dwell time
 *    accumulation and ActiveEmergencies mutation.
 * 3. Clear-Then-Assert: Detected emergencies in the current update override clearance signals.
 * 4. Asymmetric Dwell Bypass: Emergency transitions (P1, P2) and protective de-escalations
 *    (e.g. Exploring -> Resting on low energy/battery/unsafe pressure) bypass dwell locks.
 * 5. Conservative Power Policy: Missing battery telemetry (Unknown) requires tightened thresholds.
 */
class BehaviourFSM {
public:
    explicit BehaviourFSM(const BehaviourFSMConfig& config = BehaviourFSMConfig{}) noexcept;

    /**
     * @brief Pure evaluation of behavioural state transitions.
     * Does NOT accept dt. Does NOT modify dwell_time_seconds.
     */
    [[nodiscard]] BehaviourDecision evaluate(
        const common::OrganismState& current_state,
        const PressureResult& pressure_result,
        const common::HostObservation& observation,
        const ActiveEmergencies& active_emergencies
    ) const noexcept;

    bool set_config(const BehaviourFSMConfig& config) noexcept;
    [[nodiscard]] const BehaviourFSMConfig& config() const noexcept { return config_; }

    [[nodiscard]] static bool validate_config(const BehaviourFSMConfig& config) noexcept;

private:
    [[nodiscard]] float get_min_dwell(common::Behaviour b) const noexcept;

    [[nodiscard]] bool is_critical_battery(
        const std::optional<common::HostObservation::BatteryInfo>& battery
    ) const noexcept;

    [[nodiscard]] bool is_low_battery(
        const std::optional<common::HostObservation::BatteryInfo>& battery
    ) const noexcept;

    [[nodiscard]] bool is_discharging(
        const std::optional<common::HostObservation::BatteryInfo>& battery
    ) const noexcept;

    [[nodiscard]] static bool is_valid_scalar(float v) noexcept;
    [[nodiscard]] static bool is_in_range_01(float v) noexcept;

    BehaviourFSMConfig config_;
};

} // namespace prometheus::brain
