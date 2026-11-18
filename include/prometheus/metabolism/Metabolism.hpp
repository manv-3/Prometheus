#pragma once

#include "prometheus/common/HostObservation.hpp"
#include "prometheus/common/OrganismState.hpp"
#include "prometheus/common/ComputeBudget.hpp"

namespace prometheus::metabolism {

/**
 * @brief Budget table for each QualityTier.
 * All values are initial, untuned placeholders — tune from measurements.
 */
struct TierBudget {
    common::ComputeBudget dormant;
    common::ComputeBudget survival;
    common::ComputeBudget reduced;
    common::ComputeBudget nominal;
    common::ComputeBudget optimal;
};

/**
 * @brief Converts (Behaviour, smoothedPressure) → ComputeBudget.
 *
 * Rules:
 *   - Returned budget never exceeds HardLimits (enforced last, unconditionally).
 *   - Degradation is immediate; restoration is one tier per dwell period (REQ-BUDGET-03).
 *   - Pure function of inputs + current tier (tracked internally for pacing).
 *
 * No Qt headers, no Windows headers. Depends only on shared types.
 *
 * STATUS: STUB — owned by Dev B (TASK-2.2).
 */
class Metabolism {
public:
    explicit Metabolism(const TierBudget& tiers = defaultTiers()) noexcept;

    /**
     * @brief Calculate the ComputeBudget for the current organism state.
     *        Updates internal tier tracker for pacing.
     *
     * @param host     Latest host observation (used for pressure cross-check).
     * @param organism Current organism state (reads Behaviour, smoothedPressure).
     * @return Hard-clamped ComputeBudget.
     */
    [[nodiscard]] common::ComputeBudget calculate(
        const common::HostObservation& host,
        const common::OrganismState& organism
    ) noexcept;

    [[nodiscard]] common::QualityTier currentTier() const noexcept { return currentTier_; }

    static TierBudget defaultTiers() noexcept;

private:
    [[nodiscard]] static common::ComputeBudget clampToHardLimits(
        const common::ComputeBudget& proposed) noexcept;

    TierBudget tiers_;
    common::QualityTier currentTier_{common::QualityTier::Tier3_Nominal};
    float secondsInTier_{0.0f};
};

} // namespace prometheus::metabolism
