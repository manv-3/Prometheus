#include "gtest/gtest.h"
#include "prometheus/brain/HomeostasisEngine.hpp"
#include <limits>
#include <cmath>

using prometheus::brain::HomeostasisEngine;
using prometheus::brain::HomeostasisConfig;
using prometheus::brain::PressureResult;
using prometheus::common::OrganismState;
using prometheus::common::Behaviour;

// Helper to make a valid pressure result
static PressureResult make_valid_pressure(float p) {
    PressureResult res;
    res.raw_pressure = p;
    res.smoothed_pressure = p;
    res.is_valid = true;
    res.has_smoothed_value = true;
    res.contributing_signals = 1;
    res.total_valid_weight = 1.0f;
    return res;
}

// Helper to make an invalid pressure result (e.g. sensor blackout)
static PressureResult make_invalid_pressure(bool has_stale = false, float stale_p = 0.5f) {
    PressureResult res;
    res.raw_pressure = 0.0f;
    res.smoothed_pressure = stale_p;
    res.is_valid = false;
    res.has_smoothed_value = has_stale;
    res.contributing_signals = 0;
    res.total_valid_weight = 0.0f;
    return res;
}

// 1. Starvation Stress Target Independence: Low energy increases S_target and drives stress up
TEST(HomeostasisEngineTest, StarvationIncreasesStressTargetUnderZeroPressure) {
    HomeostasisEngine engine;
    OrganismState state;
    state.energy = 0.0f; // Critical starvation
    state.stress = 0.0f; // Calm initially
    state.current_behaviour = Behaviour::Idle;

    auto p = make_valid_pressure(0.0f); // Host is idle

    auto next = engine.update(state, p, 1.0f);
    EXPECT_GT(next.stress, 0.0f);
    // Over 1.0s, with S_target = 0.60, stress should rise toward 0.60
    EXPECT_GT(next.stress, 0.20f);
    EXPECT_LE(next.stress, 1.0f);
}

// 2. Starvation Can Never Accidentally Reduce Stress
TEST(HomeostasisEngineTest, StarvationNeverReducesStressEvenWhenStressIsHigh) {
    HomeostasisEngine engine;
    OrganismState state;
    state.energy = 0.0f; // Starving
    state.stress = 0.50f; // Already high stress
    state.current_behaviour = Behaviour::Idle;

    auto p = make_valid_pressure(0.0f); // P_eff = 0

    // With D_starve = 1.0, S_target = 0.60 > 0.50.
    // Stress must rise, not drop!
    auto next = engine.update(state, p, 1.0f);
    EXPECT_GT(next.stress, 0.50f);
}

// 3. Asymmetric Relaxation: Stress rises faster than it decays
TEST(HomeostasisEngineTest, StressAsymmetricRiseVersusDecay) {
    HomeostasisEngine engine;
    auto p_high = make_valid_pressure(1.0f);
    auto p_low = make_valid_pressure(0.0f);

    // Rise test from 0.0
    OrganismState s_rise;
    s_rise.energy = 1.0f;
    s_rise.stress = 0.0f;
    auto after_rise = engine.update(s_rise, p_high, 1.0f);
    float rise_delta = after_rise.stress - 0.0f;

    // Decay test from 1.0
    OrganismState s_decay;
    s_decay.energy = 1.0f;
    s_decay.stress = 1.0f;
    auto after_decay = engine.update(s_decay, p_low, 1.0f);
    float decay_delta = 1.0f - after_decay.stress;

    // Fast rise rate (0.50) vs slow decay rate (0.10)
    EXPECT_GT(rise_delta, decay_delta);
}

// 4. Exact Energy Accounting & Single dt Multiplication
TEST(HomeostasisEngineTest, ExactEnergyAccountingInExploring) {
    HomeostasisEngine engine;
    OrganismState state;
    state.energy = 0.80f;
    state.current_behaviour = Behaviour::Exploring; // Zero gain multiplier, 2.0x burn

    auto p = make_valid_pressure(0.0f); // No pressure burn

    // Expected burn_rate = basal (0.010) * 2.0 = 0.020 s^-1
    // Gain rate = 0.0
    // Over 1.0s, delta E should be exactly -0.020
    auto next = engine.update(state, p, 1.0f);
    EXPECT_NEAR(next.energy, 0.780f, 0.001f);
}

// 5. Recovery Rate Validation for Resting and Dormant
TEST(HomeostasisEngineTest, EnergyRecoveryRatesForRestingAndDormant) {
    HomeostasisEngine engine;
    auto p = make_valid_pressure(0.0f);

    // Resting recovery
    OrganismState s_rest;
    s_rest.energy = 0.50f;
    s_rest.current_behaviour = Behaviour::Resting;
    // burn_rate = 0.010 * 0.40 = 0.004 s^-1
    // gain_rate = 0.080 * 2.50 = 0.200 s^-1
    // net_gain = 0.196 s^-1
    auto after_rest = engine.update(s_rest, p, 1.0f);
    EXPECT_NEAR(after_rest.energy, 0.50f + 0.196f, 0.002f);

    // Dormant recovery
    OrganismState s_dorm;
    s_dorm.energy = 0.50f;
    s_dorm.current_behaviour = Behaviour::Dormant;
    // burn_rate = 0.010 * 0.10 = 0.001 s^-1
    // gain_rate = 0.080 * 1.50 = 0.120 s^-1
    // net_gain = 0.119 s^-1
    auto after_dorm = engine.update(s_dorm, p, 1.0f);
    EXPECT_NEAR(after_dorm.energy, 0.50f + 0.119f, 0.002f);

    // Ensure energy clamps at 1.0 and does not exceed upper bound
    s_rest.energy = 0.95f;
    auto after_clamp = engine.update(s_rest, p, 1.0f);
    EXPECT_FLOAT_EQ(after_clamp.energy, 1.0f);
}

// 6. Zero Recovery for Exploring and Stressed
TEST(HomeostasisEngineTest, ExploringAndStressedNeverRecoverEnergy) {
    HomeostasisEngine engine;
    auto p = make_valid_pressure(0.0f);

    OrganismState s_exp;
    s_exp.energy = 0.50f;
    s_exp.current_behaviour = Behaviour::Exploring;
    EXPECT_LT(engine.update(s_exp, p, 1.0f).energy, 0.50f);

    OrganismState s_str;
    s_str.energy = 0.50f;
    s_str.current_behaviour = Behaviour::Stressed;
    EXPECT_LT(engine.update(s_str, p, 1.0f).energy, 0.50f);
}

// 7. Health Damage Discontinuities
TEST(HomeostasisEngineTest, HealthDamageThresholdDiscontinuities) {
    HomeostasisEngine engine;
    auto p = make_valid_pressure(0.0f);

    // Safe energy: E = 0.15 > E_crit (0.10) -> no starvation damage
    OrganismState s_safe;
    s_safe.health = 1.0f;
    s_safe.energy = 0.15f;
    s_safe.stress = 0.0f;
    EXPECT_FLOAT_EQ(engine.update(s_safe, p, 1.0f).health, 1.0f);

    // Critical starvation: E = 0.05 < E_crit (0.10) -> damage occurs
    OrganismState s_starve;
    s_starve.health = 1.0f;
    s_starve.energy = 0.05f;
    s_starve.stress = 0.0f;
    EXPECT_LT(engine.update(s_starve, p, 1.0f).health, 1.0f);

    // High stress damage: S = 0.75 < S_crit (0.80) -> no stress damage
    OrganismState s_safe_stress;
    s_safe_stress.health = 1.0f;
    s_safe_stress.energy = 0.50f;
    s_safe_stress.stress = 0.75f;
    EXPECT_FLOAT_EQ(engine.update(s_safe_stress, p, 1.0f).health, 1.0f);

    // Extreme stress damage: S = 0.90 > S_crit (0.80) -> damage occurs
    OrganismState s_bad_stress;
    s_bad_stress.health = 1.0f;
    s_bad_stress.energy = 0.50f;
    s_bad_stress.stress = 0.90f;
    EXPECT_LT(engine.update(s_bad_stress, p, 1.0f).health, 1.0f);
}

// 8. Peaceful Healing Criteria
TEST(HomeostasisEngineTest, PeacefulHealingCriteria) {
    HomeostasisEngine engine;
    auto p = make_valid_pressure(0.0f);

    // Safe and nourished: S = 0.10 < 0.30 and E = 0.80 > 0.40 -> healing occurs
    OrganismState s_heal;
    s_heal.health = 0.50f;
    s_heal.energy = 0.80f;
    s_heal.stress = 0.10f;
    s_heal.current_behaviour = Behaviour::Resting;
    EXPECT_GT(engine.update(s_heal, p, 1.0f).health, 0.50f);

    // Blocked by high stress: S = 0.40 > 0.30
    OrganismState s_blocked_stress = s_heal;
    s_blocked_stress.stress = 0.40f;
    EXPECT_FLOAT_EQ(engine.update(s_blocked_stress, p, 1.0f).health, 0.50f);

    // Blocked by low energy: E = 0.25 < 0.40 in Idle state so energy stays below threshold
    OrganismState s_blocked_energy = s_heal;
    s_blocked_energy.current_behaviour = Behaviour::Idle;
    s_blocked_energy.energy = 0.25f;
    EXPECT_FLOAT_EQ(engine.update(s_blocked_energy, p, 1.0f).health, 0.50f);
}

// 9. Curiosity Dynamics: Stimulation and Stress Suppression
TEST(HomeostasisEngineTest, CuriosityStimulationAndSuppression) {
    HomeostasisEngine engine;
    auto p = make_valid_pressure(0.0f);

    // User activity boosts curiosity
    OrganismState s_calm;
    s_calm.curiosity = 0.50f;
    s_calm.stress = 0.0f;
    auto with_user = engine.update(s_calm, p, 1.0f, true);
    auto without_user = engine.update(s_calm, p, 1.0f, false);
    EXPECT_GT(with_user.curiosity, without_user.curiosity);

    // High stress depresses curiosity toward 0.0
    OrganismState s_stressed = s_calm;
    s_stressed.stress = 1.0f;
    auto after_stress = engine.update(s_stressed, p, 2.0f, false);
    EXPECT_LT(after_stress.curiosity, 0.50f);
}

// 10. Freshness vs Availability: Adaptation Accumulates ONLY on Fresh Telemetry
TEST(HomeostasisEngineTest, AdaptationAccumulatesOnlyOnFreshTelemetry) {
    HomeostasisEngine engine;

    OrganismState state;
    state.adaptation = 0.10f;
    state.stress = 0.80f; // High stress (> threshold 0.50)
    state.health = 0.90f; // Good health (> min 0.20)

    // Fresh valid telemetry: Adaptation MUST increase
    auto p_fresh = make_valid_pressure(0.80f);
    auto after_fresh = engine.update(state, p_fresh, 1.0f);
    EXPECT_GT(after_fresh.adaptation, 0.10f);

    // Stale/invalid telemetry (even with high stale smoothed pressure): Adaptation MUST NOT increase
    auto p_stale = make_invalid_pressure(true, 0.80f);
    auto after_stale = engine.update(state, p_stale, 1.0f);
    EXPECT_FLOAT_EQ(after_stale.adaptation, 0.10f);

    // No sensor data at all: Adaptation preserved
    auto p_none = make_invalid_pressure(false, 0.0f);
    auto after_none = engine.update(state, p_none, 1.0f);
    EXPECT_FLOAT_EQ(after_none.adaptation, 0.10f);
}

// 11. Invalid dt_seconds Rejection Leaves State 100% Unchanged
TEST(HomeostasisEngineTest, InvalidDtLeavesStateUnchanged) {
    HomeostasisEngine engine;
    OrganismState original;
    original.energy = 0.77f;
    original.health = 0.88f;
    original.stress = 0.33f;
    original.curiosity = 0.55f;
    original.adaptation = 0.11f;
    original.dwell_time_seconds = 42.0f;

    auto p = make_valid_pressure(0.50f);

    // Negative dt
    auto r1 = engine.update(original, p, -1.0f);
    EXPECT_FLOAT_EQ(r1.energy, original.energy);
    EXPECT_FLOAT_EQ(r1.dwell_time_seconds, original.dwell_time_seconds);

    // Zero dt
    auto r2 = engine.update(original, p, 0.0f);
    EXPECT_FLOAT_EQ(r2.energy, original.energy);

    // NaN dt
    auto r3 = engine.update(original, p, std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(r3.energy, original.energy);

    // Inf dt
    auto r4 = engine.update(original, p, std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(r4.energy, original.energy);
}

// 12. Large dt Clamping and Sub-Stepping Stability
TEST(HomeostasisEngineTest, LargeDtClampedToFiveSeconds) {
    HomeostasisEngine engine;
    OrganismState state;
    state.dwell_time_seconds = 10.0f;

    auto p = make_valid_pressure(0.0f);

    // Pass dt = 100.0s (e.g. system suspended)
    auto next = engine.update(state, p, 100.0f);

    // Total processed time is capped to max_total_dt_seconds (5.0s) and state remains valid
    EXPECT_GE(next.energy, 0.0f);
    EXPECT_LE(next.energy, 1.0f);
    // Dwell time is preserved untouched by HomeostasisEngine (Runtime Coordinator owns dwell time)
    EXPECT_FLOAT_EQ(next.dwell_time_seconds, 10.0f);
}

// 13. Configuration Denominator and Range Validation
TEST(HomeostasisEngineTest, ConfigurationValidationRejectsInvalidSettings) {
    HomeostasisEngine engine;
    const auto original_cfg = engine.config();

    // 1. Zero low_energy_threshold (division by zero hazard)
    HomeostasisConfig bad_cfg = original_cfg;
    bad_cfg.low_energy_threshold = 0.0f;
    EXPECT_FALSE(engine.set_config(bad_cfg));
    EXPECT_FLOAT_EQ(engine.config().low_energy_threshold, original_cfg.low_energy_threshold);

    // 2. Zero critical_energy_threshold
    bad_cfg = original_cfg;
    bad_cfg.critical_energy_threshold = 0.0f;
    EXPECT_FALSE(engine.set_config(bad_cfg));

    // 3. Contradictory thresholds: critical damage starts before starvation stress
    bad_cfg = original_cfg;
    bad_cfg.critical_energy_threshold = 0.40f;
    bad_cfg.low_energy_threshold = 0.20f;
    EXPECT_FALSE(engine.set_config(bad_cfg));

    // 4. Negative rate
    bad_cfg = original_cfg;
    bad_cfg.stress_rise_rate = -0.5f;
    EXPECT_FALSE(engine.set_config(bad_cfg));

    // 5. Out of range mitigation factor
    bad_cfg = original_cfg;
    bad_cfg.adaptation_mitigation_factor = 1.5f;
    EXPECT_FALSE(engine.set_config(bad_cfg));
}

// 14. Deterministic Output
TEST(HomeostasisEngineTest, DeterministicExecution) {
    HomeostasisEngine engine;
    OrganismState state;
    state.energy = 0.456f;
    state.stress = 0.789f;

    auto p = make_valid_pressure(0.654f);

    auto res1 = engine.update(state, p, 0.25f, true);
    auto res2 = engine.update(state, p, 0.25f, true);

    EXPECT_FLOAT_EQ(res1.energy, res2.energy);
    EXPECT_FLOAT_EQ(res1.health, res2.health);
    EXPECT_FLOAT_EQ(res1.stress, res2.stress);
    EXPECT_FLOAT_EQ(res1.curiosity, res2.curiosity);
    EXPECT_FLOAT_EQ(res1.adaptation, res2.adaptation);
    EXPECT_FLOAT_EQ(res1.dwell_time_seconds, res2.dwell_time_seconds);
}

// 15. Unsafe Denominator Boundaries Rejected by Configuration Validator
TEST(HomeostasisEngineTest, UnsafeDenominatorBoundariesRejected) {
    HomeostasisEngine engine;
    const auto original_cfg = engine.config();

    // critical_stress_threshold >= 0.999f (would cause division by zero in stress damage)
    HomeostasisConfig bad_cfg = original_cfg;
    bad_cfg.critical_stress_threshold = 1.0f;
    EXPECT_FALSE(engine.set_config(bad_cfg));

    bad_cfg.critical_stress_threshold = 0.999f;
    EXPECT_FALSE(engine.set_config(bad_cfg));
    EXPECT_FLOAT_EQ(engine.config().critical_stress_threshold, original_cfg.critical_stress_threshold);

    // heal_energy_threshold >= 0.999f (would cause division by zero in healing e_factor)
    bad_cfg = original_cfg;
    bad_cfg.heal_energy_threshold = 1.0f;
    EXPECT_FALSE(engine.set_config(bad_cfg));

    bad_cfg.heal_energy_threshold = 0.999f;
    EXPECT_FALSE(engine.set_config(bad_cfg));
    EXPECT_FLOAT_EQ(engine.config().heal_energy_threshold, original_cfg.heal_energy_threshold);

    // heal_stress_threshold <= 0.001f (would cause division by zero in healing s_factor)
    bad_cfg = original_cfg;
    bad_cfg.heal_stress_threshold = 0.0f;
    EXPECT_FALSE(engine.set_config(bad_cfg));

    bad_cfg.heal_stress_threshold = 0.001f;
    EXPECT_FALSE(engine.set_config(bad_cfg));
    EXPECT_FLOAT_EQ(engine.config().heal_stress_threshold, original_cfg.heal_stress_threshold);
}

// 16. Sub-step Duration Guarantee: max_total_dt <= 50 * max_substep
TEST(HomeostasisEngineTest, SubstepDurationGuaranteeEnforcedInConfig) {
    HomeostasisEngine engine;
    const auto original_cfg = engine.config();

    // 5.0 / 0.05 = 100 > 50 steps -> must be rejected
    HomeostasisConfig bad_cfg = original_cfg;
    bad_cfg.max_total_dt_seconds = 5.0f;
    bad_cfg.max_substep_seconds = 0.05f;
    EXPECT_FALSE(engine.set_config(bad_cfg));
    EXPECT_FLOAT_EQ(engine.config().max_substep_seconds, original_cfg.max_substep_seconds);

    // Valid custom configuration: 2.0 / 0.05 = 40 <= 50 steps -> accepted
    HomeostasisConfig good_cfg = original_cfg;
    good_cfg.max_total_dt_seconds = 2.0f;
    good_cfg.max_substep_seconds = 0.05f;
    EXPECT_TRUE(engine.set_config(good_cfg));
    EXPECT_FLOAT_EQ(engine.config().max_substep_seconds, 0.05f);

    // Test that running with dt = 2.0f executes cleanly and state remains valid
    OrganismState state;
    auto p = make_valid_pressure(0.5f);
    auto next = engine.update(state, p, 2.0f);
    EXPECT_GE(next.energy, 0.0f);
    EXPECT_LE(next.energy, 1.0f);
    EXPECT_FLOAT_EQ(next.dwell_time_seconds, 0.0f);
}

// 17. NaN and Infinity in Incoming State Sanitized Conservatively
TEST(HomeostasisEngineTest, NanAndInfinityInIncomingStateSanitizedConservatively) {
    HomeostasisEngine engine;
    auto p = make_valid_pressure(0.5f);

    OrganismState corrupt;
    corrupt.energy = std::numeric_limits<float>::quiet_NaN();
    corrupt.health = std::numeric_limits<float>::infinity();
    corrupt.stress = -std::numeric_limits<float>::infinity();
    corrupt.curiosity = std::numeric_limits<float>::quiet_NaN();
    corrupt.adaptation = std::numeric_limits<float>::quiet_NaN();

    auto clean = engine.update(corrupt, p, 0.1f);

    // Corrupt energy and health must NOT be granted free 1.0; they fall back to conservative 0.0
    EXPECT_FALSE(std::isnan(clean.energy));
    EXPECT_FALSE(std::isinf(clean.energy));
    EXPECT_GE(clean.energy, 0.0f);
    EXPECT_LE(clean.energy, 1.0f);

    EXPECT_FALSE(std::isnan(clean.health));
    EXPECT_FALSE(std::isinf(clean.health));
    EXPECT_GE(clean.health, 0.0f);
    EXPECT_LE(clean.health, 1.0f);

    EXPECT_FALSE(std::isnan(clean.stress));
    EXPECT_GE(clean.stress, 0.0f);
    EXPECT_LE(clean.stress, 1.0f);

    EXPECT_FALSE(std::isnan(clean.curiosity));
    EXPECT_GE(clean.curiosity, 0.0f);
    EXPECT_LE(clean.curiosity, 1.0f);

    EXPECT_FALSE(std::isnan(clean.adaptation));
    EXPECT_GE(clean.adaptation, 0.0f);
    EXPECT_LE(clean.adaptation, 1.0f);
}

// 18. Out-of-Range Incoming State Variables Clamped
TEST(HomeostasisEngineTest, OutOfRangeIncomingStateClamped) {
    HomeostasisEngine engine;
    auto p = make_valid_pressure(0.5f);

    OrganismState out_of_bounds;
    out_of_bounds.energy = 5.0f;
    out_of_bounds.health = -2.0f;
    out_of_bounds.stress = 10.0f;
    out_of_bounds.curiosity = -1.0f;
    out_of_bounds.adaptation = 4.0f;

    auto result = engine.update(out_of_bounds, p, 0.1f);

    EXPECT_GE(result.energy, 0.0f);
    EXPECT_LE(result.energy, 1.0f);
    EXPECT_GE(result.health, 0.0f);
    EXPECT_LE(result.health, 1.0f);
    EXPECT_GE(result.stress, 0.0f);
    EXPECT_LE(result.stress, 1.0f);
    EXPECT_GE(result.curiosity, 0.0f);
    EXPECT_LE(result.curiosity, 1.0f);
    EXPECT_GE(result.adaptation, 0.0f);
    EXPECT_LE(result.adaptation, 1.0f);
}

// 19. NaN and Infinity in Pressure Handled Safely
TEST(HomeostasisEngineTest, NanAndInfinityInPressureHandledSafely) {
    HomeostasisEngine engine;
    OrganismState state;
    state.stress = 0.50f;
    state.health = 0.90f;
    state.adaptation = 0.10f;

    // Both raw and smoothed are NaN, but claimed valid
    PressureResult bad_pressure;
    bad_pressure.raw_pressure = std::numeric_limits<float>::quiet_NaN();
    bad_pressure.smoothed_pressure = std::numeric_limits<float>::quiet_NaN();
    bad_pressure.is_valid = true;
    bad_pressure.has_smoothed_value = true;

    auto next = engine.update(state, bad_pressure, 0.5f);

    // Must not produce NaN or Infinity
    EXPECT_FALSE(std::isnan(next.energy));
    EXPECT_FALSE(std::isnan(next.stress));
    EXPECT_FALSE(std::isnan(next.health));
    EXPECT_FALSE(std::isnan(next.curiosity));
    EXPECT_FALSE(std::isnan(next.adaptation));

    // Crucially: Adaptation MUST NOT accumulate from non-finite pressure!
    EXPECT_FLOAT_EQ(next.adaptation, 0.10f);
}

// 20. Inconsistent Pressure Validity Flags Handled Safely
TEST(HomeostasisEngineTest, InconsistentPressureValidityFlagsHandledSafely) {
    HomeostasisEngine engine;
    OrganismState state;

    // is_valid = false, has_smoothed_value = true, but smoothed_pressure is Inf
    PressureResult p_inconsistent;
    p_inconsistent.raw_pressure = 0.0f;
    p_inconsistent.smoothed_pressure = std::numeric_limits<float>::infinity();
    p_inconsistent.is_valid = false;
    p_inconsistent.has_smoothed_value = true;

    auto next = engine.update(state, p_inconsistent, 0.5f);

    EXPECT_FALSE(std::isinf(next.energy));
    EXPECT_FALSE(std::isinf(next.stress));
    EXPECT_GE(next.energy, 0.0f);
    EXPECT_LE(next.energy, 1.0f);
}

// 21. Unknown Behaviour Enum Fallback
TEST(HomeostasisEngineTest, UnknownBehaviourEnumHandledSafely) {
    HomeostasisEngine engine;
    OrganismState state;
    state.energy = 0.50f;
    state.current_behaviour = static_cast<Behaviour>(99); // Unknown enum value

    auto p = make_valid_pressure(0.0f);

    // Must execute default branch without crash or undefined behavior
    auto next = engine.update(state, p, 1.0f);
    EXPECT_FALSE(std::isnan(next.energy));
    EXPECT_GE(next.energy, 0.0f);
    EXPECT_LE(next.energy, 1.0f);
    EXPECT_EQ(next.current_behaviour, static_cast<Behaviour>(99));
}

// 22. Dwell-Time Safety and Overflow Guard
TEST(HomeostasisEngineTest, DwellTimeSafetyAndOverflowGuard) {
    HomeostasisEngine engine;
    auto p = make_valid_pressure(0.5f);

    // Negative dwell time sanitized to 0.0f and preserved
    OrganismState s_neg;
    s_neg.dwell_time_seconds = -10.0f;
    auto r1 = engine.update(s_neg, p, 1.0f);
    EXPECT_FLOAT_EQ(r1.dwell_time_seconds, 0.0f);

    // NaN dwell time sanitized to 0.0f and preserved
    OrganismState s_nan;
    s_nan.dwell_time_seconds = std::numeric_limits<float>::quiet_NaN();
    auto r2 = engine.update(s_nan, p, 1.0f);
    EXPECT_FLOAT_EQ(r2.dwell_time_seconds, 0.0f);

    // Extremely large dwell time clamped to MAX_DWELL_SECONDS (~10 years)
    OrganismState s_huge;
    s_huge.dwell_time_seconds = 500000000.0f;
    auto r3 = engine.update(s_huge, p, 1.0f);
    EXPECT_LE(r3.dwell_time_seconds, 315360000.0f);
    EXPECT_FALSE(std::isinf(r3.dwell_time_seconds));
}

// 23. Regression Test: Dwell Time Not Incremented by HomeostasisEngine (Coordinator Ownership)
TEST(HomeostasisEngineTest, DwellTimeNotIncrementedByHomeostasis) {
    HomeostasisEngine engine;
    auto p = make_valid_pressure(0.3f);

    OrganismState state;
    state.dwell_time_seconds = 7.5f;

    auto next = engine.update(state, p, 2.0f);
    // HomeostasisEngine must preserve dwell_time_seconds without incrementing
    EXPECT_FLOAT_EQ(next.dwell_time_seconds, 7.5f);
}
