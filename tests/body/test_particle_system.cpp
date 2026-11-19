#include "gtest/gtest.h"
#include "prometheus/body/ParticleSystem.hpp"
#include "prometheus/common/ComputeBudget.hpp"

using prometheus::body::ParticleSystem;
using prometheus::body::kMaxParticles;
using prometheus::common::ComputeBudget;
using prometheus::common::QualityTier;

// Helper: build a ComputeBudget with a specific particle limit
static ComputeBudget makeBudget(uint32_t particles, bool complex = false) {
    ComputeBudget b;
    b.tier = QualityTier::Tier3_Nominal;
    b.max_particles = particles;
    b.target_sim_hz = 20;
    b.target_render_hz = 20;
    b.enable_complex_physics = complex;
    b.enable_secondary_effects = false;
    return b;
}

// 1. Pool capacity is kMaxParticles; never exceeded
TEST(ParticleSystemTest, CapacityNeverExceeded) {
    ParticleSystem ps;
    EXPECT_EQ(ps.capacity(), kMaxParticles);
    EXPECT_EQ(ps.activeCount(), 0u);

    // Fill to capacity
    for (uint32_t i = 0; i < kMaxParticles; ++i) {
        auto* p = ps.activate();
        EXPECT_NE(p, nullptr) << "activate() returned nullptr before capacity";
    }
    EXPECT_EQ(ps.activeCount(), kMaxParticles);

    // One more must fail gracefully
    auto* overflow = ps.activate();
    EXPECT_EQ(overflow, nullptr);
    EXPECT_EQ(ps.activeCount(), kMaxParticles);
}

// 2. activate() returns a non-null, active particle and increments activeCount
TEST(ParticleSystemTest, ActivateReturnsActiveParticle) {
    ParticleSystem ps;
    auto* p = ps.activate();
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(p->active);
    EXPECT_EQ(ps.activeCount(), 1u);
}

// 3. deactivate() decrements activeCount and uses swap-with-last (O(1))
TEST(ParticleSystemTest, DeactivateSwapsWithLast) {
    ParticleSystem ps;
    auto* p0 = ps.activate();
    auto* p1 = ps.activate();
    auto* p2 = ps.activate();
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);

    // Mark particles with sentinel values via the mutable activate() return
    p0->x = 99.0f;
    p2->x = 77.0f; // last active

    // Deactivating index 0 should swap index 2 into slot 0
    ps.deactivate(0);
    EXPECT_EQ(ps.activeCount(), 2u);
    EXPECT_FLOAT_EQ(ps.data()[0].x, 77.0f); // former last now at index 0
}

// 4. trimToLimit() deactivates excess particles to match budget
TEST(ParticleSystemTest, TrimToLimitEnforcesCount) {
    ParticleSystem ps;

    // Activate 50 particles
    for (uint32_t i = 0; i < 50; ++i) {
        ASSERT_NE(ps.activate(), nullptr);
    }
    EXPECT_EQ(ps.activeCount(), 50u);

    // Trim to 20
    ps.trimToLimit(20);
    EXPECT_EQ(ps.activeCount(), 20u);

    // Trim to 0
    ps.trimToLimit(0);
    EXPECT_EQ(ps.activeCount(), 0u);
}

// 5. update() enforces budget.max_particles before ticking
TEST(ParticleSystemTest, UpdateEnforcesParticleLimitBeforeTick) {
    ParticleSystem ps;

    // Activate 100 particles
    for (uint32_t i = 0; i < 100; ++i) {
        ps.activate();
    }
    EXPECT_EQ(ps.activeCount(), 100u);

    // Budget only allows 30
    auto budget = makeBudget(30);
    ps.update(budget, 0.05f);

    // After update active count must be <= 30
    EXPECT_LE(ps.activeCount(), 30u);
}

// 6. No allocation after construction (structural: activeCount changes only via activate/deactivate)
//    We verify this indirectly: repeatedly activating and deactivating does not change capacity.
TEST(ParticleSystemTest, CapacityUnchangedAfterActivateDeactivateCycles) {
    ParticleSystem ps;

    for (int cycle = 0; cycle < 10; ++cycle) {
        for (uint32_t i = 0; i < 50; ++i) ps.activate();
        ps.trimToLimit(0);
    }

    EXPECT_EQ(ps.capacity(), kMaxParticles);
    EXPECT_EQ(ps.activeCount(), 0u);
}

// 7. Particles advance position when updated
TEST(ParticleSystemTest, ParticlesAdvancePositionOnUpdate) {
    ParticleSystem ps;
    auto* p = ps.activate();
    ASSERT_NE(p, nullptr);
    p->x  = 0.0f;
    p->y  = 0.0f;
    p->vx = 0.5f;
    p->vy = 0.5f;
    p->life = 1.0f;

    auto budget = makeBudget(10, /*complex=*/false);
    ps.update(budget, 0.1f);

    // After 0.1s at velocity 0.5: x ≈ 0.05, y ≈ 0.05
    EXPECT_NEAR(ps.data()[0].x, 0.05f, 0.001f);
    EXPECT_NEAR(ps.data()[0].y, 0.05f, 0.001f);
}

// 8. Zero or negative dt does not advance particles
TEST(ParticleSystemTest, ZeroOrNegativeDtDoesNotAdvance) {
    ParticleSystem ps;
    auto* p = ps.activate();
    ASSERT_NE(p, nullptr);
    p->x = 0.3f; p->y = 0.4f;
    p->vx = 1.0f; p->vy = 1.0f;
    p->life = 1.0f;

    auto budget = makeBudget(10);
    ps.update(budget, 0.0f);
    EXPECT_FLOAT_EQ(ps.data()[0].x, 0.3f);

    ps.update(budget, -1.0f);
    EXPECT_FLOAT_EQ(ps.data()[0].x, 0.3f);
}

// 9. Particles with expired life are deactivated during update
TEST(ParticleSystemTest, ExpiredParticlesDeactivatedDuringUpdate) {
    ParticleSystem ps;
    auto* p = ps.activate();
    ASSERT_NE(p, nullptr);
    p->life = 0.001f; // nearly dead
    p->vx = 0.0f; p->vy = 0.0f;

    auto budget = makeBudget(10);
    // 1.0s dt decays life by 0.1 — this particle should die
    ps.update(budget, 1.0f);

    EXPECT_EQ(ps.activeCount(), 0u);
}

// 10. deactivate() out-of-range index is ignored safely
TEST(ParticleSystemTest, DeactivateOutOfRangeIsSafe) {
    ParticleSystem ps;
    ps.activate();
    EXPECT_EQ(ps.activeCount(), 1u);

    // Deactivate index beyond activeCount: must not crash or corrupt state
    ps.deactivate(5);
    EXPECT_EQ(ps.activeCount(), 1u);

    ps.deactivate(100);
    EXPECT_EQ(ps.activeCount(), 1u);
}

// 11. kMaxParticles matches HARD_MAX_PARTICLES (compile-time contract check)
TEST(ParticleSystemTest, CapacityMatchesHardLimit) {
    static_assert(kMaxParticles == ComputeBudget::HARD_MAX_PARTICLES,
        "ParticleSystem capacity must equal ComputeBudget::HARD_MAX_PARTICLES");
    EXPECT_EQ(kMaxParticles, ComputeBudget::HARD_MAX_PARTICLES);
}
