#include "prometheus/ui/OrganismViewModel.hpp"
#include <QMetaType>
#include <algorithm>
#include <cmath>

Q_DECLARE_METATYPE(std::shared_ptr<const prometheus::common::OrganismSnapshot>)

namespace prometheus::ui {

namespace {

QString behaviourToString(common::Behaviour b) noexcept {
    switch (b) {
        case common::Behaviour::Dormant:   return QStringLiteral("Dormant");
        case common::Behaviour::Resting:   return QStringLiteral("Resting");
        case common::Behaviour::Idle:      return QStringLiteral("Idle");
        case common::Behaviour::Observing: return QStringLiteral("Observing");
        case common::Behaviour::Exploring: return QStringLiteral("Exploring");
        case common::Behaviour::Stressed:  return QStringLiteral("Stressed");
        default:                           return QStringLiteral("Unknown");
    }
}

/**
 * Maps Behaviour + QualityTier to a fine-grained animation state string.
 *
 * Inspired by Mate-Engine's AvatarAnimatorController.cs which maps character
 * FSM states (idle, dance, drag) to animator parameter strings. Here we
 * combine PROMETHEUS's Behaviour enum with the QualityTier budget to give
 * the QML AnimationLayer a single, unambiguous animation key to switch on.
 *
 * Keys: "dormant" | "rest_deep" | "rest_light" | "idle" | "alert"
 *       "explore_full" | "explore_lite" | "stressed_critical" | "stressed_mild"
 */
QString computeAnimState(common::Behaviour b, common::QualityTier tier) noexcept {
    const int t = static_cast<int>(tier);
    switch (b) {
        case common::Behaviour::Dormant:
            return QStringLiteral("dormant");
        case common::Behaviour::Resting:
            return (t <= 1)
                ? QStringLiteral("rest_deep")
                : QStringLiteral("rest_light");
        case common::Behaviour::Idle:
            return QStringLiteral("idle");
        case common::Behaviour::Observing:
            return QStringLiteral("alert");
        case common::Behaviour::Exploring:
            return (t >= 3)
                ? QStringLiteral("explore_full")
                : QStringLiteral("explore_lite");
        case common::Behaviour::Stressed:
            return (t <= 1)
                ? QStringLiteral("stressed_critical")
                : QStringLiteral("stressed_mild");
        default:
            return QStringLiteral("idle");
    }
}

/**
 * Sanitizes a scalar into [0.0f, 1.0f]; non-finite values fall back to 0.0f.
 */
float sanitizeUnitScalar(float value) noexcept {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

/**
 * Computes animPulse: a view-layer spike detector for host pressure changes.
 *
 * Mate-Engine uses audio BPM peaks to trigger dance bursts. PROMETHEUS uses
 * host CPU pressure spikes to trigger "flinch" reactions in the QML layer.
 *
 * Combines instantaneous tick delta (catches step jumps at low tick rates or
 * in unit tests) with rise above a trailing baseline (catches rapid ramps
 * that are EMA-smoothed at 30 Hz in SimulationThread).
 * This is entirely presentational — no OrganismState or contract is involved.
 */
float computeAnimPulse(float currentPressure,
                       float prevPressure,
                       float trailingPressure) noexcept {
    const float tickSpike = std::max(0.0f, currentPressure - prevPressure) * 5.0f;
    const float baselineRise = std::max(0.0f, currentPressure - trailingPressure) * 4.0f;
    return std::clamp(std::max(tickSpike, baselineRise), 0.0f, 1.0f);
}

} // namespace

OrganismViewModel::OrganismViewModel(QObject* parent)
    : QObject(parent)
    , snapshot_(std::make_shared<const common::OrganismSnapshot>())
{
    qRegisterMetaType<std::shared_ptr<const common::OrganismSnapshot>>();
}

void OrganismViewModel::onSnapshotPublished(
    std::shared_ptr<const common::OrganismSnapshot> snapshot)
{
    Q_ASSERT(snapshot != nullptr);

    const float currentPressure = sanitizeUnitScalar(snapshot->environmental_pressure);
    const bool isDormant = snapshot->dormant ||
        (snapshot->state.current_behaviour == common::Behaviour::Dormant);

    if (!hasBaseline_) {
        // Seed baseline on first snapshot so startup pressure never triggers a false flinch.
        hasBaseline_ = true;
        prevPressure_ = currentPressure;
        trailingPressure_ = currentPressure;
        animPulse_ = 0.0f;
    } else if (isDormant) {
        animPulse_ = 0.0f;
        prevPressure_ = currentPressure;
        trailingPressure_ = currentPressure;
    } else {
        animPulse_ = computeAnimPulse(currentPressure, prevPressure_, trailingPressure_);
        prevPressure_ = currentPressure;
        // Asymmetric trailing filter: tracks upward slowly (~0.4s at 30Hz) so spikes
        // register, and relaxes downward faster so subsequent spikes are not masked.
        constexpr float kTrailAlphaUp = 0.08f;   // initial, to be tuned
        constexpr float kTrailAlphaDown = 0.25f; // initial, to be tuned
        const float alpha = (currentPressure >= trailingPressure_) ? kTrailAlphaUp : kTrailAlphaDown;
        trailingPressure_ += alpha * (currentPressure - trailingPressure_);
    }

    snapshot_ = std::move(snapshot);
    emit snapshotChanged();
}

// ---- Drives ----
float OrganismViewModel::energy()       const noexcept { return snapshot_->state.energy; }
float OrganismViewModel::stress()       const noexcept { return snapshot_->state.stress; }
float OrganismViewModel::health()       const noexcept { return snapshot_->state.health; }
float OrganismViewModel::curiosity()    const noexcept { return snapshot_->state.curiosity; }
float OrganismViewModel::adaptation()   const noexcept { return snapshot_->state.adaptation; }
float OrganismViewModel::pressure()     const noexcept { return snapshot_->environmental_pressure; }

// ---- Behaviour ----
QString OrganismViewModel::behaviour() const noexcept {
    return behaviourToString(snapshot_->state.current_behaviour);
}

// ---- Budget / tier ----
int OrganismViewModel::qualityTier() const noexcept {
    return static_cast<int>(snapshot_->budget.tier);
}

// ---- Simulation metrics ----
int   OrganismViewModel::activeParticles() const noexcept { return static_cast<int>(snapshot_->active_particles); }
float OrganismViewModel::measuredSimHz()   const noexcept { return snapshot_->measured_sim_hz; }
float OrganismViewModel::simTimeSeconds()  const noexcept { return snapshot_->sim_time_seconds; }

// ---- Host telemetry ----
float OrganismViewModel::hostCpuPct()   const noexcept { return snapshot_->observation.cpu_load * 100.0f; }
float OrganismViewModel::hostRamPct()   const noexcept { return snapshot_->observation.memory_pressure * 100.0f; }
bool  OrganismViewModel::hostCpuValid() const noexcept { return snapshot_->observation.cpu_valid; }
bool  OrganismViewModel::hostRamValid() const noexcept { return snapshot_->observation.memory_valid; }

// ---- Flags ----
bool OrganismViewModel::syntheticMode() const noexcept {
    return snapshot_->telemetry_source == common::TelemetrySource::Synthetic;
}
bool OrganismViewModel::dormant() const noexcept { return snapshot_->dormant; }

// ---- Animation state (view-layer derived) ----
QString OrganismViewModel::animState() const noexcept {
    return computeAnimState(snapshot_->state.current_behaviour, snapshot_->budget.tier);
}

float OrganismViewModel::animPulse() const noexcept {
    return animPulse_;
}

} // namespace prometheus::ui
