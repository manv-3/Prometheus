#include "gtest/gtest.h"
#include "prometheus/metabolism/Metabolism.hpp"
#include <algorithm>

using prometheus::metabolism::Metabolism;
using prometheus::metabolism::TierBudget;
using prometheus::metabolism::MetabolismConfig;
using prometheus::common::ComputeBudget;
using prometheus::common::QualityTier;
using prometheus::common::Behaviour;
using prometheus::common::OrganismState;

// Helper: build a state with a given behaviour
static OrganismState makeState(Behaviour b, float energy = 1.0f, float stress = 0.0f) {
    OrganismState s;
    s.current_behaviour = b;
    s.energy = energy;
    s.stress = stress;
    return s;
}

// 1. Behaviour → tier mapping: Dormant always gives Tier0
TEST(MetabolismTest, DormantBehaviourGivesTier0) {
    Metabolism m;
    OrganismState s = makeState(Behaviour::Dormant, 0.01f, 0.0f);
    auto budget = m.calculate({}, s, 0.0f, 0.05f);
    EXPECT_EQ(budget.tier, QualityTier::Tier0_Dormant);
    EXPECT_EQ(m.currentTier(), QualityTier::Tier0_Dormant);
}

// 2. Stressed behaviour gives Tier1 (immediate degrade from default Tier3)
TEST(MetabolismTest, StressedBehaviourDegradesToTier1Immediately) {
    Metabolism m;
    // First call from Idle → Tier3 (default starting tier)
    OrganismState idle = makeState(Behaviour::Idle);
    m.calculate({}, idle, 0.1f, 0.05f);
    EXPECT_EQ(m.currentTier(), QualityTier::Tier3_Nominal);

    // Now stressed → degrade to Tier1 immediately
    OrganismState stressed = makeState(Behaviour::Stressed);
    auto budget = m.calculate({}, stressed, 0.80f, 0.05f);
    EXPECT_EQ(m.currentTier(), QualityTier::Tier1_Critical);
    EXPECT_LE(budget.max_particles, 20u);
}

// 3. Degrade is immediate regardless of dwell time
TEST(MetabolismTest, DegradeIsImmediate) {
    Metabolism m;
    // Start at Tier3
    OrganismState idle = makeState(Behaviour::Idle);
    m.calculate({}, idle, 0.1f, 0.05f);

    // Immediately go to Resting → should drop to Tier2 right away
    OrganismState resting = makeState(Behaviour::Resting);
    auto budget = m.calculate({}, resting, 0.3f, 0.001f);
    EXPECT_EQ(m.currentTier(), QualityTier::Tier2_Constrained);
}

// 4. Recovery is paced: one tier at a time, requires dwell interval
TEST(MetabolismTest, RecoveryPacedOneTierAtATime) {
    MetabolismConfig cfg;
    cfg.tierRestoreIntervalSeconds = 2.0f;
    Metabolism m(TierBudget{}, cfg);

    // Start at Tier1 (Stressed)
    OrganismState stressed = makeState(Behaviour::Stressed);
    (void)m.calculate({}, stressed, 0.80f, 0.05f);
    ASSERT_EQ(m.currentTier(), QualityTier::Tier1_Critical);

    // Now conditions improve to Idle (target Tier3).
    // Must wait the full dwell interval BEFORE the first upgrade.
    OrganismState idle = makeState(Behaviour::Idle);
    (void)m.calculate({}, idle, 0.1f, 0.05f);
    EXPECT_EQ(m.currentTier(), QualityTier::Tier1_Critical); // not yet (0.05s < 2.0s)

    (void)m.calculate({}, idle, 0.1f, 1.9f);
    EXPECT_EQ(m.currentTier(), QualityTier::Tier1_Critical); // still (0.05+1.9=1.95 < 2.0)

    (void)m.calculate({}, idle, 0.1f, 0.2f); // total 2.15s >= 2.0 → upgrade one tier
    EXPECT_EQ(m.currentTier(), QualityTier::Tier2_Constrained);

    // Need another full dwell interval to go from Tier2 → Tier3
    (void)m.calculate({}, idle, 0.1f, 1.9f);
    EXPECT_EQ(m.currentTier(), QualityTier::Tier2_Constrained); // 1.9 < 2.0

    (void)m.calculate({}, idle, 0.1f, 0.2f); // 2.1 >= 2.0 → upgrade
    EXPECT_EQ(m.currentTier(), QualityTier::Tier3_Nominal);
}

// 5. Pressure cap prevents high tier even if behaviour would allow it
TEST(MetabolismTest, PressureCapLowersTierBelowBehaviourTarget) {
    Metabolism m;

    // Exploring wants Tier4, but pressure is high enough to cap at Tier2
    OrganismState exploring = makeState(Behaviour::Exploring);
    auto budget = m.calculate({}, exploring, 0.55f, 0.05f);

    // pressure 0.55 >= pressureCapTier3 (0.50) → max Tier2
    EXPECT_LE(m.currentTier(), QualityTier::Tier2_Constrained);
    EXPECT_LE(budget.max_particles, 60u);
}

// 6. Very high pressure forces Tier0 regardless of behaviour
TEST(MetabolismTest, VeryHighPressureForcesTier0) {
    Metabolism m;

    OrganismState exploring = makeState(Behaviour::Exploring);
    auto budget = m.calculate({}, exploring, 0.90f, 0.05f);

    EXPECT_EQ(m.currentTier(), QualityTier::Tier0_Dormant);
    EXPECT_LE(budget.max_particles, 5u);
    EXPECT_LE(budget.target_sim_hz, 1u);
}

// 7. Budget never exceeds HardLimits
TEST(MetabolismTest, BudgetNeverExceedsHardLimits) {
    Metabolism m;
    OrganismState exploring = makeState(Behaviour::Exploring);

    for (int i = 0; i < 100; ++i) {
        float pressure = static_cast<float>(i) / 100.0f;
        auto budget = m.calculate({}, exploring, pressure, 5.0f);
        EXPECT_LE(budget.max_particles, ComputeBudget::HARD_MAX_PARTICLES);
        EXPECT_LE(budget.target_sim_hz, ComputeBudget::HARD_MAX_SIM_HZ);
        EXPECT_LE(budget.target_render_hz, ComputeBudget::HARD_MAX_RENDER_HZ);
        EXPECT_GE(budget.target_sim_hz, ComputeBudget::HARD_MIN_SIM_HZ);
        EXPECT_GE(budget.target_render_hz, ComputeBudget::HARD_MIN_RENDER_HZ);
    }
}

// 8. Dormant behaviour always gives Tier0 even with zero pressure
TEST(MetabolismTest, DormantOverridesLowPressure) {
    Metabolism m;
    OrganismState dormant = makeState(Behaviour::Dormant, 0.01f);
    auto budget = m.calculate({}, dormant, 0.0f, 0.05f);
    EXPECT_EQ(budget.tier, QualityTier::Tier0_Dormant);
}

// 9. Idle and Observing both map to Tier3
TEST(MetabolismTest, IdleAndObservingMapToTier3) {
    Metabolism m;

    // Reset to Tier1 first
    OrganismState stressed = makeState(Behaviour::Stressed);
    m.calculate({}, stressed, 0.80f, 0.05f);
    ASSERT_EQ(m.currentTier(), QualityTier::Tier1_Critical);

    // Upgrade through tiers with enough dwell time
    MetabolismConfig cfg;
    cfg.tierRestoreIntervalSeconds = 0.001f; // very fast for testing
    Metabolism m2(TierBudget{}, cfg);

    OrganismState idle = makeState(Behaviour::Idle);
    m2.calculate({}, idle, 0.1f, 0.05f);
    m2.calculate({}, idle, 0.1f, 0.01f); // enough dwell
    m2.calculate({}, idle, 0.1f, 0.01f);
    EXPECT_EQ(m2.currentTier(), QualityTier::Tier3_Nominal);

    // Observing should also map to Tier3
    Metabolism m3(TierBudget{}, cfg);
    OrganismState obs = makeState(Behaviour::Observing);
    m3.calculate({}, obs, 0.1f, 0.05f);
    m3.calculate({}, obs, 0.1f, 0.01f);
    m3.calculate({}, obs, 0.1f, 0.01f);
    EXPECT_EQ(m3.currentTier(), QualityTier::Tier3_Nominal);
}

// 10. Zero or negative dt does not advance tier timer
TEST(MetabolismTest, ZeroOrNegativeDtDoesNotAdvanceTierTimer) {
    MetabolismConfig cfg;
    cfg.tierRestoreIntervalSeconds = 1.0f;
    Metabolism m(TierBudget{}, cfg);

    // Start at Tier1
    OrganismState stressed = makeState(Behaviour::Stressed);
    m.calculate({}, stressed, 0.80f, 0.05f);
    ASSERT_EQ(m.currentTier(), QualityTier::Tier1_Critical);

    // Idle with zero dt — should not advance
    OrganismState idle = makeState(Behaviour::Idle);
    m.calculate({}, idle, 0.1f, 0.0f);
    EXPECT_EQ(m.currentTier(), QualityTier::Tier1_Critical); // still Tier1

    m.calculate({}, idle, 0.1f, -1.0f);
    EXPECT_EQ(m.currentTier(), QualityTier::Tier1_Critical); // still Tier1
}

// 11. Particle count decreases when pressure rises (the proof chain)
TEST(MetabolismTest, ParticleCountDecreasesWhenPressureRises) {
    Metabolism m;

    // Low pressure, Exploring → Tier4 → 300 particles
    OrganismState exploring = makeState(Behaviour::Exploring);
    auto lowP = m.calculate({}, exploring, 0.1f, 0.05f);
    uint32_t particlesLow = lowP.max_particles;

    // High pressure → capped down
    auto highP = m.calculate({}, exploring, 0.75f, 0.05f);
    uint32_t particlesHigh = highP.max_particles;

    EXPECT_LT(particlesHigh, particlesLow);
}

// 12. Exploring at low pressure eventually reaches Tier4 with enough dwell time
TEST(MetabolismTest, ExploringReachesTier4WithDwellTime) {
    MetabolismConfig cfg;
    cfg.tierRestoreIntervalSeconds = 0.01f; // fast for testing
    Metabolism m(TierBudget{}, cfg);

    // Start at Tier3 (default)
    OrganismState exploring = makeState(Behaviour::Exploring);
    m.calculate({}, exploring, 0.05f, 0.05f);

    // Need one dwell interval to upgrade to Tier4
    m.calculate({}, exploring, 0.05f, 0.02f); // enough dwell
    EXPECT_EQ(m.currentTier(), QualityTier::Tier4_Optimal);
    EXPECT_EQ(m.currentTier(), QualityTier::Tier4_Optimal);
}
