#include "prometheus/ui/OrganismViewModel.hpp"
#include <QCursor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaType>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

Q_DECLARE_METATYPE(std::shared_ptr<const prometheus::common::OrganismSnapshot>)

namespace prometheus::ui {

namespace {

struct CharacterPreset {
    const char* id;
    const char* name;
    const char* tagline;
    const char* source;
    const char* accent;
};

constexpr std::array<CharacterPreset, 5> kBuiltInCharacters{{
    {"cat",   "Ember Cat",  "Classic solar feline",   "qrc:/Prometheus/resources/organism.svg",            "#ff8844"},
    {"owl",   "Aegis Owl",  "Nocturnal sentinel",     "qrc:/Prometheus/resources/characters/owl.svg",      "#ffd55a"},
    {"fox",   "Spark Fox",  "Agile ember spirit",     "qrc:/Prometheus/resources/characters/fox.svg",      "#ff6b2b"},
    {"bot",   "Atlas Bot",  "Cyber telemetry unit",   "qrc:/Prometheus/resources/characters/bot.svg",      "#33d6ff"},
    {"slime", "Nova Slime", "Bioluminescent cell",    "qrc:/Prometheus/resources/characters/slime.svg",    "#2ee6c5"},
}};

const CharacterPreset* findPreset(const QString& id) noexcept {
    for (const auto& c : kBuiltInCharacters) {
        if (id == QLatin1String(c.id)) {
            return &c;
        }
    }
    return nullptr;
}

bool isValidCustomImageFile(const QString& filePathOrUrl, QString& normalizedLocalPath) {
    QString localPath = filePathOrUrl.trimmed();
    if (localPath.isEmpty()) return false;

    const QUrl url(localPath);
    if (url.isValid() && !url.scheme().isEmpty()) {
        // Only local file:// URLs are permitted (never http/https/etc., and reject single-letter drive schemes).
        if (url.isLocalFile()) {
            localPath = url.toLocalFile();
        } else if (url.scheme().size() > 1) {
            return false;
        }
    }

    const QFileInfo fi(localPath);
    if (!fi.exists() || !fi.isFile()) return false;

    const QString ext = fi.suffix().toLower();
    if (ext != QLatin1String("svg") &&
        ext != QLatin1String("png") &&
        ext != QLatin1String("jpg") &&
        ext != QLatin1String("jpeg") &&
        ext != QLatin1String("webp") &&
        ext != QLatin1String("gif") &&
        ext != QLatin1String("bmp")) {
        return false;
    }

    normalizedLocalPath = fi.canonicalFilePath();
    if (normalizedLocalPath.isEmpty()) {
        normalizedLocalPath = fi.absoluteFilePath();
    }
    return true;
}

std::unique_ptr<QSettings> makeSettings(const QString& settingsFilePath) {
    if (!settingsFilePath.isEmpty()) {
        return std::make_unique<QSettings>(settingsFilePath, QSettings::IniFormat);
    }
    return std::make_unique<QSettings>(
        QStringLiteral("Prometheus"), QStringLiteral("PrometheusDesktop"));
}

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

bool nearlyEqual(float a, float b, float eps = 1e-4f) noexcept {
    return std::abs(a - b) <= eps;
}

bool locomotionOutputsEqual(const LocomotionOutput& a, const LocomotionOutput& b) noexcept {
    return a.mode == b.mode &&
           a.shouldMoveWindow == b.shouldMoveWindow &&
           nearlyEqual(a.targetWindowX, b.targetWindowX, 0.25f) &&
           a.isWalking == b.isWalking &&
           a.facingDir == b.facingDir &&
           nearlyEqual(a.walkPhase, b.walkPhase, 1e-3f) &&
           nearlyEqual(a.walkIntensity, b.walkIntensity, 1e-3f) &&
           nearlyEqual(a.walkSpeedPxPerSec, b.walkSpeedPxPerSec, 0.1f) &&
           nearlyEqual(a.swayRotationDeg, b.swayRotationDeg, 1e-2f) &&
           nearlyEqual(a.swayOffsetX, b.swayOffsetX, 1e-2f) &&
           nearlyEqual(a.swayOffsetY, b.swayOffsetY, 1e-2f) &&
           nearlyEqual(a.gazeLeanDeg, b.gazeLeanDeg, 1e-2f) &&
           nearlyEqual(a.gazeShiftX, b.gazeShiftX, 1e-2f) &&
           a.edgePeekSide == b.edgePeekSide &&
           a.isSitting == b.isSitting &&
           a.needsContinuousTick == b.needsContinuousTick;
}

} // namespace

OrganismViewModel::OrganismViewModel(QObject* parent)
    : OrganismViewModel(true, QString(), parent)
{}

OrganismViewModel::OrganismViewModel(
    bool enablePersistence,
    const QString& settingsFilePath,
    QObject* parent)
    : QObject(parent)
    , snapshot_(std::make_shared<const common::OrganismSnapshot>())
    , persistenceEnabled_(enablePersistence)
    , settingsFilePath_(settingsFilePath)
{
    qRegisterMetaType<std::shared_ptr<const common::OrganismSnapshot>>();
    if (persistenceEnabled_) {
        loadCharacterPreference();
    }
}

void OrganismViewModel::loadCharacterPreference() {
    const auto settings = makeSettings(settingsFilePath_);
    const QString savedId = settings->value(QStringLiteral("ui/characterId"), QStringLiteral("cat")).toString();
    const QString savedCustom = settings->value(QStringLiteral("ui/customCharacterPath")).toString();

    QString normalizedCustom;
    if (!savedCustom.isEmpty() && isValidCustomImageFile(savedCustom, normalizedCustom)) {
        customCharacterPath_ = normalizedCustom;
    }

    if (savedId == QLatin1String("custom") && !customCharacterPath_.isEmpty()) {
        characterId_ = QStringLiteral("custom");
    } else if (findPreset(savedId) != nullptr) {
        characterId_ = savedId;
    } else {
        characterId_ = QStringLiteral("cat");
    }

    locomotionEnabled_ = settings->value(QStringLiteral("ui/locomotionEnabled"), true).toBool();
}

void OrganismViewModel::saveCharacterPreference() const {
    if (!persistenceEnabled_) return;
    const auto settings = makeSettings(settingsFilePath_);
    settings->setValue(QStringLiteral("ui/characterId"), characterId_);
    settings->setValue(QStringLiteral("ui/customCharacterPath"), customCharacterPath_);
    settings->setValue(QStringLiteral("ui/locomotionEnabled"), locomotionEnabled_);
    settings->sync();
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

    bool locomotionDirty = false;
    if (isDormant && (locomotionOut_.isWalking || locomotionOut_.needsContinuousTick || locomotionOut_.walkIntensity > 0.0f)) {
        LocomotionInput resetIn{};
        resetIn.dormant = true;
        resetIn.tier = snapshot_->budget.tier;
        resetIn.behaviour = snapshot_->state.current_behaviour;
        const LocomotionOutput next = locomotion_.step(resetIn);
        locomotionDirty = !locomotionOutputsEqual(locomotionOut_, next);
        locomotionOut_ = next;
    }

    emit snapshotChanged();
    if (locomotionDirty) {
        emit locomotionChanged();
    }
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

int OrganismViewModel::targetRenderHz() const noexcept {
    const uint32_t hz = std::clamp(
        snapshot_->budget.target_render_hz,
        common::ComputeBudget::HARD_MIN_RENDER_HZ,
        common::ComputeBudget::HARD_MAX_RENDER_HZ);
    return static_cast<int>(hz);
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

// ---- Character / avatar selection ----
QString OrganismViewModel::characterId() const noexcept {
    return characterId_;
}

QString OrganismViewModel::characterName() const {
    if (characterId_ == QLatin1String("custom") && !customCharacterPath_.isEmpty()) {
        const QString base = QFileInfo(customCharacterPath_).completeBaseName();
        return base.isEmpty() ? QStringLiteral("Custom") : base;
    }
    if (const auto* preset = findPreset(characterId_)) {
        return QString::fromUtf8(preset->name);
    }
    return QStringLiteral("Ember Cat");
}

QString OrganismViewModel::characterSource() const {
    if (characterId_ == QLatin1String("custom") && !customCharacterPath_.isEmpty()) {
        return QUrl::fromLocalFile(customCharacterPath_).toString();
    }
    if (const auto* preset = findPreset(characterId_)) {
        return QString::fromUtf8(preset->source);
    }
    return QStringLiteral("qrc:/Prometheus/resources/organism.svg");
}

QString OrganismViewModel::characterAccent() const {
    if (characterId_ == QLatin1String("custom")) {
        return QStringLiteral("#c084fc");
    }
    if (const auto* preset = findPreset(characterId_)) {
        return QString::fromUtf8(preset->accent);
    }
    return QStringLiteral("#ff8844");
}

QString OrganismViewModel::customCharacterPath() const noexcept {
    return customCharacterPath_;
}

QString OrganismViewModel::customCharacterSource() const {
    if (customCharacterPath_.isEmpty()) return QString();
    return QUrl::fromLocalFile(customCharacterPath_).toString();
}

QVariantList OrganismViewModel::availableCharacters() const {
    QVariantList list;
    list.reserve(static_cast<qsizetype>(kBuiltInCharacters.size()));
    for (const auto& c : kBuiltInCharacters) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), QString::fromUtf8(c.id));
        entry.insert(QStringLiteral("name"), QString::fromUtf8(c.name));
        entry.insert(QStringLiteral("tagline"), QString::fromUtf8(c.tagline));
        entry.insert(QStringLiteral("source"), QString::fromUtf8(c.source));
        entry.insert(QStringLiteral("accent"), QString::fromUtf8(c.accent));
        list.append(entry);
    }
    return list;
}

bool OrganismViewModel::selectCharacter(const QString& characterId) {
    if (characterId == QLatin1String("custom")) {
        QString normalized;
        if (customCharacterPath_.isEmpty() || !isValidCustomImageFile(customCharacterPath_, normalized)) {
            return false;
        }
        if (characterId_ != QLatin1String("custom") || customCharacterPath_ != normalized) {
            characterId_ = QStringLiteral("custom");
            customCharacterPath_ = normalized;
            saveCharacterPreference();
            emit characterChanged();
        }
        return true;
    }

    if (findPreset(characterId) == nullptr) {
        return false;
    }
    if (characterId_ != characterId) {
        characterId_ = characterId;
        saveCharacterPreference();
        emit characterChanged();
    }
    return true;
}

bool OrganismViewModel::selectCustomCharacter(const QString& filePathOrUrl) {
    QString normalized;
    if (!isValidCustomImageFile(filePathOrUrl, normalized)) {
        return false;
    }
    const bool changed = (characterId_ != QLatin1String("custom")) || (customCharacterPath_ != normalized);
    characterId_ = QStringLiteral("custom");
    customCharacterPath_ = normalized;
    if (changed) {
        saveCharacterPreference();
        emit characterChanged();
    }
    return true;
}

// ---- Locomotion & moving animations ----
bool OrganismViewModel::locomotionEnabled() const noexcept {
    return locomotionEnabled_;
}

QString OrganismViewModel::locomotionMode() const {
    return QString::fromLatin1(locomotionModeToString(locomotionOut_.mode));
}

bool OrganismViewModel::isWalking() const noexcept { return locomotionOut_.isWalking; }
int OrganismViewModel::facingDir() const noexcept { return locomotionOut_.facingDir; }
float OrganismViewModel::walkPhase() const noexcept { return locomotionOut_.walkPhase; }
float OrganismViewModel::walkIntensity() const noexcept { return locomotionOut_.walkIntensity; }
float OrganismViewModel::walkSpeed() const noexcept { return locomotionOut_.walkSpeedPxPerSec; }
float OrganismViewModel::swayRotation() const noexcept { return locomotionOut_.swayRotationDeg; }
float OrganismViewModel::swayOffsetX() const noexcept { return locomotionOut_.swayOffsetX; }
float OrganismViewModel::swayOffsetY() const noexcept { return locomotionOut_.swayOffsetY; }
float OrganismViewModel::gazeLean() const noexcept { return locomotionOut_.gazeLeanDeg; }
float OrganismViewModel::gazeShiftX() const noexcept { return locomotionOut_.gazeShiftX; }
int OrganismViewModel::edgePeekSide() const noexcept { return locomotionOut_.edgePeekSide; }
bool OrganismViewModel::isSitting() const noexcept { return locomotionOut_.isSitting; }
bool OrganismViewModel::locomotionNeedsTick() const noexcept { return locomotionOut_.needsContinuousTick; }
float OrganismViewModel::targetWindowX() const noexcept { return locomotionOut_.targetWindowX; }

void OrganismViewModel::setLocomotionEnabled(bool enabled) {
    if (locomotionEnabled_ == enabled) return;
    locomotionEnabled_ = enabled;
    saveCharacterPreference();
    emit locomotionConfigChanged();
}

void OrganismViewModel::toggleLocomotion() {
    setLocomotionEnabled(!locomotionEnabled_);
}

bool OrganismViewModel::stepLocomotion(float dtSeconds,
                                       float windowX,
                                       float windowY,
                                       float stageWidth,
                                       float stageHeight,
                                       float charBottomOffsetY,
                                       float workLeft,
                                       float workTop,
                                       float workWidth,
                                       float workHeight,
                                       bool  dragging,
                                       bool  interactionBlocked) {
    float cursorX = 0.0f;
    float cursorY = 0.0f;
    bool cursorValid = false;
    if (QGuiApplication::instance() != nullptr) {
        const QPoint pos = QCursor::pos();
        cursorX = static_cast<float>(pos.x());
        cursorY = static_cast<float>(pos.y());
        cursorValid = true;
    }
    return stepLocomotionWithCursor(
        dtSeconds, windowX, windowY, stageWidth, stageHeight, charBottomOffsetY,
        workLeft, workTop, workWidth, workHeight, dragging, interactionBlocked,
        cursorX, cursorY, cursorValid);
}

bool OrganismViewModel::stepLocomotionWithCursor(float dtSeconds,
                                                 float windowX,
                                                 float windowY,
                                                 float stageWidth,
                                                 float stageHeight,
                                                 float charBottomOffsetY,
                                                 float workLeft,
                                                 float workTop,
                                                 float workWidth,
                                                 float workHeight,
                                                 bool  dragging,
                                                 bool  interactionBlocked,
                                                 float cursorX,
                                                 float cursorY,
                                                 bool  cursorValid) {
    LocomotionInput in{};
    in.dtSeconds = dtSeconds;
    in.behaviour = snapshot_->state.current_behaviour;
    in.tier = snapshot_->budget.tier;
    in.dormant = snapshot_->dormant || (snapshot_->state.current_behaviour == common::Behaviour::Dormant);
    in.energy = snapshot_->state.energy;
    in.curiosity = snapshot_->state.curiosity;
    in.stress = snapshot_->state.stress;
    in.locomotionEnabled = locomotionEnabled_;
    in.dragging = dragging;
    in.interactionBlocked = interactionBlocked;
    in.windowX = windowX;
    in.windowY = windowY;
    in.stageWidth = stageWidth;
    in.stageHeight = stageHeight;
    in.charBottomOffsetY = charBottomOffsetY;
    in.workLeft = workLeft;
    in.workTop = workTop;
    in.workWidth = workWidth;
    in.workHeight = workHeight;
    in.cursorX = cursorX;
    in.cursorY = cursorY;
    in.cursorValid = cursorValid;

    const LocomotionOutput prev = locomotionOut_;
    locomotionOut_ = locomotion_.step(in);
    if (!locomotionOutputsEqual(prev, locomotionOut_)) {
        emit locomotionChanged();
    }
    return locomotionOut_.shouldMoveWindow;
}

} // namespace prometheus::ui
