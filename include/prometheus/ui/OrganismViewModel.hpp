#pragma once

#include "prometheus/common/OrganismSnapshot.hpp"
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <memory>

namespace prometheus::ui {

/**
 * @brief Thin QObject ViewModel between the simulation and QML.
 *
 * Receives immutable OrganismSnapshots from the simulation thread via a
 * queued signal (never a direct call — REQ-UI-01). Exposes read-only
 * Q_PROPERTYs to QML. QML never touches simulation objects or Windows handles.
 */
class OrganismViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // ---- Core drives [0.0, 1.0] ----
    Q_PROPERTY(float energy        READ energy        NOTIFY snapshotChanged)
    Q_PROPERTY(float stress        READ stress        NOTIFY snapshotChanged)
    Q_PROPERTY(float health        READ health        NOTIFY snapshotChanged)
    Q_PROPERTY(float curiosity     READ curiosity     NOTIFY snapshotChanged)
    Q_PROPERTY(float adaptation    READ adaptation    NOTIFY snapshotChanged)

    // ---- Host pressure [0.0, 1.0] ----
    Q_PROPERTY(float pressure      READ pressure      NOTIFY snapshotChanged)

    // ---- Behaviour string ----
    Q_PROPERTY(QString behaviour   READ behaviour     NOTIFY snapshotChanged)

    // ---- Quality tier (int 0–4) ----
    Q_PROPERTY(int qualityTier     READ qualityTier   NOTIFY snapshotChanged)

    // ---- Simulation metrics ----
    Q_PROPERTY(int   activeParticles READ activeParticles NOTIFY snapshotChanged)
    Q_PROPERTY(float measuredSimHz   READ measuredSimHz   NOTIFY snapshotChanged)
    Q_PROPERTY(float simTimeSeconds  READ simTimeSeconds  NOTIFY snapshotChanged)

    // ---- Host telemetry (for observer panel) ----
    Q_PROPERTY(float hostCpuPct    READ hostCpuPct    NOTIFY snapshotChanged)
    Q_PROPERTY(float hostRamPct    READ hostRamPct    NOTIFY snapshotChanged)
    Q_PROPERTY(bool  hostCpuValid  READ hostCpuValid  NOTIFY snapshotChanged)
    Q_PROPERTY(bool  hostRamValid  READ hostRamValid  NOTIFY snapshotChanged)

    // ---- Flags ----
    Q_PROPERTY(bool syntheticMode  READ syntheticMode NOTIFY snapshotChanged)
    Q_PROPERTY(bool dormant        READ dormant       NOTIFY snapshotChanged)

public:
    explicit OrganismViewModel(QObject* parent = nullptr);

    [[nodiscard]] float   energy()          const noexcept;
    [[nodiscard]] float   stress()          const noexcept;
    [[nodiscard]] float   health()          const noexcept;
    [[nodiscard]] float   curiosity()       const noexcept;
    [[nodiscard]] float   adaptation()      const noexcept;
    [[nodiscard]] float   pressure()        const noexcept;
    [[nodiscard]] QString behaviour()       const noexcept;
    [[nodiscard]] int     qualityTier()     const noexcept;
    [[nodiscard]] int     activeParticles() const noexcept;
    [[nodiscard]] float   measuredSimHz()   const noexcept;
    [[nodiscard]] float   simTimeSeconds()  const noexcept;
    [[nodiscard]] float   hostCpuPct()      const noexcept;
    [[nodiscard]] float   hostRamPct()      const noexcept;
    [[nodiscard]] bool    hostCpuValid()    const noexcept;
    [[nodiscard]] bool    hostRamValid()    const noexcept;
    [[nodiscard]] bool    syntheticMode()   const noexcept;
    [[nodiscard]] bool    dormant()         const noexcept;

public slots:
    void onSnapshotPublished(std::shared_ptr<const common::OrganismSnapshot> snapshot);

signals:
    void snapshotChanged();

private:
    std::shared_ptr<const common::OrganismSnapshot> snapshot_;
};

} // namespace prometheus::ui
