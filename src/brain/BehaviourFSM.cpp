#include "prometheus/brain/BehaviourFSM.hpp"

namespace prometheus::brain {

bool BehaviourFSM::is_valid_scalar(float v) noexcept {
    return !std::isnan(v) && !std::isinf(v);
}

bool BehaviourFSM::is_in_range_01(float v) noexcept {
    return is_valid_scalar(v) && v >= 0.0f && v <= 1.0f;
}

bool BehaviourFSM::validate_config(const BehaviourFSMConfig& cfg) noexcept {
    // 1. All thresholds must be in [0.0f, 1.0f]
    const float* thresholds[] = {
        &cfg.health_dormant_enter,
        &cfg.health_dormant_exit,
        &cfg.energy_dormant_enter,
        &cfg.energy_dormant_exit,
        &cfg.thermal_headroom_exit,
        &cfg.critical_battery_threshold,
        &cfg.critical_battery_exit,
        &cfg.stress_enter_threshold,
        &cfg.stress_exit_threshold,
        &cfg.pressure_stress_enter,
        &cfg.pressure_stress_exit,
        &cfg.energy_rest_enter,
        &cfg.energy_rest_exit,
        &cfg.low_battery_threshold,
        &cfg.low_battery_energy_rest_boost,
        &cfg.min_energy_to_observe,
        &cfg.max_stress_to_observe,
        &cfg.max_pressure_to_observe,
        &cfg.curiosity_observe_trigger,
        &cfg.curiosity_observe_exit,
        &cfg.energy_explore_enter,
        &cfg.energy_explore_exit,
        &cfg.curiosity_explore_enter,
        &cfg.curiosity_explore_exit,
        &cfg.stress_explore_max,
        &cfg.stress_explore_exit,
        &cfg.pressure_explore_max,
        &cfg.pressure_explore_exit,
        &cfg.energy_explore_discharging,
        &cfg.pressure_explore_discharging,
        &cfg.energy_explore_unknown,
        &cfg.pressure_explore_unknown,
        &cfg.fallback_pressure
    };

    for (const float* ptr : thresholds) {
        if (!is_in_range_01(*ptr)) {
            return false;
        }
    }

    // 2. All dwell times must be finite and non-negative
    const float* dwell_times[] = {
        &cfg.min_dwell_dormant,
        &cfg.min_dwell_stressed,
        &cfg.min_dwell_resting,
        &cfg.min_dwell_observing,
        &cfg.min_dwell_exploring,
        &cfg.min_dwell_idle
    };

    for (const float* ptr : dwell_times) {
        if (!is_valid_scalar(*ptr) || *ptr < 0.0f) {
            return false;
        }
    }

    // 3. Hysteresis band relationships (strictly ordered enter vs exit)
    if (cfg.health_dormant_enter >= cfg.health_dormant_exit) return false;
    if (cfg.energy_dormant_enter >= cfg.energy_dormant_exit) return false;
    if (cfg.critical_battery_threshold >= cfg.critical_battery_exit) return false;
    if (cfg.stress_exit_threshold >= cfg.stress_enter_threshold) return false;
    if (cfg.pressure_stress_exit >= cfg.pressure_stress_enter) return false;
    if (cfg.energy_rest_enter >= cfg.energy_rest_exit) return false;
    if (cfg.energy_rest_enter > cfg.low_battery_energy_rest_boost) return false;
    if (cfg.low_battery_energy_rest_boost >= cfg.energy_rest_exit) return false;
    if (cfg.curiosity_observe_exit >= cfg.curiosity_observe_trigger) return false;
    if (cfg.energy_explore_exit >= cfg.energy_explore_enter) return false;
    if (cfg.curiosity_explore_exit >= cfg.curiosity_explore_enter) return false;
    if (cfg.stress_explore_max >= cfg.stress_explore_exit) return false;
    if (cfg.pressure_explore_max >= cfg.pressure_explore_exit) return false;

    // Power conservation thresholds
    if (cfg.energy_explore_enter > cfg.energy_explore_discharging) return false;
    if (cfg.pressure_explore_discharging > cfg.pressure_explore_max) return false;
    if (cfg.energy_explore_enter > cfg.energy_explore_unknown) return false;
    if (cfg.pressure_explore_unknown > cfg.pressure_explore_max) return false;

    return true;
}

BehaviourFSM::BehaviourFSM(const BehaviourFSMConfig& config) noexcept
    : config_(validate_config(config) ? config : BehaviourFSMConfig{}) {
}

bool BehaviourFSM::set_config(const BehaviourFSMConfig& config) noexcept {
    if (validate_config(config)) {
        config_ = config;
        return true;
    }
    return false;
}

float BehaviourFSM::get_min_dwell(common::Behaviour b) const noexcept {
    switch (b) {
        case common::Behaviour::Dormant:   return config_.min_dwell_dormant;
        case common::Behaviour::Stressed:  return config_.min_dwell_stressed;
        case common::Behaviour::Resting:   return config_.min_dwell_resting;
        case common::Behaviour::Observing: return config_.min_dwell_observing;
        case common::Behaviour::Exploring: return config_.min_dwell_exploring;
        case common::Behaviour::Idle:      return config_.min_dwell_idle;
        default:                           return config_.min_dwell_idle;
    }
}

bool BehaviourFSM::is_critical_battery(
    const std::optional<common::HostObservation::BatteryInfo>& battery
) const noexcept {
    if (!battery.has_value() || !battery->is_discharging) {
        return false;
    }
    return battery->is_low_battery || (battery->level <= config_.critical_battery_threshold);
}

bool BehaviourFSM::is_low_battery(
    const std::optional<common::HostObservation::BatteryInfo>& battery
) const noexcept {
    if (!battery.has_value() || !battery->is_discharging) {
        return false;
    }
    if (is_critical_battery(battery)) {
        return false;
    }
    return battery->level <= config_.low_battery_threshold;
}

bool BehaviourFSM::is_discharging(
    const std::optional<common::HostObservation::BatteryInfo>& battery
) const noexcept {
    return battery.has_value() && battery->is_discharging;
}

BehaviourDecision BehaviourFSM::evaluate(
    const common::OrganismState& current_state,
    const PressureResult& pressure_result,
    const common::HostObservation& observation,
    const ActiveEmergencies& active_emergencies
) const noexcept {
    // 1. Ingress Sanitization of state
    common::OrganismState clean_state = current_state;
    clean_state.energy = is_valid_scalar(clean_state.energy) ? std::clamp(clean_state.energy, 0.0f, 1.0f) : 1.0f;
    clean_state.health = is_valid_scalar(clean_state.health) ? std::clamp(clean_state.health, 0.0f, 1.0f) : 1.0f;
    clean_state.stress = is_valid_scalar(clean_state.stress) ? std::clamp(clean_state.stress, 0.0f, 1.0f) : 0.0f;
    clean_state.curiosity = is_valid_scalar(clean_state.curiosity) ? std::clamp(clean_state.curiosity, 0.0f, 1.0f) : 0.5f;
    clean_state.adaptation = is_valid_scalar(clean_state.adaptation) ? std::clamp(clean_state.adaptation, 0.0f, 1.0f) : 0.0f;
    if (!is_valid_scalar(clean_state.dwell_time_seconds) || clean_state.dwell_time_seconds < 0.0f) {
        clean_state.dwell_time_seconds = 0.0f;
    }
    if (static_cast<uint8_t>(clean_state.current_behaviour) > 5) {
        clean_state.current_behaviour = common::Behaviour::Idle;
    }

    // 2. Resolve Effective Pressure & Freshness
    const bool is_fresh = pressure_result.is_valid;
    float eff_pressure = config_.fallback_pressure;
    if (pressure_result.is_valid) {
        eff_pressure = pressure_result.has_smoothed_value ? pressure_result.smoothed_pressure : pressure_result.raw_pressure;
    } else if (pressure_result.has_smoothed_value) {
        eff_pressure = pressure_result.smoothed_pressure;
    }
    eff_pressure = is_valid_scalar(eff_pressure) ? std::clamp(eff_pressure, 0.0f, 1.0f) : config_.fallback_pressure;

    // 3. Emergency Evaluation & Clear-Then-Assert Deltas
    ActiveEmergencies detected{};
    ActiveEmergencies cleared{};

    // Thermal Throttling
    if (observation.thermal.has_value() && observation.thermal->is_throttling) {
        detected.thermal_throttling = true;
    } else if (active_emergencies.thermal_throttling) {
        // Positive confirmation required to clear
        if (observation.thermal.has_value() && 
            !observation.thermal->is_throttling && 
            observation.thermal->thermal_headroom >= config_.thermal_headroom_exit) {
            cleared.thermal_throttling = true;
        }
    }

    // Critical Battery
    if (is_critical_battery(observation.battery)) {
        detected.critical_battery = true;
    } else if (active_emergencies.critical_battery) {
        // Positive confirmation required to clear
        if (observation.battery.has_value()) {
            const bool not_discharging = !observation.battery->is_discharging;
            const bool level_recovered = !observation.battery->is_low_battery && 
                                         observation.battery->level >= config_.critical_battery_exit;
            if (not_discharging || level_recovered) {
                cleared.critical_battery = true;
            }
        }
    }

    // Biological Collapse
    if (clean_state.health <= config_.health_dormant_enter || clean_state.energy <= config_.energy_dormant_enter) {
        detected.biological_collapse = true;
    } else if (active_emergencies.biological_collapse) {
        if (clean_state.health >= config_.health_dormant_exit && clean_state.energy >= config_.energy_dormant_exit) {
            cleared.biological_collapse = true;
        }
    }

    // Compute effective active emergencies under Coordinator clear-then-assert order
    const bool thermal_active = (active_emergencies.thermal_throttling && !cleared.thermal_throttling) || detected.thermal_throttling;
    const bool battery_active = (active_emergencies.critical_battery && !cleared.critical_battery) || detected.critical_battery;
    const bool bio_active = (active_emergencies.biological_collapse && !cleared.biological_collapse) || detected.biological_collapse;
    const bool any_emergency_active = thermal_active || battery_active || bio_active;

    // Initialize base decision
    BehaviourDecision decision{};
    decision.previous_behaviour = clean_state.current_behaviour;
    decision.detected_emergencies = detected;
    decision.cleared_emergencies = cleared;

    // ------------------------------------------------------------------------
    // Priority 1: Dormant (Hardware Emergency / Biological Collapse)
    // ------------------------------------------------------------------------
    if (clean_state.current_behaviour == common::Behaviour::Dormant) {
        bool can_exit_dormant = !any_emergency_active &&
                                (clean_state.dwell_time_seconds >= config_.min_dwell_dormant) &&
                                (clean_state.health >= config_.health_dormant_exit) &&
                                (clean_state.energy >= config_.energy_dormant_exit);

        if (observation.thermal.has_value() && observation.thermal->thermal_headroom < config_.thermal_headroom_exit) {
            can_exit_dormant = false;
        }

        if (!can_exit_dormant) {
            decision.behaviour = common::Behaviour::Dormant;
            decision.transition_occurred = false;
            decision.reason = "Retain Dormant: Active emergency or cooldown pending";
            return decision;
        }
        // If can_exit_dormant is true, fall through to re-evaluate lower-priority states (staged recovery)
    } else {
        if (any_emergency_active) {
            decision.behaviour = common::Behaviour::Dormant;
            decision.transition_occurred = true; // Emergency bypasses dwell
            if (detected.thermal_throttling) {
                decision.reason = "Emergency Dormant: Thermal throttling";
            } else if (detected.critical_battery) {
                decision.reason = "Emergency Dormant: Critical battery";
            } else if (detected.biological_collapse) {
                decision.reason = "Emergency Dormant: Biological collapse";
            } else {
                decision.reason = "Emergency Dormant: Active emergency persisted";
            }
            return decision;
        }
    }

    // ------------------------------------------------------------------------
    // Priority 2: Stressed (Acute Panic Stress / Severe Fresh Pressure)
    // ------------------------------------------------------------------------
    if (clean_state.current_behaviour == common::Behaviour::Stressed) {
        const bool stress_cleared = clean_state.stress <= config_.stress_exit_threshold;
        // Stale pressure cannot hold organism in Stressed forever if internal stress has recovered
        const bool pressure_cleared = !is_fresh || (eff_pressure <= config_.pressure_stress_exit);
        const bool dwell_cleared = clean_state.dwell_time_seconds >= config_.min_dwell_stressed;

        if (!(stress_cleared && pressure_cleared && dwell_cleared)) {
            decision.behaviour = common::Behaviour::Stressed;
            decision.transition_occurred = false;
            decision.reason = "Retain Stressed: Defensive posture or dwell pending";
            return decision;
        }
        // Fall through to lower priority states if exited Stressed
    } else {
        const bool stress_enter = clean_state.stress >= config_.stress_enter_threshold;
        const bool pressure_enter = is_fresh && (eff_pressure >= config_.pressure_stress_enter);

        if (stress_enter || pressure_enter) {
            decision.behaviour = common::Behaviour::Stressed;
            decision.transition_occurred = true; // Emergency entry bypasses dwell
            decision.reason = stress_enter ? "Enter Stressed: Acute internal stress" : "Enter Stressed: Fresh environmental pressure spike";
            return decision;
        }
    }

    // ------------------------------------------------------------------------
    // Protective De-escalations from Exploring & Observing (Bypass Dwell)
    // ------------------------------------------------------------------------
    const bool low_batt = is_low_battery(observation.battery);
    const float rest_enter_threshold = low_batt ? config_.low_battery_energy_rest_boost : config_.energy_rest_enter;

    if (clean_state.current_behaviour == common::Behaviour::Exploring) {
        // 1. Unsafe environmental pressure (P >= 0.45 fresh): protective de-escalation to Resting
        if (is_fresh && eff_pressure >= config_.pressure_explore_exit) {
            decision.behaviour = common::Behaviour::Resting;
            decision.transition_occurred = true; // Protective de-escalation bypasses dwell
            decision.reason = "Protective De-escalation: Unsafe environmental pressure in Exploring";
            return decision;
        }

        // 2. Low energy: protective de-escalation to Resting
        if (clean_state.energy <= rest_enter_threshold) {
            decision.behaviour = common::Behaviour::Resting;
            decision.transition_occurred = true;
            decision.reason = "Protective De-escalation: Low energy in Exploring";
            return decision;
        }

        // 3. Low battery: protective de-escalation to Resting
        if (low_batt) {
            decision.behaviour = common::Behaviour::Resting;
            decision.transition_occurred = true;
            decision.reason = "Protective De-escalation: Low battery in Exploring";
            return decision;
        }

        // 4. Stale telemetry: cannot safely continue Exploring
        if (!is_fresh) {
            decision.behaviour = common::Behaviour::Idle;
            decision.transition_occurred = true;
            decision.reason = "Protective De-escalation: Telemetry stale in Exploring";
            return decision;
        }

        // 5. Exploration drives subsided (curiosity decay, stress rise, energy below explore exit)
        if (clean_state.curiosity <= config_.curiosity_explore_exit ||
            clean_state.stress >= config_.stress_explore_exit ||
            clean_state.energy <= config_.energy_explore_exit) {
            decision.behaviour = common::Behaviour::Idle;
            decision.transition_occurred = true;
            decision.reason = "De-escalation: Exploring drive subsided";
            return decision;
        }

        // 6. User interaction preempts Exploring
        if (observation.is_user_active) {
            decision.behaviour = common::Behaviour::Observing;
            decision.transition_occurred = true;
            decision.reason = "De-escalation: User interaction preempts Exploring";
            return decision;
        }
    }

    if (clean_state.current_behaviour == common::Behaviour::Observing) {
        // Low energy in Observing causes protective de-escalation to Resting
        if (clean_state.energy <= rest_enter_threshold) {
            decision.behaviour = common::Behaviour::Resting;
            decision.transition_occurred = true; // Protective de-escalation bypasses dwell
            decision.reason = "Protective De-escalation: Low energy in Observing";
            return decision;
        }
    }

    // ------------------------------------------------------------------------
    // Dwell Lock for Voluntary Escalation
    // ------------------------------------------------------------------------
    if (clean_state.dwell_time_seconds < get_min_dwell(clean_state.current_behaviour)) {
        decision.behaviour = clean_state.current_behaviour;
        decision.transition_occurred = false;
        decision.reason = "Dwell Lock: Minimum dwell time not yet elapsed";
        return decision;
    }

    // ------------------------------------------------------------------------
    // Priority 3: Resting (Energy Replenishment)
    // ------------------------------------------------------------------------
    if (clean_state.current_behaviour == common::Behaviour::Resting) {
        if (clean_state.energy < config_.energy_rest_exit) {
            decision.behaviour = common::Behaviour::Resting;
            decision.transition_occurred = false;
            decision.reason = "Retain Resting: Replenishing energy reserves";
            return decision;
        }
        // Exited Resting, fall through
    } else {
        if (clean_state.energy <= rest_enter_threshold) {
            decision.behaviour = common::Behaviour::Resting;
            decision.transition_occurred = true;
            decision.reason = low_batt ? "Enter Resting: Low battery power conservation" : "Enter Resting: Energy below replenishment threshold";
            return decision;
        }
    }

    // ------------------------------------------------------------------------
    // Priority 4: Observing (Social / Curiosity Orientation)
    // ------------------------------------------------------------------------
    const bool obs_energy_ok = clean_state.energy >= config_.min_energy_to_observe;
    const bool obs_stress_ok = clean_state.stress <= config_.max_stress_to_observe;
    const bool obs_pressure_ok = eff_pressure <= config_.max_pressure_to_observe;

    if (clean_state.current_behaviour == common::Behaviour::Observing) {
        const bool retain_trigger = observation.is_user_active || (clean_state.curiosity > config_.curiosity_observe_exit);
        if (retain_trigger && obs_energy_ok && obs_stress_ok) {
            decision.behaviour = common::Behaviour::Observing;
            decision.transition_occurred = false;
            decision.reason = "Retain Observing: Active engagement or sustained curiosity";
            return decision;
        }
        // Exited Observing, fall through
    } else {
        const bool enter_trigger = observation.is_user_active || (clean_state.curiosity >= config_.curiosity_observe_trigger);
        if (enter_trigger && obs_energy_ok && obs_stress_ok && obs_pressure_ok) {
            decision.behaviour = common::Behaviour::Observing;
            decision.transition_occurred = true;
            decision.reason = observation.is_user_active ? "Enter Observing: User interaction active" : "Enter Observing: Spontaneous curiosity spike";
            return decision;
        }
    }

    // ------------------------------------------------------------------------
    // Priority 5: Exploring (High Vitality Play)
    // ------------------------------------------------------------------------
    float explore_e_enter = config_.energy_explore_enter;
    float explore_p_enter = config_.pressure_explore_max;

    if (observation.battery.has_value() && observation.battery->is_discharging) {
        explore_e_enter = config_.energy_explore_discharging;
        explore_p_enter = config_.pressure_explore_discharging;
    } else if (!observation.battery.has_value()) {
        // Unknown battery: conservative tightened thresholds
        explore_e_enter = config_.energy_explore_unknown;
        explore_p_enter = config_.pressure_explore_unknown;
    } else {
        // Present but not discharging (e.g. wall power)
        explore_e_enter = config_.energy_explore_enter;
        explore_p_enter = config_.pressure_explore_max;
    }

    if (clean_state.current_behaviour == common::Behaviour::Exploring) {
        const bool retain_ok = is_fresh &&
                               !low_batt &&
                               !observation.is_user_active &&
                               (clean_state.energy > config_.energy_explore_exit) &&
                               (clean_state.curiosity > config_.curiosity_explore_exit) &&
                               (clean_state.stress < config_.stress_explore_exit) &&
                               (eff_pressure < config_.pressure_explore_exit);
        if (retain_ok) {
            decision.behaviour = common::Behaviour::Exploring;
            decision.transition_occurred = false;
            decision.reason = "Retain Exploring: Vitality and calm environment sustained";
            return decision;
        }
    } else {
        const bool enter_ok = is_fresh &&
                              !low_batt &&
                              !observation.is_user_active &&
                              (clean_state.current_behaviour != common::Behaviour::Dormant) &&
                              (clean_state.energy >= explore_e_enter) &&
                              (clean_state.curiosity >= config_.curiosity_explore_enter) &&
                              (clean_state.stress <= config_.stress_explore_max) &&
                              (eff_pressure <= explore_p_enter);
        if (enter_ok) {
            decision.behaviour = common::Behaviour::Exploring;
            decision.transition_occurred = true;
            decision.reason = "Enter Exploring: High vitality and peaceful host environment";
            return decision;
        }
    }

    // ------------------------------------------------------------------------
    // Priority 6: Idle (Baseline Homeostatic Equilibrium Attractor)
    // ------------------------------------------------------------------------
    decision.behaviour = common::Behaviour::Idle;
    decision.transition_occurred = (clean_state.current_behaviour != common::Behaviour::Idle);
    decision.reason = (clean_state.current_behaviour == common::Behaviour::Idle) ? 
                      "Retain Idle: Baseline homeostatic equilibrium" : 
                      "Enter Idle: Default homeostatic attractor";
    return decision;
}

} // namespace prometheus::brain
