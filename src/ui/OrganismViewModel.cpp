#include "prometheus/ui/OrganismViewModel.hpp"
#include <QMetaType>

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

} // namespace prometheus::ui
