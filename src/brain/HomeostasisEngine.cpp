#include "prometheus/brain/HomeostasisEngine.hpp"

namespace prometheus::brain {

bool HomeostasisEngine::is_valid_scalar(float v) noexcept {
    return !std::isnan(v) && !std::isinf(v);
}

bool HomeostasisEngine::is_in_range_01(float v) noexcept {
    return is_valid_scalar(v) && (v >= 0.0f) && (v <= 1.0f);
}

bool HomeostasisEngine::validate_config(const HomeostasisConfig& cfg) noexcept {
    auto is_non_neg = [](float v) noexcept {
        return is_valid_scalar(v) && (v >= 0.0f);
    };

    // 1. All rate parameters must be finite and non-negative
    if (!is_non_neg(cfg.basal_energy_burn_rate) ||
        !is_non_neg(cfg.energy_pressure_burn_factor) ||
        !is_non_neg(cfg.energy_rest_gain_rate) ||
        !is_non_neg(cfg.stress_rise_rate) ||
        !is_non_neg(cfg.stress_decay_rate) ||
        !is_non_neg(cfg.health_starve_damage_rate) ||
        !is_non_neg(cfg.health_stress_damage_rate) ||
        !is_non_neg(cfg.health_heal_rate) ||
        !is_non_neg(cfg.curiosity_relax_rate) ||
        !is_non_neg(cfg.adaptation_rate)) {
        return false;
    }

    // 2. Denominator safeguards: Divisors must be strictly positive with non-zero margins
    if (cfg.low_energy_threshold <= 0.001f || cfg.low_energy_threshold > 1.0f ||
        cfg.critical_energy_threshold <= 0.001f || cfg.critical_energy_threshold > 1.0f) {
        return false;
    }

    // 3. Threshold ordering and range consistency
    if (cfg.critical_energy_threshold > cfg.low_energy_threshold) {
        return false; // Starvation damage cannot start before starvation stress
    }

    // heal_energy_threshold must leave a non-zero denominator: (1.0f - heal_energy_threshold) >= 0.001f
    if (cfg.heal_energy_threshold <= cfg.critical_energy_threshold || cfg.heal_energy_threshold >= 0.999f) {
        return false;
    }

    // heal_stress_threshold must be strictly positive: denominator in s_factor >= 0.001f
    if (cfg.heal_stress_threshold <= 0.001f || cfg.heal_stress_threshold >= cfg.critical_stress_threshold) {
        return false;
    }

    // critical_stress_threshold must leave a non-zero denominator: (1.0f - critical_stress_threshold) >= 0.001f
    if (cfg.critical_stress_threshold >= 0.999f || !is_in_range_01(cfg.critical_stress_threshold)) {
        return false;
    }

    if (!is_in_range_01(cfg.starvation_stress_weight) ||
        !is_in_range_01(cfg.curiosity_nominal_target) ||
        !is_in_range_01(cfg.user_activity_boost) ||
        !is_in_range_01(cfg.adaptation_stress_threshold) ||
        !is_in_range_01(cfg.adaptation_min_health) ||
        !is_in_range_01(cfg.adaptation_mitigation_factor) ||
        !is_in_range_01(cfg.fallback_pressure)) {
        return false;
    }

    // Adaptation stress and health thresholds must allow room for accumulation (< 0.999f)
    if (cfg.adaptation_stress_threshold >= 0.999f || cfg.adaptation_min_health >= 0.999f) {
        return false;
    }

    // 4. Sub-step timing validation
    if (cfg.max_substep_seconds <= 0.001f || !is_valid_scalar(cfg.max_substep_seconds) ||
        cfg.max_total_dt_seconds < cfg.max_substep_seconds || !is_valid_scalar(cfg.max_total_dt_seconds)) {
        return false;
    }

    // 50-step invariant: max_total_dt_seconds <= 50 * max_substep_seconds
    // Guarantees dt_sub never exceeds max_substep_seconds under the 50-step clamp
    if (cfg.max_total_dt_seconds > (50.0f * cfg.max_substep_seconds)) {
        return false;
    }

    return true;
}

HomeostasisEngine::HomeostasisEngine(const HomeostasisConfig& config) noexcept {
    if (validate_config(config)) {
        config_ = config;
    } else {
        config_ = HomeostasisConfig{};
    }
}

bool HomeostasisEngine::set_config(const HomeostasisConfig& config) noexcept {
    if (!validate_config(config)) {
        return false;
    }
    config_ = config;
    return true;
}

common::OrganismState HomeostasisEngine::sanitize_incoming_state(const common::OrganismState& state) const noexcept {
    common::OrganismState clean = state;

    // Energy: non-finite falls back to conservative 0.0f (depleted, no free energy)
    if (!is_valid_scalar(clean.energy)) {
        clean.energy = 0.0f;
    } else {
        clean.energy = std::clamp(clean.energy, 0.0f, 1.0f);
    }

    // Health: non-finite falls back to conservative 0.0f (critical, no free health)
    if (!is_valid_scalar(clean.health)) {
        clean.health = 0.0f;
    } else {
        clean.health = std::clamp(clean.health, 0.0f, 1.0f);
    }

    // Stress: non-finite falls back to conservative 1.0f (maximum alarm)
    if (!is_valid_scalar(clean.stress)) {
        clean.stress = 1.0f;
    } else {
        clean.stress = std::clamp(clean.stress, 0.0f, 1.0f);
    }

    // Curiosity: non-finite falls back to 0.0f
    if (!is_valid_scalar(clean.curiosity)) {
        clean.curiosity = 0.0f;
    } else {
        clean.curiosity = std::clamp(clean.curiosity, 0.0f, 1.0f);
    }

    // Adaptation: non-finite falls back to 0.0f
    if (!is_valid_scalar(clean.adaptation)) {
        clean.adaptation = 0.0f;
    } else {
        clean.adaptation = std::clamp(clean.adaptation, 0.0f, 1.0f);
    }

    // Dwell time: non-finite or negative falls back to 0.0f; bounded to prevent float overflow
    constexpr float MAX_DWELL_SECONDS = 315360000.0f; // ~10 years
    if (!is_valid_scalar(clean.dwell_time_seconds) || clean.dwell_time_seconds < 0.0f) {
        clean.dwell_time_seconds = 0.0f;
    } else if (clean.dwell_time_seconds > MAX_DWELL_SECONDS) {
        clean.dwell_time_seconds = MAX_DWELL_SECONDS;
    }

    return clean;
}

HomeostasisEngine::ResolvedPressure HomeostasisEngine::resolve_pressure(const PressureResult& res) const noexcept {
    ResolvedPressure out{};
    out.value = config_.fallback_pressure;
    out.is_fresh = false;

    // Fresh telemetry branch
    if (res.is_valid) {
        const float candidate = res.has_smoothed_value ? res.smoothed_pressure : res.raw_pressure;
        if (is_valid_scalar(candidate)) {
            out.value = std::clamp(candidate, 0.0f, 1.0f);
            out.is_fresh = true;
            return out;
        }
        // If candidate scalar is non-finite, fall through to fallback
    }

    // Stale smoothed telemetry branch (for conservative metabolism, not fresh)
    if (res.has_smoothed_value) {
        if (is_valid_scalar(res.smoothed_pressure)) {
            out.value = std::clamp(res.smoothed_pressure, 0.0f, 1.0f);
            out.is_fresh = false;
            return out;
        }
    }

    // Fallback branch
    out.value = is_valid_scalar(config_.fallback_pressure) ?
                std::clamp(config_.fallback_pressure, 0.0f, 1.0f) : 0.50f;
    out.is_fresh = false;
    return out;
}

common::OrganismState HomeostasisEngine::single_substep(
    const common::OrganismState& state,
    float resolved_pressure,
    bool is_fresh_telemetry,
    float dt,
    bool is_user_active
) const noexcept {
    common::OrganismState next = state;

    // Dwell time and behaviour are preserved untouched (Runtime Coordinator is sole owner of dwell timer)
    next.dwell_time_seconds = state.dwell_time_seconds;
    next.current_behaviour = state.current_behaviour;

    // 1. Effective Pressure with Adaptation Dampening
    const float eff_pressure = std::clamp(
        resolved_pressure * (1.0f - config_.adaptation_mitigation_factor * state.adaptation),
        0.0f, 1.0f
    );

    // 2. Stress Dynamics: Unified target with decoupled starvation drive
    const float starve_drive = std::clamp(
        (config_.low_energy_threshold - state.energy) / config_.low_energy_threshold,
        0.0f, 1.0f
    );

    const float s_target = std::clamp(
        eff_pressure + (config_.starvation_stress_weight * starve_drive),
        0.0f, 1.0f
    );

    float k_relax = 0.0f;
    if (s_target > state.stress) {
        k_relax = config_.stress_rise_rate;
    } else if (s_target < state.stress) {
        k_relax = config_.stress_decay_rate;
    }

    next.stress = std::clamp(
        s_target + (state.stress - s_target) * std::exp(-k_relax * dt),
        0.0f, 1.0f
    );

    // 3. Energy Dynamics
    float burn_multiplier = 1.0f;
    float gain_multiplier = 0.8f;

    switch (state.current_behaviour) {
        case common::Behaviour::Dormant:
            burn_multiplier = 0.10f;
            gain_multiplier = 1.50f;
            break;
        case common::Behaviour::Resting:
            burn_multiplier = 0.40f;
            gain_multiplier = 2.50f;
            break;
        case common::Behaviour::Idle:
            burn_multiplier = 1.00f;
            gain_multiplier = 0.80f;
            break;
        case common::Behaviour::Observing:
            burn_multiplier = 1.20f;
            gain_multiplier = 0.20f;
            break;
        case common::Behaviour::Exploring:
            burn_multiplier = 2.00f;
            gain_multiplier = 0.00f; // Active expenditure: zero rest recovery
            break;
        case common::Behaviour::Stressed:
            burn_multiplier = 2.50f;
            gain_multiplier = 0.00f; // High alarm: zero rest recovery
            break;
        default:
            burn_multiplier = 1.00f; // Nominal Idle fallback
            gain_multiplier = 0.80f; // Nominal Idle fallback
            break;
    }

    const float burn_rate = (config_.basal_energy_burn_rate * burn_multiplier) +
                            (config_.energy_pressure_burn_factor * resolved_pressure);

    const float gain_rate = config_.energy_rest_gain_rate * gain_multiplier * (1.0f - resolved_pressure);

    next.energy = std::clamp(
        state.energy + (gain_rate - burn_rate) * dt,
        0.0f, 1.0f
    );

    // 4. Health Dynamics: Starvation & acute stress damage vs. peaceful healing
    const float starve_damage_drive = std::clamp(
        (config_.critical_energy_threshold - state.energy) / config_.critical_energy_threshold,
        0.0f, 1.0f
    );

    const float stress_damage_drive = std::clamp(
        (state.stress - config_.critical_stress_threshold) / (1.0f - config_.critical_stress_threshold),
        0.0f, 1.0f
    );

    const float damage_rate = (config_.health_starve_damage_rate * starve_damage_drive) +
                              (config_.health_stress_damage_rate * stress_damage_drive);

    float healing_rate = 0.0f;
    if ((state.stress < config_.heal_stress_threshold) && (state.energy > config_.heal_energy_threshold)) {
        const float e_factor = (state.energy - config_.heal_energy_threshold) / (1.0f - config_.heal_energy_threshold);
        const float s_factor = (config_.heal_stress_threshold - state.stress) / config_.heal_stress_threshold;
        healing_rate = config_.health_heal_rate * e_factor * s_factor;
    }

    next.health = std::clamp(
        state.health + (healing_rate - damage_rate) * dt,
        0.0f, 1.0f
    );

    // 5. Curiosity Dynamics: Relaxation toward calm baseline, stimulated by user, suppressed by stress
    const float c_target = std::clamp(
        (config_.curiosity_nominal_target * (1.0f - state.stress)) +
        (is_user_active ? config_.user_activity_boost : 0.0f),
        0.0f, 1.0f
    );

    next.curiosity = std::clamp(
        c_target + (state.curiosity - c_target) * std::exp(-config_.curiosity_relax_rate * dt),
        0.0f, 1.0f
    );

    // 6. Adaptation Dynamics: Gated accumulation (only on fresh confirmed telemetry)
    if (is_fresh_telemetry &&
        (state.stress > config_.adaptation_stress_threshold) &&
        (state.health > config_.adaptation_min_health)) {
        const float stress_factor = (state.stress - config_.adaptation_stress_threshold) /
                                    (1.0f - config_.adaptation_stress_threshold);
        const float delta_a = config_.adaptation_rate * stress_factor * state.health * dt;
        next.adaptation = std::clamp(state.adaptation + delta_a, 0.0f, 1.0f);
    } else {
        next.adaptation = state.adaptation; // Never accumulate on fallback or stale telemetry
    }

    return next;
}

common::OrganismState HomeostasisEngine::update(
    const common::OrganismState& current_state,
    const PressureResult& pressure_result,
    float dt_seconds,
    bool is_user_active
) const noexcept {
    // 1. Invalid elapsed time leaves input state completely unchanged
    if (!is_valid_scalar(dt_seconds) || dt_seconds <= 0.0f) {
        return current_state;
    }

    // 2. Long pause / suspension guard: cap total processed time and discard excess
    const float capped_dt = std::min(dt_seconds, config_.max_total_dt_seconds);

    // 3. Ingress sanitization of incoming state
    const common::OrganismState sanitized_state = sanitize_incoming_state(current_state);

    // 4. Resolve environmental pressure with robust sanitization
    const auto resolved = resolve_pressure(pressure_result);

    // 5. Deterministic Bounded Sub-stepping (capped at 50 sub-steps maximum)
    uint32_t num_steps = static_cast<uint32_t>(std::ceil(capped_dt / config_.max_substep_seconds));
    num_steps = std::clamp(num_steps, 1u, 50u);

    const float dt_sub = capped_dt / static_cast<float>(num_steps);

    common::OrganismState state_iter = sanitized_state;
    for (uint32_t i = 0; i < num_steps; ++i) {
        state_iter = single_substep(state_iter, resolved.value, resolved.is_fresh, dt_sub, is_user_active);
    }

    // Dwell time is preserved untouched (Runtime Coordinator is the sole owner of dwell timer)
    state_iter.dwell_time_seconds = sanitized_state.dwell_time_seconds;

    return state_iter;
}

} // namespace prometheus::brain
