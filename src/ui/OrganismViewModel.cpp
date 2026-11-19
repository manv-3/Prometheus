#include "prometheus/ui/OrganismViewModel.hpp"
#include <QMetaType>

// Register shared_ptr<const OrganismSnapshot> for queued connections across threads.
// This must be done before the first queued signal using this type fires.
Q_DECLARE_METATYPE(std::shared_ptr<const prometheus::common::OrganismSnapshot>)

namespace prometheus::ui {

namespace {

// Convert Behaviour enum to a display string for QML bindings.
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
    // Must only be called on the GUI thread (enforced by queued connection).
    Q_ASSERT(snapshot != nullptr);
    snapshot_ = std::move(snapshot);
    emit snapshotChanged();
}

// ---- Property readers ----
// All reads go through snapshot_ which is only written on the GUI thread,
// so no lock is needed here.

float OrganismViewModel::energy() const noexcept {
    return snapshot_->state.energy;
}

float OrganismViewModel::stress() const noexcept {
    return snapshot_->state.stress;
}

float OrganismViewModel::health() const noexcept {
    return snapshot_->state.health;
}

float OrganismViewModel::curiosity() const noexcept {
    return snapshot_->state.curiosity;
}

float OrganismViewModel::adaptation() const noexcept {
    return snapshot_->state.adaptation;
}

float OrganismViewModel::pressure() const noexcept {
    return snapshot_->environmental_pressure;
}

QString OrganismViewModel::behaviour() const noexcept {
    return behaviourToString(snapshot_->state.current_behaviour);
}

int OrganismViewModel::qualityTier() const noexcept {
    return static_cast<int>(snapshot_->budget.tier);
}

int OrganismViewModel::activeParticles() const noexcept {
    return static_cast<int>(snapshot_->active_particles);
}

bool OrganismViewModel::syntheticMode() const noexcept {
    return snapshot_->telemetry_source == common::TelemetrySource::Synthetic;
}

bool OrganismViewModel::dormant() const noexcept {
    return snapshot_->dormant;
}

} // namespace prometheus::ui
