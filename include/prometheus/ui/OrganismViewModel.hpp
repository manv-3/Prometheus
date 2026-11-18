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
 *
 * All properties are derived from the snapshot at the moment it arrives;
 * there are no setters that modify simulation state.
 *
 * TASK-1.6: initial properties sufficient for the placeholder visual.
 * TASK-2.5 / Phase 4: add observer-panel properties.
 */
class OrganismViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // ---- Core drives [0.0, 1.0] ----
    Q_PROPERTY(float energy       READ energy       NOTIFY snapshotChanged)
    Q_PROPERTY(float stress       READ stress       NOTIFY snapshotChanged)
    Q_PROPERTY(float health       READ health       NOTIFY snapshotChanged)
    Q_PROPERTY(float curiosity    READ curiosity    NOTIFY snapshotChanged)
    Q_PROPERTY(float adaptation   READ adaptation   NOTIFY snapshotChanged)

    // ---- Host pressure [0.0, 1.0] ----
    Q_PROPERTY(float pressure     READ pressure     NOTIFY snapshotChanged)

    // ---- Behaviour (string for QML binding) ----
    Q_PROPERTY(QString behaviour  READ behaviour    NOTIFY snapshotChanged)

    // ---- Quality tier (int 0–4) ----
    Q_PROPERTY(int qualityTier    READ qualityTier  NOTIFY snapshotChanged)

    // ---- Particle count ----
    Q_PROPERTY(int activeParticles READ activeParticles NOTIFY snapshotChanged)

    // ---- Synthetic telemetry indicator (REQ-UI-02) ----
    Q_PROPERTY(bool syntheticMode READ syntheticMode NOTIFY snapshotChanged)

    // ---- Dormancy flag ----
    Q_PROPERTY(bool dormant       READ dormant      NOTIFY snapshotChanged)

public:
    explicit OrganismViewModel(QObject* parent = nullptr);

    // Property readers — all derived from the latest snapshot
    [[nodiscard]] float   energy()         const noexcept;
    [[nodiscard]] float   stress()         const noexcept;
    [[nodiscard]] float   health()         const noexcept;
    [[nodiscard]] float   curiosity()      const noexcept;
    [[nodiscard]] float   adaptation()     const noexcept;
    [[nodiscard]] float   pressure()       const noexcept;
    [[nodiscard]] QString behaviour()      const noexcept;
    [[nodiscard]] int     qualityTier()    const noexcept;
    [[nodiscard]] int     activeParticles() const noexcept;
    [[nodiscard]] bool    syntheticMode()  const noexcept;
    [[nodiscard]] bool    dormant()        const noexcept;

public slots:
    /// Receives a new snapshot from the simulation thread (queued connection).
    /// The slot must be called on the GUI thread only.
    void onSnapshotPublished(std::shared_ptr<const common::OrganismSnapshot> snapshot);

signals:
    void snapshotChanged();

private:
    std::shared_ptr<const common::OrganismSnapshot> snapshot_;
};

} // namespace prometheus::ui
