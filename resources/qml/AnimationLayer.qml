import QtQuick

/**
 * AnimationLayer.qml — PROMETHEUS Animation State Machine (Phases 1–5)
 *
 * Encapsulates all behaviour-driven body and moving animations for the organism.
 * Inspired by Mate-Engine's:
 *   - AvatarAnimatorController.cs  (FSM state transitions, idle sub-cycle, dance)
 *   - AvatarLocomotionController.cs (desktop walking, left/right facing, gait)
 *   - AvatarSwayController.cs       (velocity-reactive drag spring pendulum)
 *   - AvatarMouseTracking.cs        (cursor gaze tracking)
 *   - AvatarHideHandler.cs          (screen-edge peek pose)
 *   - AvatarTaskbarController.cs    (taskbar-edge sitting pose)
 *
 * Rules:
 *   - Dormancy is always respected: all non-trivial animations check vm.dormant.
 *   - Transition durations are scaled by qualityTier (0 = instant, 4 = smoothest).
 *   - No simulation state is read or written here — purely presentational.
 *   - All durations are bounded; no unbounded loops.
 */
Item {
    id: animLayer

    // ----------------------------------------------------------------
    // Input property — parent sets this
    // ----------------------------------------------------------------
    property bool dragging: false

    // ----------------------------------------------------------------
    // Per-character motion personality (purely visual scaling)
    // ----------------------------------------------------------------
    readonly property real hopGain: {
        switch (vm.characterId) {
            case "slime": return 1.35
            case "owl":   return 1.20
            case "fox":   return 1.05
            case "bot":   return 0.40
            default:      return 1.00
        }
    }
    readonly property real squashGain: {
        switch (vm.characterId) {
            case "slime": return 1.90
            case "owl":   return 0.70
            case "fox":   return 0.90
            case "bot":   return 0.30
            default:      return 1.00
        }
    }
    readonly property real leanGain: {
        switch (vm.characterId) {
            case "slime": return 0.55
            case "owl":   return 0.80
            case "fox":   return 1.25
            case "bot":   return 1.45
            default:      return 1.00
        }
    }
    readonly property real waddleGain: {
        switch (vm.characterId) {
            case "slime": return 0.80
            case "owl":   return 1.15
            case "fox":   return 1.10
            case "bot":   return 0.35
            default:      return 1.00
        }
    }

    // ----------------------------------------------------------------
    // Crossfade duration scaled by QualityTier
    // Mate-Engine: AnimationTransitionTime
    // ----------------------------------------------------------------
    readonly property int xfadeDuration: {
        if (vm.qualityTier === 0) return 0
        if (dragging)             return 120
        switch (vm.qualityTier) {
            case 1:  return 150
            case 2:  return 300
            case 3:  return 500
            default: return 700   // tier 4: optimal
        }
    }

    // ----------------------------------------------------------------
    // Target values driven by animState + dragging
    // ----------------------------------------------------------------
    readonly property real targetScale: {
        if (dragging)                          return 1.07
        switch (vm.animState) {
            case "dormant":           return 0.88
            case "rest_deep":         return 0.92
            case "rest_light":        return 0.96
            case "idle":              return 1.00
            case "alert":             return 1.03
            case "explore_full":      return 1.07
            case "explore_lite":      return 1.04
            case "stressed_critical": return 0.94
            case "stressed_mild":     return 0.97
            default:                  return 1.00
        }
    }

    readonly property real targetRotation: {
        // When dragging, dynamic spring sway (vm.swayRotation) drives tilt instead of a static angle
        if (dragging)                          return  0.0
        switch (vm.animState) {
            case "dormant":           return  0.0
            case "rest_deep":         return -3.0
            case "rest_light":        return -1.5
            case "idle":              return  0.0
            case "alert":             return  1.5
            case "explore_full":      return  0.0
            case "explore_lite":      return  0.0
            case "stressed_critical": return -4.0
            case "stressed_mild":     return -2.0
            default:                  return  0.0
        }
    }

    readonly property real targetYOffset: {
        if (dragging)                          return -6.0
        switch (vm.animState) {
            case "dormant":           return  4.0
            case "rest_deep":         return  3.0
            case "rest_light":        return  1.5
            case "idle":              return  0.0
            case "alert":             return -2.0
            case "explore_full":      return -3.0
            case "explore_lite":      return -1.5
            case "stressed_critical": return  2.0
            case "stressed_mild":     return  1.0
            default:                  return  0.0
        }
    }

    // ----------------------------------------------------------------
    // Phase 2 — Pressure-Reactive Flinch ("Pulse") scale offset
    // ----------------------------------------------------------------
    property real pulseScaleExtra: 0.0
    property real _pulsePeak:      0.0

    Connections {
        target: vm
        function onSnapshotChanged() {
            if (vm.dormant) {
                pulseBurst.stop()
                animLayer.pulseScaleExtra = 0.0
                animLayer._pulsePeak = 0.0
                return
            }
            if (vm.animPulse > 0.3) {
                const nextPeak = vm.animPulse * 0.12
                if (!pulseBurst.running || nextPeak > animLayer._pulsePeak) {
                    animLayer._pulsePeak = nextPeak
                    pulseBurst.restart()
                }
            }
        }
    }

    SequentialAnimation {
        id: pulseBurst
        NumberAnimation {
            target: animLayer
            property: "pulseScaleExtra"
            to: animLayer._pulsePeak
            duration: 70
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: animLayer
            property: "pulseScaleExtra"
            to: 0.0
            duration: 350
            easing.type: Easing.InOutSine
        }
        onRunningChanged: {
            if (!running) animLayer._pulsePeak = 0.0
        }
    }

    // ----------------------------------------------------------------
    // Phase 3 — Exploration "Dance" Pulse
    // ----------------------------------------------------------------
    property real danceScaleExtra: 0.0

    SequentialAnimation {
        id: danceAnim
        running: (vm.animState === "explore_full") && !vm.dormant && !animLayer.dragging
        loops:   Animation.Infinite
        onRunningChanged: if (!running) animLayer.danceScaleExtra = 0.0

        NumberAnimation { target: animLayer; property: "danceScaleExtra"; to:  0.04; duration: 400; easing.type: Easing.InOutSine }
        NumberAnimation { target: animLayer; property: "danceScaleExtra"; to: -0.02; duration: 300; easing.type: Easing.InOutSine }
        NumberAnimation { target: animLayer; property: "danceScaleExtra"; to:  0.02; duration: 350; easing.type: Easing.InOutSine }
        NumberAnimation { target: animLayer; property: "danceScaleExtra"; to:  0.00; duration: 300; easing.type: Easing.OutSine   }
        PauseAnimation  { duration: 200 }
    }

    // ----------------------------------------------------------------
    // Phase 2b — Stress tremor (micro-rotation jitter)
    // ----------------------------------------------------------------
    property real tremorRotExtra: 0.0

    SequentialAnimation {
        id: tremorAnim
        running: vm.stress > 0.75 && !vm.dormant && vm.qualityTier >= 2 && !animLayer.dragging
        loops:   Animation.Infinite
        onRunningChanged: if (!running) animLayer.tremorRotExtra = 0.0

        NumberAnimation { target: animLayer; property: "tremorRotExtra"; to:  1.5; duration: 60; easing.type: Easing.InOutSine }
        NumberAnimation { target: animLayer; property: "tremorRotExtra"; to: -1.5; duration: 60; easing.type: Easing.InOutSine }
        NumberAnimation { target: animLayer; property: "tremorRotExtra"; to:  0.8; duration: 60; easing.type: Easing.InOutSine }
        NumberAnimation { target: animLayer; property: "tremorRotExtra"; to: -0.8; duration: 60; easing.type: Easing.InOutSine }
        NumberAnimation { target: animLayer; property: "tremorRotExtra"; to:  0.0; duration: 80; easing.type: Easing.OutSine   }
        PauseAnimation  { duration: 400 }
    }

    // ----------------------------------------------------------------
    // Phase 4 — Idle Sub-Cycle System (pauses during walking / peeking)
    // ----------------------------------------------------------------
    property real idleSubYExtra:     0.0
    property real idleSubScaleExtra: 0.0
    property real idleSubRotExtra:   0.0
    property int  idleSubPause:      3000

    SequentialAnimation {
        id: idleSubcycle
        running: vm.animState === "idle" && !vm.dormant && vm.qualityTier >= 2 &&
                 !animLayer.dragging && !vm.isWalking && vm.edgePeekSide === 0
        loops:   Animation.Infinite

        onRunningChanged: {
            if (!running) {
                animLayer.idleSubYExtra     = 0.0
                animLayer.idleSubScaleExtra = 0.0
                animLayer.idleSubRotExtra   = 0.0
            }
        }

        PauseAnimation { duration: animLayer.idleSubPause }

        ParallelAnimation {
            NumberAnimation {
                target: animLayer; property: "idleSubYExtra"
                to: -2.5; duration: 600; easing.type: Easing.OutSine
            }
            NumberAnimation {
                target: animLayer; property: "idleSubScaleExtra"
                to: 0.04; duration: 600; easing.type: Easing.OutSine
            }
        }
        PauseAnimation { duration: 450 }

        ParallelAnimation {
            NumberAnimation {
                target: animLayer; property: "idleSubRotExtra"
                to: 3.0; duration: 350; easing.type: Easing.InOutSine
            }
            NumberAnimation {
                target: animLayer; property: "idleSubScaleExtra"
                to: 0.0; duration: 400; easing.type: Easing.InOutSine
            }
        }
        NumberAnimation {
            target: animLayer; property: "idleSubRotExtra"
            to: -2.0; duration: 450; easing.type: Easing.InOutSine
        }
        ParallelAnimation {
            NumberAnimation {
                target: animLayer; property: "idleSubRotExtra"
                to: 0.0; duration: 350; easing.type: Easing.OutSine
            }
            NumberAnimation {
                target: animLayer; property: "idleSubYExtra"
                to: 0.0; duration: 400; easing.type: Easing.OutSine
            }
        }

        ScriptAction {
            script: {
                animLayer.idleSubPause = Math.round(2000 + Math.random() * 4000)
            }
        }
    }

    // ----------------------------------------------------------------
    // Complementary curiosity bounce & dormant breath
    // ----------------------------------------------------------------
    property real curiousBounceExtra: 0.0

    SequentialAnimation {
        id: curiousBounceAnim
        running: vm.curiosity > 0.65 && !vm.dormant && !animLayer.dragging &&
                 !vm.isWalking && vm.edgePeekSide === 0
        loops:   Animation.Infinite
        onRunningChanged: if (!running) animLayer.curiousBounceExtra = 0.0

        NumberAnimation { target: animLayer; property: "curiousBounceExtra"; to: -6.0; duration: 350; easing.type: Easing.OutQuad }
        NumberAnimation { target: animLayer; property: "curiousBounceExtra"; to:  0.0; duration: 350; easing.type: Easing.InQuad  }
        PauseAnimation  { duration: Math.max(200, 1200 - vm.curiosity * 1000) }
    }

    property real dormantBreathExtra: 0.0

    SequentialAnimation {
        id: dormantBreathAnim
        running: vm.dormant
        loops:   Animation.Infinite
        onRunningChanged: if (!running) animLayer.dormantBreathExtra = 0.0

        NumberAnimation { target: animLayer; property: "dormantBreathExtra"; to: -0.04; duration: 2500; easing.type: Easing.InOutSine }
        NumberAnimation { target: animLayer; property: "dormantBreathExtra"; to:  0.00; duration: 2500; easing.type: Easing.InOutSine }
    }

    // ----------------------------------------------------------------
    // Phase 5 — Procedural Walking Gait, Facing Flip, Sitting & Edge Peek
    // ----------------------------------------------------------------
    readonly property real walkActive: vm.dormant ? 0.0 : vm.walkIntensity
    readonly property real stepArc:    Math.abs(Math.sin(vm.walkPhase))
    readonly property real hopAmplitude: {
        if (vm.animState === "explore_full") return 9.5 * hopGain
        if (vm.animState === "explore_lite") return 6.5 * hopGain
        return 4.5 * hopGain
    }
    readonly property real walkYExtra:    -stepArc * hopAmplitude * walkActive
    readonly property real walkXExtra:    Math.cos(vm.walkPhase) * 2.5 * walkActive
    readonly property real walkTiltExtra: ((vm.facingDir * 5.0 * leanGain) + Math.sin(vm.walkPhase) * 4.5 * waddleGain) * walkActive
    readonly property real walkSquashWave: Math.cos(2.0 * vm.walkPhase) * 0.05 * squashGain * walkActive

    // Facing horizontal mirror applied to catImage (1.0 right, -1.0 left)
    property real _facingTarget: vm.facingDir < 0 ? -1.0 : 1.0
    Behavior on _facingTarget {
        enabled: animLayer.xfadeDuration > 0
        NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
    }

    // Sitting on taskbar pose (Mate-Engine AvatarTaskbarController analog)
    property real _sitYExtra:    (vm.isSitting && !vm.dormant && !dragging) ?  6.0 : 0.0
    property real _sitSquashX:   (vm.isSitting && !vm.dormant && !dragging) ?  0.06 : 0.0
    property real _sitSquashY:   (vm.isSitting && !vm.dormant && !dragging) ? -0.06 : 0.0
    Behavior on _sitYExtra  { enabled: animLayer.xfadeDuration > 0; NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
    Behavior on _sitSquashX { enabled: animLayer.xfadeDuration > 0; NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
    Behavior on _sitSquashY { enabled: animLayer.xfadeDuration > 0; NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

    // Screen-edge peek pose (Mate-Engine AvatarHideHandler analog)
    property real _peekRotExtra: (vm.edgePeekSide !== 0 && !vm.dormant && !dragging)
                                     ? ((vm.edgePeekSide < 0) ? 14.0 : -14.0)
                                     : 0.0
    property real _peekXExtra:   (vm.edgePeekSide !== 0 && !vm.dormant && !dragging)
                                     ? ((vm.edgePeekSide < 0) ? 4.0 : -4.0)
                                     : 0.0
    Behavior on _peekRotExtra { enabled: animLayer.xfadeDuration > 0; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Behavior on _peekXExtra   { enabled: animLayer.xfadeDuration > 0; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    // ----------------------------------------------------------------
    // Smooth crossfade intermediates + final compound outputs
    // ----------------------------------------------------------------
    property real _baseScale:    targetScale
    property real _baseRotation: targetRotation
    property real _baseYOffset:  targetYOffset

    Behavior on _baseScale {
        enabled: animLayer.xfadeDuration > 0
        NumberAnimation { duration: animLayer.xfadeDuration; easing.type: Easing.OutCubic }
    }
    Behavior on _baseRotation {
        enabled: animLayer.xfadeDuration > 0
        NumberAnimation { duration: animLayer.xfadeDuration; easing.type: Easing.OutCubic }
    }
    Behavior on _baseYOffset {
        enabled: animLayer.xfadeDuration > 0
        NumberAnimation { duration: animLayer.xfadeDuration; easing.type: Easing.OutCubic }
    }

    // Public output properties — parent's catBody and catImage bind to these
    readonly property real bodyScale:    _baseScale + pulseScaleExtra + danceScaleExtra + idleSubScaleExtra + dormantBreathExtra
    readonly property real squashScaleX: Math.max(0.75, 1.0 + walkSquashWave + _sitSquashX)
    readonly property real squashScaleY: Math.max(0.75, 1.0 - walkSquashWave + _sitSquashY)
    readonly property real facingScaleX: _facingTarget
    readonly property real bodyRotation: _baseRotation + tremorRotExtra + idleSubRotExtra + walkTiltExtra + vm.swayRotation + vm.gazeLean + _peekRotExtra
    readonly property real bodyXOffset:  vm.swayOffsetX + vm.gazeShiftX + walkXExtra + _peekXExtra
    readonly property real bodyYOffset:  _baseYOffset + idleSubYExtra + curiousBounceExtra + walkYExtra + vm.swayOffsetY + _sitYExtra
}
