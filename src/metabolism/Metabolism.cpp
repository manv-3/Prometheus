#include "prometheus/metabolism/Metabolism.hpp"

namespace prometheus::metabolism {

// ---------------------------------------------------------------------------
// Real Metabolism implementation (TASK-2.2).
//
// The pressure → behaviour → budget → workload chain is completed here:
//   1. Behaviour → target tier (qualitative mode)
//   2. Smoothed pressure → secondary tier cap (defence-in-depth)
//   3. Asymmetric pacing: degrade immediately, recover one tier per interval
//   4. Hard-limit clamp as the LAST unconditional operation
// ---------------------------------------------------------------------------

Metabolism::Metabolism(const TierBudget& tiers, const MetabolismConfig& config) noexcept
    : tiers_(tiers)
    , config_(config)
{}

// ---- Behaviour → target tier mapping ----

common::QualityTier Metabolism::behaviourToTier(common::Behaviour behaviour) noexcept {
    switch (behaviour) {
        case common::Behaviour::Dormant:   return common::QualityTier::Tier0_Dormant;
        case common::Behaviour::Stressed:  return common::QualityTier::Tier1_Critical;
        case common::Behaviour::Resting:   return common::QualityTier::Tier2_Constrained;
        case common::Behaviour::Idle:      return common::QualityTier::Tier3_Nominal;
        case common::Behaviour::Observing: return common::QualityTier::Tier3_Nominal;
        case common::Behaviour::Exploring: return common::QualityTier::Tier4_Optimal;
        default:                           return common::QualityTier::Tier3_Nominal;
    }
}

// ---- Pressure → tier cap (defence-in-depth) ----

common::QualityTier Metabolism::pressureCapTier(float smoothedPressure) const noexcept {
    // Higher pressure caps the tier lower. These thresholds are untuned.
    if (smoothedPressure >= config_.pressureCapTier1) return common::QualityTier::Tier0_Dormant;
    if (smoothedPressure >= config_.pressureCapTier2) return common::QualityTier::Tier1_Critical;
    if (smoothedPressure >= config_.pressureCapTier3) return common::QualityTier::Tier2_Constrained;
    if (smoothedPressure >= config_.pressureCapTier4) return common::QualityTier::Tier3_Nominal;
    return common::QualityTier::Tier4_Optimal;
}

// ---- Tier → budget lookup ----

const common::ComputeBudget& Metabolism::tierToBudget(common::QualityTier tier) const noexcept {
    switch (tier) {
        case common::QualityTier::Tier0_Dormant:     return tiers_.dormant;
        case common::QualityTier::Tier1_Critical:    return tiers_.survival;
        case common::QualityTier::Tier2_Constrained: return tiers_.reduced;
        case common::QualityTier::Tier3_Nominal:     return tiers_.nominal;
        case common::QualityTier::Tier4_Optimal:      return tiers_.optimal;
        default:                                     return tiers_.nominal;
    }
}

// ---- Hard-limit clamp (always last, unconditional) ----

common::ComputeBudget Metabolism::clampToHardLimits(const common::ComputeBudget& proposed) noexcept {
    common::ComputeBudget safe = proposed;
    safe.max_particles    = std::min(safe.max_particles,    common::ComputeBudget::HARD_MAX_PARTICLES);
    safe.target_sim_hz    = std::clamp(safe.target_sim_hz,
                                       common::ComputeBudget::HARD_MIN_SIM_HZ,
                                       common::ComputeBudget::HARD_MAX_SIM_HZ);
    safe.target_render_hz = std::clamp(safe.target_render_hz,
                                       common::ComputeBudget::HARD_MIN_RENDER_HZ,
                                       common::ComputeBudget::HARD_MAX_RENDER_HZ);
    return safe;
}

// ---- setFixedTier (TASK-4.2) ----

void Metabolism::setFixedTier(std::optional<common::QualityTier> tier) noexcept {
    fixedTier_ = tier;
    if (tier.has_value()) {
        // Snap the internal tracker so currentTier() reports the pinned value
        currentTier_    = *tier;
        secondsInTier_  = 0.0f;
    }
}

// ---- Main calculation ----

common::ComputeBudget Metabolism::calculate(
    const common::HostObservation& /*host*/,
    const common::OrganismState& organism,
    float smoothedPressure,
    float dt
) noexcept {
    // 1. Determine the target tier from behaviour
    common::QualityTier behaviourTier = behaviourToTier(organism.current_behaviour);

    // 2. Apply pressure cap (defence-in-depth — pressure can only lower the tier)
    common::QualityTier pressureTier = pressureCapTier(smoothedPressure);
    common::QualityTier targetTier = std::min(behaviourTier, pressureTier);

    // 3. Advance time in current tier
    if (dt > 0.0f) {
        secondsInTier_ += dt;
    }

    // Fixed-complexity mode (TASK-4.2): bypass all adaptive logic
    if (fixedTier_.has_value()) {
        currentTier_ = *fixedTier_;
        return clampToHardLimits(tierToBudget(currentTier_));
    }

    // 4. Asymmetric pacing (REQ-BUDGET-03)
    if (targetTier <= currentTier_) {
        // Degrade: apply immediately (degrade fast)
        currentTier_ = targetTier;
        secondsInTier_ = 0.0f;
    } else if (targetTier > currentTier_) {
        // Recover: only one tier at a time, and only after the dwell interval
        if (secondsInTier_ >= config_.tierRestoreIntervalSeconds) {
            currentTier_ = static_cast<common::QualityTier>(
                static_cast<uint8_t>(currentTier_) + 1
            );
            secondsInTier_ = 0.0f;
        }
        // If dwell not satisfied, keep current tier (don't restore yet)
    }

    // 5. Look up the budget for the (possibly still-degrading) current tier
    common::ComputeBudget budget = tierToBudget(currentTier_);

    // 6. Hard-limit clamp — ALWAYS the last unconditional operation
    return clampToHardLimits(budget);
}

// ---- Default tier table ----

TierBudget Metabolism::defaultTiers() noexcept {
    // All values are initial, untuned placeholders.
    TierBudget t;

    // Tier 0 — Dormant
    t.dormant.tier             = common::QualityTier::Tier0_Dormant;
    t.dormant.max_particles    = 5;
    t.dormant.target_sim_hz    = 1;
    t.dormant.target_render_hz = 1;
    t.dormant.enable_complex_physics   = false;
    t.dormant.enable_secondary_effects = false;

    // Tier 1 — Critical (Survival)
    t.survival.tier             = common::QualityTier::Tier1_Critical;
    t.survival.max_particles    = 20;
    t.survival.target_sim_hz    = 5;
    t.survival.target_render_hz = 5;
    t.survival.enable_complex_physics   = false;
    t.survival.enable_secondary_effects = false;

    // Tier 2 — Constrained (Reduced)
    t.reduced.tier             = common::QualityTier::Tier2_Constrained;
    t.reduced.max_particles    = 60;
    t.reduced.target_sim_hz    = 12;
    t.reduced.target_render_hz = 12;
    t.reduced.enable_complex_physics   = false;
    t.reduced.enable_secondary_effects = false;

    // Tier 3 — Nominal
    t.nominal.tier             = common::QualityTier::Tier3_Nominal;
    t.nominal.max_particles    = 150;
    t.nominal.target_sim_hz    = 20;
    t.nominal.target_render_hz = 20;
    t.nominal.enable_complex_physics   = true;
    t.nominal.enable_secondary_effects = false;

    // Tier 4 — Optimal (Thriving)
    t.optimal.tier             = common::QualityTier::Tier4_Optimal;
    t.optimal.max_particles    = 300;
    t.optimal.target_sim_hz    = 30;
    t.optimal.target_render_hz = 30;
    t.optimal.enable_complex_physics   = true;
    t.optimal.enable_secondary_effects = true;

    return t;
}

} // namespace prometheus::metabolism
