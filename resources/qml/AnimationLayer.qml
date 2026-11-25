import QtQuick

/**
 * AnimationLayer.qml — PROMETHEUS Animation State Machine
 *
 * Encapsulates all behaviour-driven body animations for the organism.
 * Inspired by Mate-Engine's AvatarAnimatorController.cs which manages FSM
 * transitions between idle, dance, drag, and interaction states.
 *
 * PROMETHEUS adaptation maps:
 *   animState (vm.animState)  → body transformation target values
 *   animPulse (vm.animPulse)  → flinch/burst reactive animation
 *                               (Mate-Engine: audio BPM spike → dance burst)
 *   dragging  (input prop)    → tilt + float effect
 *                               (Mate-Engine: drag/drop avatar state)
 *   QualityTier               → animation complexity + crossfade duration
 *
 * Rules:
 *   - Dormancy is always respected: all non-trivial animations check vm.dormant.
 *   - Transition durations are scaled by qualityTier (0 = instant, 4 = smoothest).
 *   - No simulation state is read or written here — purely presentational.
 *   - All durations are bounded; no unbounded loops.
 *
 * Usage in Main.qml:
 *   AnimationLayer {
 *       id: anim
 *       dragging: root.isDragging
 *   }
 *   // Then bind catBody:
 *   scale:    anim.bodyScale
 *   rotation: anim.bodyRotation
 *   transform: Translate { y: anim.bodyYOffset }
 */
Item {
    id: animLayer

    // ----------------------------------------------------------------
    // Input property — parent sets this
    // ----------------------------------------------------------------
    property bool dragging: false

    // ----------------------------------------------------------------
    // Crossfade duration scaled by QualityTier
    // Mate-Engine: AnimationTransitionTime
    // Tier 0=dormant/instant, drag=120ms (when awake), Tier 1..4=150..700ms
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
    // Mate-Engine equivalent: AvatarAnimatorController parameter targets
    // ----------------------------------------------------------------
    readonly property real targetScale: {
        if (dragging)                          return 1.08
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
        if (dragging)                          return 7.0
        switch (vm.animState) {
            case "dormant":           return  0.0
            case "rest_deep":         return -3.0
            case "rest_light":        return -1.5
            case "idle":              return  0.0
            case "alert":             return  2.0
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
            case "explore_full":      return -4.0
            case "explore_lite":      return -2.0
            case "stressed_critical": return  2.0
            case "stressed_mild":     return  1.0
            default:                  return  0.0
        }
    }

    // ----------------------------------------------------------------
    // Phase 2 — Pressure-Reactive Flinch ("Pulse") scale offset
    // animPulse > 0.3 fires a crisp outward scale burst then decays.
    // Mate-Engine: audio BPM peak → dance burst.
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
        // Sharp outward burst
        NumberAnimation {
            target: animLayer
            property: "pulseScaleExtra"
            to: animLayer._pulsePeak
            duration: 70
            easing.type: Easing.OutQuad
        }
        // Slow organic decay
        NumberAnimation {
            target: animLayer
            property: "pulseScaleExtra"
            to: 0.0
            duration: 350
            easing.type: Easing.InOutSine
        }
        onRunningChanged: {
            if (!running) {
                animLayer._pulsePeak = 0.0
            }
        }
    }

    // ----------------------------------------------------------------
    // Phase 3 — Exploration "Dance" Pulse
    // Rhythmic breathing scale when exploring at high tier.
    // Mate-Engine: dance animation looping during audio-reactive mode.
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
    // High-frequency jitter when vm.stress > 0.75, tier >= 2.
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
    // Phase 4 — Idle Sub-Cycle System
    // Mate-Engine: randomised idle animation timing (AnimationChangeTime).
    // Random stretch + head-tilt micro-animations fire while idle, tier >= 2.
    // ----------------------------------------------------------------
    property real idleSubYExtra:     0.0
    property real idleSubScaleExtra: 0.0
    property real idleSubRotExtra:   0.0
    property int  idleSubPause:      3000   // re-rolled each cycle [2000–6000 ms]

    SequentialAnimation {
        id: idleSubcycle
        running: vm.animState === "idle" && !vm.dormant && vm.qualityTier >= 2 && !animLayer.dragging
        loops:   Animation.Infinite

        onRunningChanged: {
            if (!running) {
                animLayer.idleSubYExtra     = 0.0
                animLayer.idleSubScaleExtra = 0.0
                animLayer.idleSubRotExtra   = 0.0
            }
        }

        // --- neutral hold (random dwell — Mate-Engine: AnimationChangeTime) ---
        PauseAnimation { duration: animLayer.idleSubPause }

        // --- sub-state A: gentle float-up + stretch swell (1.0 → 1.04) ---
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

        // --- sub-state B: subtle head-tilt look (0 → 3 → -2 → 0) & settle ---
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

        // Re-roll the random pause on each cycle (bounded [2000, 6000])
        ScriptAction {
            script: {
                animLayer.idleSubPause = Math.round(2000 + Math.random() * 4000)
            }
        }
    }

    // ----------------------------------------------------------------
    // Complementary base animations (moved from Main.qml to avoid
    // binding conflicts on catBody)
    // ----------------------------------------------------------------
    property real curiousBounceExtra: 0.0

    SequentialAnimation {
        id: curiousBounceAnim
        running: vm.curiosity > 0.65 && !vm.dormant && !animLayer.dragging
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

    // Public output properties — parent's catBody binds to these
    readonly property real bodyScale:    _baseScale    + pulseScaleExtra + danceScaleExtra + idleSubScaleExtra + dormantBreathExtra
    readonly property real bodyRotation: _baseRotation + tremorRotExtra  + idleSubRotExtra
    readonly property real bodyYOffset:  _baseYOffset  + idleSubYExtra   + curiousBounceExtra
}
