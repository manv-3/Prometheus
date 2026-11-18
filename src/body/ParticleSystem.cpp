#include "prometheus/body/ParticleSystem.hpp"
#include <algorithm>
#include <cmath>

namespace prometheus::body {

ParticleSystem::ParticleSystem() noexcept {
    // Pool is value-initialised by std::array default constructor.
    // All particles start inactive (Particle::active == false, activeCount_ == 0).
    static_assert(kMaxParticles == common::ComputeBudget::HARD_MAX_PARTICLES,
        "ParticleSystem capacity must equal ComputeBudget::HARD_MAX_PARTICLES");
}

// ---- activate / deactivate ------------------------------------------------

Particle* ParticleSystem::activate() noexcept {
    if (activeCount_ >= kMaxParticles) {
        return nullptr; // Pool full — caller must trim first
    }
    Particle& p = pool_[activeCount_];
    p = Particle{};        // reset to defaults
    p.active = true;
    ++activeCount_;
    return &p;
}

void ParticleSystem::deactivate(uint32_t index) noexcept {
    if (index >= activeCount_) return;

    // Swap-with-last: O(1), preserves the compact active prefix
    if (index != activeCount_ - 1) {
        pool_[index] = pool_[activeCount_ - 1];
    }
    pool_[activeCount_ - 1].active = false;
    --activeCount_;
}

void ParticleSystem::trimToLimit(uint32_t limit) noexcept {
    while (activeCount_ > limit) {
        deactivate(activeCount_ - 1);
    }
}

// ---- update ---------------------------------------------------------------

void ParticleSystem::update(const common::ComputeBudget& budget, float dt) noexcept {
    // 1. Enforce particle limit immediately (REQ-BODY-01)
    trimToLimit(budget.max_particles);

    if (dt <= 0.0f || activeCount_ == 0) return;

    // Cap dt to prevent large jumps after pauses (matches HomeostasisEngine convention)
    const float safe_dt = std::min(dt, 0.5f);

    // 2. Update active particles [0, activeCount_)
    // Simple Euler integration — cheap, deterministic, budget-appropriate.
    // Complex physics only when budget permits.
    const bool complex = budget.enable_complex_physics;

    uint32_t i = 0;
    while (i < activeCount_) {
        Particle& p = pool_[i];

        // Advance life
        p.life -= safe_dt * 0.1f; // initial decay rate — to be tuned

        if (p.life <= 0.0f) {
            deactivate(i);
            // Don't increment i — the swapped-in particle now occupies slot i
            continue;
        }

        // Euler position step
        p.x += p.vx * safe_dt;
        p.y += p.vy * safe_dt;

        // Soft boundary: reflect at ±1 (only when complex physics enabled)
        if (complex) {
            if (p.x >  1.0f) { p.x =  1.0f; p.vx = -p.vx * 0.8f; }
            if (p.x < -1.0f) { p.x = -1.0f; p.vx = -p.vx * 0.8f; }
            if (p.y >  1.0f) { p.y =  1.0f; p.vy = -p.vy * 0.8f; }
            if (p.y < -1.0f) { p.y = -1.0f; p.vy = -p.vy * 0.8f; }
        } else {
            // Cheap wrap instead of reflect
            if (p.x >  1.0f) p.x -= 2.0f;
            if (p.x < -1.0f) p.x += 2.0f;
            if (p.y >  1.0f) p.y -= 2.0f;
            if (p.y < -1.0f) p.y += 2.0f;
        }

        ++i;
    }
}

} // namespace prometheus::body
