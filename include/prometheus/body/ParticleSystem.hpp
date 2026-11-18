#pragma once

#include "prometheus/common/ComputeBudget.hpp"
#include <array>
#include <cstdint>
#include <cmath>

namespace prometheus::body {

/// Hard capacity ceiling — matches ComputeBudget::HARD_MAX_PARTICLES.
/// Allocated once at startup; never changes (REQ-BODY-01).
static constexpr uint32_t kMaxParticles = common::ComputeBudget::HARD_MAX_PARTICLES;

/**
 * @brief A single particle in the fixed-capacity pool.
 *
 * Position and velocity use normalised coordinates [-1, 1].
 * All fields are plain data — no virtual functions, no heap allocation.
 */
struct Particle {
    float x{0.0f}, y{0.0f};        // position  [-1, 1]
    float vx{0.0f}, vy{0.0f};      // velocity  [-1, 1] per second
    float life{1.0f};              // remaining life [0, 1]; 0 = deactivate
    float size{1.0f};              // visual size hint [0, 1]
    bool  active{false};
};

/**
 * @brief Fixed-capacity particle pool (TASK-1.7, REQ-BODY-01).
 *
 * Invariants:
 * - Capacity is kMaxParticles, allocated once on construction.
 * - activeCount() never exceeds the capacity or the current budget limit.
 * - activate()/deactivate() use swap-with-last: O(1), no heap allocation.
 * - update() processes only [0, activeCount()) — dormant slots are never touched.
 * - No per-frame allocation of any kind.
 */
class ParticleSystem {
public:
    ParticleSystem() noexcept;

    // Non-copyable (owns a large fixed array; copies would be expensive and wrong)
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    /**
     * @brief Advance all active particles by dt seconds within the given budget.
     *
     * If activeCount() > budget.max_particles, excess particles are deactivated
     * before the update step (REQ-BODY-01 enforcement).
     *
     * Must be called from the simulation thread only. Never called from render.
     *
     * @param budget  Current ComputeBudget (reads max_particles, detail flags).
     * @param dt      Elapsed simulation time in seconds (steady_clock, >0).
     */
    void update(const common::ComputeBudget& budget, float dt) noexcept;

    /**
     * @brief Activate the next available dormant particle.
     * @return Pointer to the activated Particle, or nullptr if pool is full.
     */
    Particle* activate() noexcept;

    /**
     * @brief Deactivate the particle at index i using swap-with-last (O(1)).
     * @param index Must be < activeCount().
     */
    void deactivate(uint32_t index) noexcept;

    /**
     * @brief Deactivate particles until activeCount() <= limit.
     * Deactivates from the tail (highest indices) downward.
     */
    void trimToLimit(uint32_t limit) noexcept;

    // ---- Inspection (read-only; safe to call from any thread for snapshot) ----
    [[nodiscard]] uint32_t activeCount()   const noexcept { return activeCount_; }
    [[nodiscard]] uint32_t capacity()      const noexcept { return kMaxParticles; }

    /// Read-only view of the active range [0, activeCount()).
    [[nodiscard]] const Particle* data()   const noexcept { return pool_.data(); }

private:
    std::array<Particle, kMaxParticles> pool_{};
    uint32_t activeCount_{0};
};

} // namespace prometheus::body
