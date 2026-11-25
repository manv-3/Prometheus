#pragma once

#include "prometheus/common/OrganismSnapshot.hpp"
#include <QObject>
#include <QString>
#include <QVariantList>
#include <memory>

namespace prometheus::ui {

/**
 * @brief Thin QObject ViewModel between the simulation and QML.
 *
 * Receives immutable OrganismSnapshots from the simulation thread via a
 * queued signal (never a direct call — REQ-UI-01). Exposes read-only
 * Q_PROPERTYs to QML. QML never touches simulation objects or Windows handles.
 *
 * Animation properties (animState, animPulse) and character selection
 * (characterId, characterSource, availableCharacters) are view-layer state
 * inside the UI module. They do NOT affect OrganismState or any simulation
 * contract — they are purely presentational.
 */
class OrganismViewModel : public QObject {
    Q_OBJECT

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

    // ---- Animation state (view-layer derived — no simulation contract) ----
    // animState: fine-grained animation key combining Behaviour + QualityTier.
    //   Possible values: "dormant", "rest_deep", "rest_light", "idle",
    //                    "alert", "explore_full", "explore_lite",
    //                    "stressed_critical", "stressed_mild"
    // Analogous to Mate-Engine's AvatarAnimatorController state names.
    Q_PROPERTY(QString animState   READ animState     NOTIFY snapshotChanged)

    // animPulse: magnitude of recent host-pressure spike [0.0, 1.0].
    // Computed from upward pressure delta and rise above a trailing baseline,
    // clamped to [0, 1] and suppressed while dormant.
    // QML uses this to drive "flinch" reactions — analogous to Mate-Engine's
    // audio-reactive dance mode (BPM spike → dance, pressure spike → flinch).
    Q_PROPERTY(float animPulse     READ animPulse     NOTIFY snapshotChanged)

    // ---- Character / avatar selection (UI presentation only) ----
    Q_PROPERTY(QString      characterId         READ characterId         NOTIFY characterChanged)
    Q_PROPERTY(QString      characterName       READ characterName       NOTIFY characterChanged)
    Q_PROPERTY(QString      characterSource     READ characterSource     NOTIFY characterChanged)
    Q_PROPERTY(QString      characterAccent       READ characterAccent       NOTIFY characterChanged)
    Q_PROPERTY(QString      customCharacterPath   READ customCharacterPath   NOTIFY characterChanged)
    Q_PROPERTY(QString      customCharacterSource READ customCharacterSource NOTIFY characterChanged)
    Q_PROPERTY(QVariantList availableCharacters   READ availableCharacters   CONSTANT)

public:
    explicit OrganismViewModel(QObject* parent = nullptr);
    OrganismViewModel(bool enablePersistence,
                      const QString& settingsFilePath,
                      QObject* parent = nullptr);

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
    [[nodiscard]] QString animState()       const noexcept;
    [[nodiscard]] float   animPulse()       const noexcept;

    [[nodiscard]] QString      characterId()         const noexcept;
    [[nodiscard]] QString      characterName()       const;
    [[nodiscard]] QString      characterSource()     const;
    [[nodiscard]] QString      characterAccent()       const;
    [[nodiscard]] QString      customCharacterPath()   const noexcept;
    [[nodiscard]] QString      customCharacterSource() const;
    [[nodiscard]] QVariantList availableCharacters()   const;

    Q_INVOKABLE bool selectCharacter(const QString& characterId);
    Q_INVOKABLE bool selectCustomCharacter(const QString& filePathOrUrl);

public slots:
    void onSnapshotPublished(std::shared_ptr<const common::OrganismSnapshot> snapshot);

signals:
    void snapshotChanged();
    void characterChanged();

private:
    void loadCharacterPreference();
    void saveCharacterPreference() const;

    std::shared_ptr<const common::OrganismSnapshot> snapshot_;
    bool  hasBaseline_{false};     // avoids false startup pulse on first snapshot
    float prevPressure_{0.0f};     // previous sanitized pressure for step-jump detection
    float trailingPressure_{0.0f}; // slower trailing baseline for smoothed 30 Hz spikes
    float animPulse_{0.0f};        // cached pulse value updated on each snapshot

    bool    persistenceEnabled_{true};
    QString settingsFilePath_;
    QString characterId_{QStringLiteral("cat")};
    QString customCharacterPath_;
};

} // namespace prometheus::ui
