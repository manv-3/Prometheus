#include "prometheus/metabolism/Metabolism.hpp"
#include <algorithm>

namespace prometheus::metabolism {

// STUB — Phase 0 skeleton. Dev B owns the full implementation (TASK-2.2).
// Real version: tier table lookup, asymmetric pacing, hard-limit clamp as the
// LAST unconditional operation before returning.

Metabolism::Metabolism(const TierBudget& tiers) noexcept
    : tiers_(tiers) {}

common::TierBudget Metabolism::defaultTiers() noexcept {
    // All values are initial, untuned placeholders.
    TierBudget t;

    // Tier 0 — Dormant
    t.dormant.tier             = common::QualityTier::Tier0_Dormant;
    t.dormant.max_particles    = 5;
    t.dormant.target_sim_hz    = 1;
    t.dormant.target_render_hz = 1;
    t.dormant.enable_complex_physics   = false;
    t.dormant.enable_secondary_effects = false;

    // Tier 1 — Critical
    t.survival.tier             = common::QualityTier::Tier1_Critical;
    t.survival.max_particles    = 20;
    t.survival.target_sim_hz    = 5;
    t.survival.target_render_hz = 5;
    t.survival.enable_complex_physics   = false;
    t.survival.enable_secondary_effects = false;

    // Tier 2 — Constrained
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

    // Tier 4 — Optimal
    t.optimal.tier             = common::QualityTier::Tier4_Optimal;
    t.optimal.max_particles    = 300;
    t.optimal.target_sim_hz    = 30;
    t.optimal.target_render_hz = 30;
    t.optimal.enable_complex_physics   = true;
    t.optimal.enable_secondary_effects = true;

    return t;
}

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

common::ComputeBudget Metabolism::calculate(
    const common::HostObservation& /*host*/,
    const common::OrganismState& /*organism*/
) noexcept {
    // STUB: return a fixed Nominal budget so the Body and UI have something
    // to work with before Dev B ships the real tier selection (TASK-2.2).
    return clampToHardLimits(tiers_.nominal);
}

} // namespace prometheus::metabolism
