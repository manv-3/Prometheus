#pragma once

#include "prometheus/common/ComputeBudget.hpp"
#include "prometheus/common/OrganismState.hpp"
#include <cstdint>
#include <random>

namespace prometheus::ui {

/**
 * @brief High-level desktop locomotion mode for UI presentation and diagnostics.
 *
 * Combines Mate-Engine concepts:
 *   - AvatarLocomotionController (IdleWander, ExplorePatrol)
 *   - AvatarMouseTracking        (ObserveGaze)
 *   - AvatarSwayController       (Dragging)
 *   - AvatarHideHandler          (EdgePeek)
 */
enum class LocomotionMode : uint8_t {
    Stationary = 0,
    IdleWander,
    ExplorePatrol,
    ObserveGaze,
    Dragging,
    EdgePeek
};

/**
 * @brief Tunable parameters for LocomotionEngine (initial values, to be tuned).
 *
 * Lives in a config struct rather than inline literals so thresholds can be
 * inspected, adjusted, and overridden in deterministic unit tests.
 */
struct LocomotionConfig {
    // Screen-edge turning & snap-to-peek thresholds (pixels)
    float edgeTurnThresholdPx{28.0f};    // initial, to be tuned
    float edgeSnapThresholdPx{14.0f};    // initial, to be tuned
    float edgeUnsnapThresholdPx{34.0f};  // initial, to be tuned
    float edgeTuckPx{34.0f};             // initial, to be tuned
    float edgeUnsnapCooldownSec{0.35f};  // initial, to be tuned

    // Work-area bottom (taskbar) detection band (pixels)
    float taskbarSnapBandPx{22.0f};      // initial, to be tuned

    // Idle stroll cadence & speed (QualityTier >= 2)
    float idlePauseMinSec{5.5f};         // initial, to be tuned
    float idlePauseMaxSec{10.5f};        // initial, to be tuned
    float idleWalkMinPx{75.0f};          // initial, to be tuned
    float idleWalkMaxPx{165.0f};         // initial, to be tuned
    float idleSpeedPxPerSec{50.0f};      // initial, to be tuned

    // Exploring patrol cadence & speed
    float explorePauseMinSec{1.2f};      // initial, to be tuned
    float explorePauseMaxSec{3.2f};      // initial, to be tuned
    float exploreWalkMinPx{140.0f};      // initial, to be tuned
    float exploreWalkMaxPx{320.0f};      // initial, to be tuned
    float exploreBaseSpeedPxPerSec{90.0f}; // initial, to be tuned
    float exploreLiteSpeedScale{0.58f};  // initial, to be tuned
    float exploreLiteDistScale{0.60f};   // initial, to be tuned
    float exploreLitePauseScale{2.0f};   // initial, to be tuned

    // Drag pendulum spring (Mate-Engine AvatarSwayController analog)
    float swaySpringFreqHz{2.6f};        // initial, to be tuned
    float swayDampingRatio{0.38f};       // initial, to be tuned
    float swayMaxRotDeg{22.0f};          // initial, to be tuned
    float swayVelToRotGain{0.022f};      // initial, to be tuned
    float swayMaxOffsetX{12.0f};         // initial, to be tuned
    float swayMaxOffsetY{10.0f};         // initial, to be tuned
    float swayVelToOffsetXGain{0.012f};  // initial, to be tuned
    float swayVelToOffsetYGain{0.010f};  // initial, to be tuned
    float swayVelFilterHz{12.0f};        // initial, to be tuned

    // Cursor gaze tracking (Mate-Engine AvatarMouseTracking analog)
    float gazeMaxRotDeg{6.5f};           // initial, to be tuned
    float gazeMaxShiftX{6.0f};           // initial, to be tuned
    float gazeDeadzonePx{32.0f};         // initial, to be tuned
    float gazeSmoothHz{8.0f};            // initial, to be tuned

    // Procedural gait phase & envelope
    float walkStepBaseHz{1.6f};          // initial, to be tuned
    float walkStepSpeedFactor{0.012f};   // initial, to be tuned
    float walkIntensityAttackPerSec{5.0f};  // initial, to be tuned
    float walkIntensityReleasePerSec{6.0f}; // initial, to be tuned
};

/**
 * @brief Frame input passed from the UI/ViewModel into LocomotionEngine::step().
 */
struct LocomotionInput {
    float dtSeconds{0.0f};

    common::Behaviour   behaviour{common::Behaviour::Idle};
    common::QualityTier tier{common::QualityTier::Tier3_Nominal};
    bool  dormant{false};
    float energy{1.0f};
    float curiosity{0.5f};
    float stress{0.0f};

    bool locomotionEnabled{true};
    bool dragging{false};
    bool interactionBlocked{false}; // true while cursor hovers or UI panel is open

    // Window & stage geometry in desktop coordinates
    float windowX{0.0f};
    float windowY{0.0f};
    float stageWidth{280.0f};
    float stageHeight{300.0f};
    float charBottomOffsetY{185.0f};

    // Available desktop work area (excludes taskbar)
    float workLeft{0.0f};
    float workTop{0.0f};
    float workWidth{1920.0f};
    float workHeight{1040.0f};

    // Global cursor position (optional; used for Observing/Idle gaze lean)
    float cursorX{0.0f};
    float cursorY{0.0f};
    bool  cursorValid{false};
};

/**
 * @brief View-layer motion state produced by LocomotionEngine::step().
 */
struct LocomotionOutput {
    LocomotionMode mode{LocomotionMode::Stationary};

    bool  shouldMoveWindow{false};
    float targetWindowX{0.0f};

    bool  isWalking{false};
    int   facingDir{1};           // -1 = facing left, +1 = facing right
    float walkPhase{0.0f};        // [0, 2*pi)
    float walkIntensity{0.0f};    // [0, 1] smooth blend envelope
    float walkSpeedPxPerSec{0.0f};

    float swayRotationDeg{0.0f};  // spring-damped drag/release lean
    float swayOffsetX{0.0f};
    float swayOffsetY{0.0f};

    float gazeLeanDeg{0.0f};      // cursor tracking tilt
    float gazeShiftX{0.0f};

    int   edgePeekSide{0};        // -1 = left edge, 0 = none, +1 = right edge
    bool  isSitting{false};       // perched near desktop work-area bottom / taskbar

    // True when high-rate frame animation is actively moving/settling;
    // false when stationary so QML can pause FrameAnimation and save CPU.
    bool  needsContinuousTick{false};
};

/**
 * @brief Pure C++ view-layer desktop locomotion & physics engine.
 *
 * Deterministic when constructed with an explicit seed; zero Qt/Win32
 * dependencies, zero heap allocations per step, and strictly isolated from
 * simulation contracts.
 */
class LocomotionEngine {
public:
    explicit LocomotionEngine(uint32_t seed = 0x50524F4Du,
                              LocomotionConfig config = LocomotionConfig{}) noexcept;

    [[nodiscard]] LocomotionOutput step(const LocomotionInput& input) noexcept;
    void reset(uint32_t seed = 0x50524F4Du) noexcept;

    [[nodiscard]] const LocomotionConfig& config() const noexcept { return cfg_; }
    [[nodiscard]] const LocomotionOutput& lastOutput() const noexcept { return out_; }

private:
    float randUnit() noexcept;
    float randRange(float minVal, float maxVal) noexcept;

    void scheduleNextWalk(common::Behaviour behaviour,
                          common::QualityTier tier,
                          bool initial) noexcept;
    void startWalk(common::Behaviour behaviour,
                   common::QualityTier tier,
                   float curiosity,
                   float energy,
                   float windowX,
                   float minWinX,
                   float maxWinX) noexcept;
    void stopWalk() noexcept;

    void updateSwaySpring(float dt,
                          bool highFidelity,
                          float targetRot,
                          float targetOffX,
                          float targetOffY) noexcept;

    LocomotionConfig cfg_{};
    std::mt19937     rng_;
    LocomotionOutput out_{};

    // Walk state
    bool  walking_{false};
    int   walkDir_{1};
    int   forcedNextDir_{0};
    float remainingWalkPx_{0.0f};
    float activeSpeedPxPerSec_{0.0f};
    float walkCooldownSec_{2.0f};
    float walkPhase_{0.0f};
    float walkIntensity_{0.0f};
    int   facingDir_{1};
    bool  hasVirtualX_{false};
    float virtualWinX_{0.0f};
    float lastAppliedWinX_{0.0f};

    // Drag & sway spring state
    bool  wasDragging_{false};
    float prevWinX_{0.0f};
    float prevWinY_{0.0f};
    float filteredVx_{0.0f};
    float filteredVy_{0.0f};
    float swayRot_{0.0f};
    float swayRotVel_{0.0f};
    float swayOffX_{0.0f};
    float swayOffXVel_{0.0f};
    float swayOffY_{0.0f};
    float swayOffYVel_{0.0f};

    // Edge peek state
    int   edgePeekSide_{0};
    float edgeCooldownSec_{0.0f};

    // Cursor gaze state
    float gazeLean_{0.0f};
    float gazeShiftX_{0.0f};
};

[[nodiscard]] const char* locomotionModeToString(LocomotionMode mode) noexcept;

} // namespace prometheus::ui
