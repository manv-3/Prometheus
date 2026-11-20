#pragma once

#include "prometheus/common/HostObservation.hpp"
#include "prometheus/common/OrganismState.hpp"
#include "prometheus/common/ComputeBudget.hpp"
#include <algorithm>
#include <optional>

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
 * @brief Configurable pacing and pressure-cap thresholds.
 * All thresholds are initial, untuned placeholders.
 */
struct MetabolismConfig {
    /// Seconds the organism must remain in a tier before upgrading one level.
    /// Degrade (downgrade) is always immediate; only recovery is paced.
    float tierRestoreIntervalSeconds{3.0f};

    /// Pressure thresholds that cap the maximum tier regardless of behaviour.
    /// Defence-in-depth: even if the FSM hasn't transitioned yet, high pressure
    /// prevents the Metabolism from issuing a high-tier budget.
    float pressureCapTier4{0.30f};  // pressure above this → max Tier3
    float pressureCapTier3{0.50f};  // pressure above this → max Tier2
    float pressureCapTier2{0.70f};  // pressure above this → max Tier1
    float pressureCapTier1{0.85f};  // pressure above this → max Tier0
};

/**
 * @brief Converts (Behaviour, smoothedPressure) → ComputeBudget.
 *
 * Rules (REQ-BUDGET-01 – REQ-BUDGET-03):
 *   - Returned budget never exceeds HardLimits (enforced last, unconditionally).
 *   - Degradation is immediate; restoration is one tier per dwell period.
 *   - Pressure provides a secondary cap: high pressure lowers the max tier
 *     even if the FSM hasn't transitioned yet.
 *
 * Fixed-complexity mode (TASK-4.2 / REQ-BENCH-01):
 *   When a fixed tier is set via setFixedTier(), calculate() ignores Behaviour
 *   and pressure and always returns the budget for that tier.
 *   This provides the baseline for the adaptive vs fixed benchmark comparison.
 *
 * No Qt headers, no Windows headers. Depends only on shared types.
 */
class Metabolism {
public:
    explicit Metabolism(
        const TierBudget& tiers = defaultTiers(),
        const MetabolismConfig& config = MetabolismConfig{}
    ) noexcept;

    /**
     * @brief Calculate the ComputeBudget for the current organism state.
     *        Updates internal tier tracker for asymmetric pacing.
     *
     * @param host             Latest host observation (for pressure cross-check).
     * @param organism         Current organism state (reads Behaviour).
     * @param smoothedPressure EMA-filtered pressure [0,1] from PressureEstimator.
     * @param dt               Elapsed time in seconds since last call.
     * @return Hard-clamped ComputeBudget.
     */
    [[nodiscard]] common::ComputeBudget calculate(
        const common::HostObservation& host,
        const common::OrganismState& organism,
        float smoothedPressure,
        float dt
    ) noexcept;

    [[nodiscard]] common::QualityTier currentTier() const noexcept { return currentTier_; }
    [[nodiscard]] float secondsInTier() const noexcept { return secondsInTier_; }

    /**
     * @brief Fix the metabolism to a constant tier (TASK-4.2).
     *        When set, calculate() bypasses all adaptive logic and always
     *        returns the budget for this tier, clamped to HardLimits.
     *        Pass std::nullopt to restore adaptive mode.
     */
    void setFixedTier(std::optional<common::QualityTier> tier) noexcept;
    [[nodiscard]] std::optional<common::QualityTier> fixedTier() const noexcept { return fixedTier_; }
    [[nodiscard]] bool isFixedMode() const noexcept { return fixedTier_.has_value(); }

    static TierBudget defaultTiers() noexcept;

private:
    [[nodiscard]] static common::QualityTier behaviourToTier(
        common::Behaviour behaviour) noexcept;

    [[nodiscard]] common::QualityTier pressureCapTier(
        float smoothedPressure) const noexcept;

    [[nodiscard]] static common::ComputeBudget clampToHardLimits(
        const common::ComputeBudget& proposed) noexcept;

    [[nodiscard]] const common::ComputeBudget& tierToBudget(
        common::QualityTier tier) const noexcept;

    TierBudget tiers_;
    MetabolismConfig config_;
    common::QualityTier currentTier_{common::QualityTier::Tier3_Nominal};
    float secondsInTier_{0.0f};
    std::optional<common::QualityTier> fixedTier_; // nullopt = adaptive mode
};

} // namespace prometheus::metabolism
