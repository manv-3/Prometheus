import QtQuick
import QtQuick.Window

/**
 * PROMETHEUS – Main organism window
 *
 * The organism body renders the active companion SVG/image (`vm.characterSource`)
 * chosen from CharacterPanel.qml (default: Ember Cat `organism.svg`).
 * All behaviour-driven animations are applied via QML properties layered
 * on top of the image — no SMIL animation required from the SVG itself.
 *
 * Visual mapping:
 *   Character skin  ← vm.characterSource (Cat, Owl, Fox, Bot, Slime, or custom)
 *   Body size       ← energy             (larger = more energetic)
 *   Body opacity    ← health             (dimmer = damaged)
 *   Hue tint        ← behaviour          (colour shift per state)
 *   Stress ring     ← stress             (pulsing halo)
 *   Particle dots   ← activeParticles    (orbiting dots)
 *   Adaptation glow ← adaptation         (golden outer ring)
 *   Dormancy        ← dormant            (shrinks, greyscale, slow breathing)
 *   Bounce anim     ← curiosity          (subtle bounce when curious)
 *
 *   === Mate-Engine animation system (Phase 1–5) ===
 *   animState       ← Behaviour × QualityTier  (body posture / scale / rotation)
 *   animPulse       ← pressure spike           (flinch burst — analog: BPM spike)
 *   isDragging      ← mouse drag               (velocity spring sway — AvatarSwayController)
 *   idle sub-cycle  ← timer-driven QML         (random stretch + head-tilt)
 *   locomotion      ← LocomotionEngine         (desktop patrol, gait, gaze, edge-peek, taskbar-sit)
 *
 * Controls:
 *   [C] or "Chars" pill : toggle Character Selector Panel
 *   [M] or "Move" pill  : toggle autonomous desktop walking
 *   [D] or right-click  : toggle Developer Diagnostics Panel
 *   [Esc]               : close open overlay panel
 *
 * SYNTHETIC badge shown when syntheticMode == true (REQ-UI-02).
 */
Window {
    id: root

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"

    // Expand window horizontally when the side CharacterPanel is open so the
    // live organism stays completely unobstructed alongside the selection grid.
    // Expand vertically only if the diagnostics panel needs extra vertical space.
    width:  showCharPanel ? 510 : 280
    height: showDiag ? Math.max(300, diagBox.height + 12) : 300
    minimumWidth:  180
    minimumHeight: 200

    visible: true
    title: "PROMETHEUS"

    // ---- ViewModel is exposed as "vm" context property from C++ (main.cpp) ----
    // Do NOT instantiate OrganismViewModel here — it lives on the C++ side.

    onActiveChanged: if (active) keyHandler.forceActiveFocus()

    // ---- Overlay panel toggles ----
    property bool showDiag:      false
    property bool showCharPanel: false

    // ---- Drag state (Mate-Engine drag/sway interaction) ----
    property bool isDragging: false
    onIsDraggingChanged: runLocomotionStep(isDragging ? 0.0 : 0.016)

    // ---- Locomotion step helper (Phase 5) ----
    function runLocomotionStep(dt) {
        const charBottomLocal = catBody.y + catBody.height
        const blocked = windowHover.hovered || root.showCharPanel || root.showDiag
        const shouldMove = vm.stepLocomotion(
            dt,
            root.x, root.y,
            organismStage.width, organismStage.height,
            charBottomLocal,
            Screen.desktopAvailableLeft, Screen.desktopAvailableTop,
            Screen.desktopAvailableWidth, Screen.desktopAvailableHeight,
            root.isDragging,
            blocked
        )
        if (shouldMove) {
            root.x = Math.round(vm.targetWindowX)
        }
    }

    Component.onCompleted: {
        // Clamp initial window placement safely inside the active screen work area
        const minX = Screen.desktopAvailableLeft
        const maxX = Math.max(minX, minX + Screen.desktopAvailableWidth - organismStage.width)
        const minY = Screen.desktopAvailableTop
        const maxY = Math.max(minY, minY + Screen.desktopAvailableHeight - organismStage.height)
        if (root.x < minX || root.x > maxX) root.x = Math.round((minX + maxX) * 0.5)
        if (root.y < minY || root.y > maxY) root.y = Math.round(maxY - 20)
    }

    // High-FPS locomotion & spring-sway animation driver (paused when dormant/stationary)
    FrameAnimation {
        id: locomotionFrameAnim
        running: !vm.dormant && (root.isDragging || vm.locomotionNeedsTick)
        property real _accumDt: 0.0
        onRunningChanged: {
            if (!running) _accumDt = 0.0
        }
        onTriggered: {
            const safeDt = Math.min(frameTime, 0.05)
            if (root.isDragging) {
                _accumDt = 0.0
                root.runLocomotionStep(safeDt)
                return
            }
            const budgetHz = Math.max(1, vm.targetRenderHz)
            const minStep = 1.0 / budgetHz
            _accumDt += safeDt
            if (_accumDt >= minStep) {
                const stepDt = Math.min(_accumDt, 0.1)
                _accumDt = 0.0
                root.runLocomotionStep(stepDt)
            }
        }
    }

    // Low-frequency (4 Hz) decision timer when stationary in Idle/Exploring/Observing
    Timer {
        id: locomotionIdleTimer
        interval: 250
        repeat: true
        running: !vm.dormant && !locomotionFrameAnim.running && !root.isDragging &&
                 vm.locomotionEnabled &&
                 (vm.behaviour === "Idle" || vm.behaviour === "Exploring" || vm.behaviour === "Observing")
        onTriggered: root.runLocomotionStep(0.25)
    }

    Connections {
        target: vm
        function onLocomotionConfigChanged() {
            if (!vm.dormant) root.runLocomotionStep(0.016)
        }
    }

    function toggleCharPanel() {
        root.showCharPanel = !root.showCharPanel
        if (root.showCharPanel) root.showDiag = false
        keyHandler.forceActiveFocus()
    }

    function toggleDiagPanel() {
        root.showDiag = !root.showDiag
        if (root.showDiag) root.showCharPanel = false
        keyHandler.forceActiveFocus()
    }

    Item {
        id: keyHandler
        anchors.fill: parent
        focus: true
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_D) {
                root.toggleDiagPanel()
                event.accepted = true
            } else if (event.key === Qt.Key_C) {
                root.toggleCharPanel()
                event.accepted = true
            } else if (event.key === Qt.Key_M) {
                vm.toggleLocomotion()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && (root.showDiag || root.showCharPanel)) {
                root.showDiag = false
                root.showCharPanel = false
                event.accepted = true
            }
        }
    }

    // Track pointer hover anywhere inside the window without intercepting clicks/drags
    HoverHandler {
        id: windowHover
    }

    // ================================================================
    //  ORGANISM STAGE (fixed 280x300 on the left — never jumps on panel open)
    // ================================================================
    Item {
        id: organismStage
        anchors {
            left: parent.left
            top: parent.top
        }
        width:  280
        height: 300

        // ---- Drag to reposition + grab keyboard focus on any click ----
        MouseArea {
            anchors.fill: parent
            property point startPos
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            onPressed: (m) => {
                startPos = Qt.point(m.x, m.y)
                root.requestActivate()
                keyHandler.forceActiveFocus()
                if (m.button === Qt.RightButton) {
                    root.toggleDiagPanel()
                    m.accepted = true
                } else {
                    root.isDragging = true
                }
            }
            onPositionChanged: (m) => {
                if (pressed && (m.buttons & Qt.LeftButton)) {
                    root.x += m.x - startPos.x
                    root.y += m.y - startPos.y
                }
            }
            onReleased: root.isDragging = false
            onCanceled: root.isDragging = false
        }

        // ============================================================
        //  LAYER 1 — Adaptation glow (outermost golden ring)
        // ============================================================
        Rectangle {
            anchors.centerIn: catBody
            width:  catBody.width  + 20 + vm.adaptation * 30
            height: catBody.height + 20 + vm.adaptation * 30
            radius: width / 2
            color: "transparent"
            border.color: Qt.rgba(1.0, 0.85, 0.15, vm.adaptation * 0.5)
            border.width: 3 + vm.adaptation * 6
            visible: vm.adaptation > 0.05
            Behavior on width { NumberAnimation { duration: 800; easing.type: Easing.OutSine } }
        }

        // ============================================================
        //  LAYER 2 — Stress pulse ring
        // ============================================================
        Rectangle {
            id: pulseRing
            anchors.centerIn: catBody
            width:  catBody.width  + 14
            height: catBody.height + 14
            radius: width / 2
            color: "transparent"
            border.color: Qt.rgba(1.0, 0.2, 0.05, vm.stress * 0.75)

            property real flareExtra: 0.0
            border.width: 2 + vm.stress * 5 + flareExtra
            visible: vm.stress > 0.10

            SequentialAnimation on opacity {
                running: pulseRing.visible
                loops: Animation.Infinite
                NumberAnimation {
                    to: 0.1
                    duration: vm.dormant ? 4000 : (400 + (1.0 - vm.stress) * 700)
                    easing.type: Easing.InOutSine
                }
                NumberAnimation {
                    to: 1.0
                    duration: vm.dormant ? 4000 : (400 + (1.0 - vm.stress) * 700)
                    easing.type: Easing.InOutSine
                }
            }

            property real _flarePeak: 0.0
            SequentialAnimation {
                id: stressFlare
                running: false
                NumberAnimation {
                    target: pulseRing
                    property: "flareExtra"
                    to: pulseRing._flarePeak
                    duration: 80
                    easing.type: Easing.OutQuad
                }
                NumberAnimation {
                    target: pulseRing
                    property: "flareExtra"
                    to: 0.0
                    duration: 400
                    easing.type: Easing.InSine
                }
                onRunningChanged: if (!running) pulseRing._flarePeak = 0.0
            }

            Connections {
                target: vm
                function onSnapshotChanged() {
                    if (vm.dormant || vm.stress <= 0.1) {
                        stressFlare.stop()
                        pulseRing.flareExtra = 0.0
                        pulseRing._flarePeak = 0.0
                        return
                    }
                    if (vm.animPulse > 0.3) {
                        const nextPeak = vm.animPulse * 8.0
                        if (!stressFlare.running || nextPeak > pulseRing._flarePeak) {
                            pulseRing._flarePeak = nextPeak
                            stressFlare.restart()
                        }
                    }
                }
            }
        }

        // ============================================================
        //  LAYER 3 — Orbiting particle dots
        // ============================================================
        Item {
            id: particleField
            anchors.centerIn: catBody
            width:  catBody.width  + 70
            height: catBody.height + 70

            property int visibleDots: Math.min(12, Math.max(0,
                Math.round(vm.activeParticles / 25.0)))

            Repeater {
                model: 12
                delegate: Rectangle {
                    property int   idx:    index
                    property real  orbit:  (particleField.width / 2) - 4 + Math.sin(idx * 1.4) * 10
                    property real  ang:    (idx / 12.0) * 360.0 + particleField.rotation

                    width:  visible ? (3 + idx % 3) : 0
                    height: width
                    radius: width / 2
                    x: particleField.width  / 2 + orbit * Math.cos(ang * Math.PI / 180) - width / 2
                    y: particleField.height / 2 + orbit * Math.sin(ang * Math.PI / 180) - height / 2

                    color: {
                        if (vm.dormant) return Qt.rgba(0.5, 0.5, 0.55, 0.4)
                        switch (vm.behaviour) {
                            case "Exploring":  return Qt.rgba(0.3, 0.7, 1.0, 0.85)
                            case "Observing":  return Qt.rgba(0.7, 0.3, 1.0, 0.85)
                            case "Stressed":   return Qt.rgba(1.0, 0.4, 0.1, 0.85)
                            case "Resting":    return Qt.rgba(1.0, 0.95, 0.3, 0.85)
                            default:           return Qt.rgba(0.4, 1.0, 0.6, 0.85)
                        }
                    }
                    visible: index < particleField.visibleDots && !vm.dormant
                    Behavior on color { ColorAnimation { duration: 400 } }
                }
            }

            transform: Translate { x: -vm.facingDir * vm.walkIntensity * 10.0 }

            FrameAnimation {
                running: !vm.dormant && particleField.visibleDots > 0
                onTriggered: {
                    const durSec = Math.max(4.0, 18.0 - vm.curiosity * 12.0 - vm.qualityTier * 1.0)
                    const dir = root.isDragging ? -1.0 : 1.0
                    const walkBoost = 1.0 + vm.walkIntensity * 0.65
                    const step = dir * walkBoost * (360.0 / durSec) * Math.min(frameTime, 0.1)
                    particleField.rotation = (particleField.rotation + step + 360.0) % 360.0
                }
            }
        }

        // Ground contact shadow (shrinks when jumping/dragged, widens when sitting)
        Rectangle {
            id: groundShadow
            anchors {
                horizontalCenter: catBody.horizontalCenter
                top: catBody.bottom
                topMargin: -4
            }
            readonly property real liftFactor: Math.max(0.0, Math.min(1.0, (-anim.bodyYOffset) / 14.0))
            width:  Math.max(24, catBody.width * (vm.isSitting ? 0.86 : (0.70 - liftFactor * 0.22)))
            height: vm.isSitting ? 9 : 7
            radius: height / 2
            color:  Qt.rgba(0.0, 0.0, 0.0, vm.dormant ? 0.16 : (0.34 - liftFactor * 0.16))
            transform: Translate { x: anim.bodyXOffset * 0.65 }
            Behavior on width { NumberAnimation { duration: 160 } }
        }

        // ============================================================
        //  LAYER 4 — Organism character body + AnimationLayer
        // ============================================================
        Item {
            id: catBody
            anchors {
                horizontalCenter: parent.horizontalCenter
                top: parent.top
                topMargin: 10
            }

            property real targetSize: vm.dormant
                ? 90
                : Math.round(90 + vm.energy * 80)

            width:  targetSize
            height: targetSize

            Behavior on width  { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
            Behavior on height { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }

            opacity: vm.dormant
                ? 0.5
                : (0.55 + vm.health * 0.45)
            Behavior on opacity { NumberAnimation { duration: 600 } }

            AnimationLayer {
                id: anim
                dragging: root.isDragging
            }

            scale:     anim.bodyScale
            rotation:  anim.bodyRotation
            transform: [
                Scale {
                    origin.x: catBody.width / 2
                    origin.y: catBody.height
                    xScale: anim.squashScaleX
                    yScale: anim.squashScaleY
                },
                Translate {
                    x: anim.bodyXOffset
                    y: anim.bodyYOffset
                }
            ]

            // Greyscale filter when dormant
            layer.enabled:  vm.dormant
            layer.effect: ShaderEffect {
                property variant src: catBody
                fragmentShader: "
                    varying highp vec2 qt_TexCoord0;
                    uniform sampler2D source;
                    void main() {
                        vec4 c = texture2D(source, qt_TexCoord0);
                        float g = dot(c.rgb, vec3(0.299, 0.587, 0.114));
                        gl_FragColor = vec4(g, g, g, c.a) * 0.75;
                    }
                "
            }

            // Active character asset (driven by vm.characterSource)
            Image {
                id: catImage
                anchors.fill: parent
                source: vm.characterSource
                fillMode: Image.PreserveAspectFit
                smooth: true
                antialiasing: true
                transform: Scale {
                    origin.x: catImage.width / 2
                    origin.y: catImage.height / 2
                    xScale: anim.facingScaleX
                }
            }

            // Behaviour colour tint overlay
            Rectangle {
                anchors.fill: catImage
                radius: width / 2
                opacity: vm.dormant ? 0.0 : 0.16
                color: {
                    switch (vm.behaviour) {
                        case "Exploring":  return "#2299ff"
                        case "Observing":  return "#aa44ff"
                        case "Stressed":   return "#ff5500"
                        case "Resting":    return "#ffdd00"
                        default:           return "#44cc77"   // Idle
                    }
                }
                Behavior on color   { ColorAnimation { duration: 500 } }
                Behavior on opacity { NumberAnimation { duration: 400 } }
            }
        }

        // ============================================================
        //  LAYER 5 — Health bar (below character)
        // ============================================================
        Rectangle {
            anchors {
                horizontalCenter: catBody.horizontalCenter
                top: catBody.bottom
                topMargin: 4
            }
            width:  catBody.width * 0.85
            height: 4
            radius: 2
            color: Qt.rgba(1, 1, 1, 0.15)

            Rectangle {
                width:  parent.width * vm.health
                height: parent.height
                radius: parent.radius
                color: {
                    if (vm.health > 0.6) return Qt.rgba(0.3, 0.9, 0.4, 0.9)
                    if (vm.health > 0.3) return Qt.rgba(0.9, 0.75, 0.1, 0.9)
                    return Qt.rgba(0.9, 0.2, 0.1, 0.9)
                }
                Behavior on width { NumberAnimation { duration: 400 } }
                Behavior on color { ColorAnimation  { duration: 400 } }
            }
        }

        // ============================================================
        //  LAYER 6 — Status strip (bottom-left)
        // ============================================================
        Column {
            anchors { left: parent.left; bottom: parent.bottom; margins: 7 }
            spacing: 2

            Text { text: "⚡ " + Math.round(vm.energy    * 100) + "%"; font.pixelSize: 9; color: Qt.rgba(1,1,1,0.75) }
            Text { text: "🔥 " + Math.round(vm.stress    * 100) + "%"; font.pixelSize: 9; color: Qt.rgba(1,1,1,0.75) }
            Text { text: "T" + vm.qualityTier + "  " + vm.activeParticles + "p"; font.pixelSize: 9; color: Qt.rgba(1,1,1,0.70) }
        }

        // ============================================================
        //  LAYER 7 — SYNTHETIC badge (REQ-UI-02) & hover character pill
        // ============================================================
        Rectangle {
            anchors { top: parent.top; right: parent.right; margins: 5 }
            width:  synLabel.implicitWidth + 8
            height: synLabel.implicitHeight + 4
            color: Qt.rgba(0.9, 0.7, 0.0, 0.88)
            radius: 3
            visible: vm.syntheticMode

            Text {
                id: synLabel
                anchors.centerIn: parent
                text: "SYNTHETIC"
                font.pixelSize: 8; font.bold: true
                color: "black"
            }
        }

        // Subtle hover pill to open/close the Character Selector Panel
        Rectangle {
            id: charBtnPill
            anchors { top: parent.top; left: parent.left; margins: 6 }
            width:  charPillRow.implicitWidth + 12
            height: 20
            radius: 10
            visible: !root.showDiag && (windowHover.hovered || root.showCharPanel)
            color: root.showCharPanel
                ? Qt.rgba(0.25, 0.55, 0.95, 0.85)
                : (charPillMouse.containsMouse
                    ? Qt.rgba(0.12, 0.16, 0.25, 0.92)
                    : Qt.rgba(0.06, 0.08, 0.14, 0.75))
            border.width: 1
            border.color: root.showCharPanel
                ? "#93c5fd"
                : Qt.rgba(1, 1, 1, 0.22)

            Row {
                id: charPillRow
                anchors.centerIn: parent
                spacing: 4

                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    color: vm.characterAccent
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "Chars [C]"
                    font.pixelSize: 9
                    font.bold: true
                    color: "#e6eefc"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                id: charPillMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.toggleCharPanel()
            }
        }

        // Subtle hover pill to toggle autonomous moving/walking
        Rectangle {
            id: moveBtnPill
            anchors { top: charBtnPill.top; left: charBtnPill.right; leftMargin: 6 }
            width:  movePillRow.implicitWidth + 12
            height: 20
            radius: 10
            visible: !root.showDiag && (windowHover.hovered || root.showCharPanel)
            color: vm.locomotionEnabled
                ? (movePillMouse.containsMouse
                    ? Qt.rgba(0.18, 0.65, 0.42, 0.90)
                    : Qt.rgba(0.12, 0.48, 0.32, 0.80))
                : (movePillMouse.containsMouse
                    ? Qt.rgba(0.16, 0.18, 0.24, 0.90)
                    : Qt.rgba(0.06, 0.08, 0.14, 0.75))
            border.width: 1
            border.color: vm.locomotionEnabled ? "#6ee7b7" : Qt.rgba(1, 1, 1, 0.22)

            Row {
                id: movePillRow
                anchors.centerIn: parent
                spacing: 4

                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    color: vm.locomotionEnabled ? "#34d399" : "#64748b"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: vm.locomotionEnabled ? "Move [M]" : "Still [M]"
                    font.pixelSize: 9
                    font.bold: true
                    color: "#e6eefc"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                id: movePillMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    vm.toggleLocomotion()
                    keyHandler.forceActiveFocus()
                }
            }
        }

        // ============================================================
        //  LAYER 8 — Developer diagnostics panel (press D or right-click)
        // ============================================================
        Rectangle {
            id: diagBox
            visible: root.showDiag
            anchors { top: parent.top; left: parent.left }
            width:  diagContent.implicitWidth  + 16
            height: diagContent.implicitHeight + 16
            color:  Qt.rgba(0.0, 0.0, 0.0, 0.84)
            radius: 6

            Column {
                id: diagContent
                anchors { top: parent.top; left: parent.left; margins: 8 }
                spacing: 2

                Text { text: "── ORGANISM ──"; font.pixelSize: 9; font.bold: true; color: "#aaffaa" }
                Text { text: "Character " + vm.characterName;                     font.pixelSize: 9; color: vm.characterAccent }
                Text { text: "Energy    " + (vm.energy    * 100).toFixed(1) + "%"; font.pixelSize: 9; color: "white" }
                Text { text: "Health    " + (vm.health    * 100).toFixed(1) + "%"; font.pixelSize: 9; color: "white" }
                Text { text: "Stress    " + (vm.stress    * 100).toFixed(1) + "%"; font.pixelSize: 9; color: "white" }
                Text { text: "Curiosity " + (vm.curiosity * 100).toFixed(1) + "%"; font.pixelSize: 9; color: "white" }
                Text { text: "Adapt     " + (vm.adaptation* 100).toFixed(1) + "%"; font.pixelSize: 9; color: "white" }
                Text { text: "Behaviour " + vm.behaviour;                          font.pixelSize: 9; color: "#ffdd88" }
                Text { text: "Tier      " + vm.qualityTier;                        font.pixelSize: 9; color: "white" }

                Text { text: "── ANIMATION ──"; font.pixelSize: 9; font.bold: true; color: "#aaddff"; topPadding: 4 }
                Text {
                    text: "AnimState " + vm.animState
                    font.pixelSize: 9
                    color: "#88ddff"
                }
                Text {
                    text: "Pulse     " + (vm.animPulse * 100).toFixed(0) + "%"
                    font.pixelSize: 9
                    color: vm.animPulse > 0.3 ? "#ff8844" : "white"
                }
                Text {
                    text: "Dragging  " + (root.isDragging ? "YES" : "no")
                    font.pixelSize: 9
                    color: root.isDragging ? "#ffdd44" : "white"
                }
                Text {
                    text: "XFade     " + anim.xfadeDuration + "ms"
                    font.pixelSize: 9
                    color: "white"
                }
                Text {
                    text: "Move      " + vm.locomotionMode +
                          (vm.isSitting ? " (Sit)" : (vm.edgePeekSide !== 0 ? " (Peek)" : "")) +
                          " [" + (vm.facingDir < 0 ? "L" : "R") + "]"
                    font.pixelSize: 9
                    color: vm.isWalking ? "#6ee7b7" : "#cbd5e1"
                }
                Text {
                    text: "WalkCtrl  " + (vm.locomotionEnabled ? "ON" : "OFF") +
                          " · (" + Math.round(root.x) + "," + Math.round(root.y) + ")"
                    font.pixelSize: 9
                    color: vm.locomotionEnabled ? "#a7f3d0" : "#94a3b8"
                }

                Text { text: "── HOST ──"; font.pixelSize: 9; font.bold: true; color: "#aaaaff"; topPadding: 4 }
                Text { text: "CPU  " + (vm.hostCpuValid ? vm.hostCpuPct.toFixed(1) + "%" : "N/A"); font.pixelSize: 9; color: vm.hostCpuValid ? "white" : "#ff8888" }
                Text { text: "RAM  " + (vm.hostRamValid ? vm.hostRamPct.toFixed(1) + "%" : "N/A"); font.pixelSize: 9; color: vm.hostRamValid ? "white" : "#ff8888" }
                Text { text: "Pressure " + (vm.pressure * 100).toFixed(1) + "%";  font.pixelSize: 9; color: "white" }

                Text { text: "── RUNTIME ──"; font.pixelSize: 9; font.bold: true; color: "#ffaaaa"; topPadding: 4 }
                Text { text: "Particles " + vm.activeParticles;                    font.pixelSize: 9; color: "white" }
                Text { text: "Sim Hz    " + vm.measuredSimHz.toFixed(1);           font.pixelSize: 9; color: "white" }
                Text { text: "Age       " + vm.simTimeSeconds.toFixed(0) + "s";    font.pixelSize: 9; color: "white" }
                Text {
                    text: vm.syntheticMode ? "● SYNTHETIC" : "● LIVE"
                    font.pixelSize: 9; font.bold: true
                    color: vm.syntheticMode ? "#ffcc00" : "#44ff88"
                    topPadding: 2
                }

                // Quick action link to switch characters directly from diag panel
                Rectangle {
                    width: switchDiagText.implicitWidth + 10
                    height: 16
                    radius: 4
                    color: switchDiagMouse.containsMouse
                        ? Qt.rgba(1, 1, 1, 0.20)
                        : Qt.rgba(1, 1, 1, 0.10)
                    Text {
                        id: switchDiagText
                        anchors.centerIn: parent
                        text: "Switch Character [C]"
                        font.pixelSize: 9
                        color: "#93c5fd"
                    }
                    MouseArea {
                        id: switchDiagMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleCharPanel()
                    }
                }
            }
        }

        // Shortcut hint
        Text {
            anchors { right: parent.right; bottom: parent.bottom; margins: 5 }
            text: "[C] chars · [M] move · [D] diag"
            font.pixelSize: 8
            color: Qt.rgba(1, 1, 1, 0.35)
            visible: !root.showDiag && !root.showCharPanel
            SequentialAnimation on opacity {
                running: true
                PauseAnimation { duration: 4500 }
                NumberAnimation { to: 0; duration: 1500 }
            }
        }
    }

    // ================================================================
    //  CHARACTER SELECTOR SIDE PANEL (opened via [C] or "Chars" pill)
    // ================================================================
    CharacterPanel {
        id: characterPanel
        visible: root.showCharPanel
        x: 278
        y: 10
        width: 224
        height: 280
        onCloseRequested: {
            root.showCharPanel = false
            keyHandler.forceActiveFocus()
        }
    }
}
