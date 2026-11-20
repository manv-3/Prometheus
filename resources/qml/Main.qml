import QtQuick
import QtQuick.Window
import Prometheus

/**
 * PROMETHEUS – Main organism window
 *
 * Transparent, frameless desktop window that visually represents the
 * organism's internal state in real time. Reads only from OrganismViewModel.
 * No simulation logic lives here — only visual bindings to snapshot data.
 *
 * Visual mapping:
 *   Body size       ← energy       (larger = more energetic)
 *   Body colour     ← behaviour    (each state has a distinct colour)
 *   Body opacity    ← health       (dimmer = damaged health)
 *   Stress ring     ← stress       (pulsing orange/red halo)
 *   Particle dots   ← activeParticles (scattered dots around body)
 *   Adaptation glow ← adaptation   (soft golden outer ring)
 *   Curiosity sparks← curiosity    (small orbiting sparks when high)
 *   Dormancy state  ← dormant      (shrinks, greyscale, slow breathing)
 *
 * Press 'D' to toggle the developer diagnostics overlay.
 * SYNTHETIC badge shown when syntheticMode == true (REQ-UI-02).
 */
Window {
    id: root

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"

    width:  260
    height: 260
    minimumWidth:  180
    minimumHeight: 180

    visible: true
    title: "PROMETHEUS"

    // ---- ViewModel ----
    OrganismViewModel { id: vm }

    // ---- Keyboard: toggle diagnostics overlay ----
    property bool showDiag: false
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_D) { root.showDiag = !root.showDiag }
    }

    // ---- Drag to reposition ----
    MouseArea {
        anchors.fill: parent
        property point startPos
        onPressed:  (m) => { startPos = Qt.point(m.x, m.y); root.requestActivate() }
        onPositionChanged: (m) => {
            if (pressed) { root.x += m.x - startPos.x; root.y += m.y - startPos.y }
        }
    }

    // ================================================================
    //  LAYER 1 — Adaptation glow (outermost, behind everything)
    // ================================================================
    Rectangle {
        id: adaptGlow
        anchors.centerIn: parent
        width:  bodyCircle.width + 32 + vm.adaptation * 24
        height: width
        radius: width / 2
        color:  "transparent"
        border.color: Qt.rgba(1.0, 0.85, 0.2, vm.adaptation * 0.45)
        border.width: 3 + vm.adaptation * 5
        visible: vm.adaptation > 0.05
        Behavior on width { NumberAnimation { duration: 600; easing.type: Easing.OutSine } }
    }

    // ================================================================
    //  LAYER 2 — Stress pulse ring
    // ================================================================
    Rectangle {
        id: pulseRing
        anchors.centerIn: parent
        width:  bodyCircle.width + 20
        height: width
        radius: width / 2
        color:  "transparent"
        border.color: Qt.rgba(1.0, 0.25, 0.05, vm.stress * 0.7)
        border.width: 2 + vm.stress * 4
        visible: vm.stress > 0.10

        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation {
                to: 0.15
                duration: vm.dormant ? 3000 : (500 + (1.0 - vm.stress) * 700)
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                to: 1.0
                duration: vm.dormant ? 3000 : (500 + (1.0 - vm.stress) * 700)
                easing.type: Easing.InOutSine
            }
        }
    }

    // ================================================================
    //  LAYER 3 — Particle field  (activeParticles drives density)
    // ================================================================
    // Render up to 12 visual dots whose positions orbit the body.
    // Count shown is proportional to vm.activeParticles (max 300 → 12 dots).
    // Positions are driven by continuous rotation — purely cosmetic.
    Item {
        id: particleField
        anchors.centerIn: parent
        width:  bodyCircle.width + 60
        height: width

        property int visibleDots: Math.min(12, Math.max(0,
            Math.round(vm.activeParticles / 25.0)))

        Repeater {
            model: 12
            delegate: Rectangle {
                property int   idx:    index
                property real  angle:  (idx / 12.0) * 360.0 + particleField.rotation
                property real  orbitR: (particleField.width / 2) - 6
                                       + Math.sin(idx * 1.3) * 10

                width:  visible ? (3 + (idx % 3))  : 0
                height: width
                radius: width / 2

                x: particleField.width  / 2 + orbitR * Math.cos(angle * Math.PI / 180) - width / 2
                y: particleField.height / 2 + orbitR * Math.sin(angle * Math.PI / 180) - height / 2

                color: {
                    switch (vm.behaviour) {
                        case "Exploring": return Qt.rgba(0.4, 0.75, 1.0, 0.8)
                        case "Observing": return Qt.rgba(0.7, 0.4,  1.0, 0.8)
                        case "Stressed":  return Qt.rgba(1.0, 0.5,  0.1, 0.8)
                        case "Resting":   return Qt.rgba(1.0, 0.95, 0.4, 0.8)
                        case "Dormant":   return Qt.rgba(0.5, 0.5,  0.55, 0.5)
                        default:          return Qt.rgba(0.5, 1.0,  0.6,  0.8)
                    }
                }

                visible: index < particleField.visibleDots && !vm.dormant
                Behavior on color { ColorAnimation { duration: 400 } }
            }
        }

        // Rotate the whole field — speed scales with curiosity and tier
        NumberAnimation on rotation {
            from: 0; to: 360; loops: Animation.Infinite
            duration: vm.dormant ? 999999
                : Math.max(3000, 15000 - vm.curiosity * 10000 - vm.qualityTier * 800)
        }
    }

    // ================================================================
    //  LAYER 4 — Main body circle
    // ================================================================
    Rectangle {
        id: bodyCircle
        anchors.centerIn: parent

        // Size: driven by energy. Dormant = minimum. Thriving = maximum.
        property real targetSize: vm.dormant
            ? 54
            : Math.round(54 + vm.energy * 72)

        width:  targetSize
        height: targetSize
        radius: width / 2

        // Health affects opacity — a damaged organism looks dim
        opacity: 0.55 + vm.health * 0.45

        // Colour encodes behaviour
        color: {
            if (vm.dormant) return Qt.rgba(0.22, 0.22, 0.26, 0.80)
            switch (vm.behaviour) {
                case "Exploring":  return Qt.rgba(0.15, 0.55, 1.00, 0.88)
                case "Observing":  return Qt.rgba(0.55, 0.18, 0.92, 0.88)
                case "Stressed":   return Qt.rgba(0.95, 0.35, 0.08, 0.88)
                case "Resting":    return Qt.rgba(0.85, 0.80, 0.15, 0.88)
                default:           return Qt.rgba(0.20, 0.80, 0.45, 0.88)  // Idle
            }
        }

        Behavior on width   { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
        Behavior on color   { ColorAnimation  { duration: 500 } }
        Behavior on opacity { NumberAnimation { duration: 600 } }

        // ---- Inner nucleus ----
        // A small brighter circle whose radius tracks health
        Rectangle {
            anchors.centerIn: parent
            width:  parent.width * (0.28 + vm.health * 0.22)
            height: width
            radius: width / 2
            color:  Qt.lighter(parent.color, 1.5)
            opacity: 0.6
            Behavior on width { NumberAnimation { duration: 400 } }
        }

        // ---- State icon / label ----
        Text {
            anchors.centerIn: parent
            text: {
                if (vm.dormant)              return "💤"
                switch (vm.behaviour) {
                    case "Exploring": return "✦"
                    case "Stressed":  return "⚠"
                    case "Resting":   return "♥"
                    case "Observing": return "👁"
                    default:          return "●"
                }
            }
            font.pixelSize: 14
            color: "white"
            opacity: 0.90
        }
    }

    // ================================================================
    //  LAYER 5 — Health bar (thin arc at the bottom of the body)
    // ================================================================
    Rectangle {
        id: healthBarBg
        anchors {
            horizontalCenter: parent.horizontalCenter
            top: bodyCircle.bottom
            topMargin: 4
        }
        width:  bodyCircle.width
        height: 4
        radius: 2
        color:  Qt.rgba(1, 1, 1, 0.15)

        Rectangle {
            width:  parent.width * vm.health
            height: parent.height
            radius: parent.radius
            color: {
                if (vm.health > 0.6) return Qt.rgba(0.3, 0.9, 0.4, 0.85)
                if (vm.health > 0.3) return Qt.rgba(0.9, 0.75, 0.1, 0.85)
                return Qt.rgba(0.9, 0.2, 0.1, 0.85)
            }
            Behavior on width { NumberAnimation { duration: 400 } }
            Behavior on color { ColorAnimation  { duration: 400 } }
        }
    }

    // ================================================================
    //  LAYER 6 — Compact status strip (bottom-left)
    // ================================================================
    Column {
        anchors {
            left:   parent.left
            bottom: parent.bottom
            margins: 7
        }
        spacing: 2

        Text {
            text: "⚡ " + Math.round(vm.energy    * 100) + "%"
            font.pixelSize: 9; color: Qt.rgba(1, 1, 1, 0.75)
        }
        Text {
            text: "🔥 " + Math.round(vm.stress    * 100) + "%"
            font.pixelSize: 9; color: Qt.rgba(1, 1, 1, 0.75)
        }
        Text {
            text: "T" + vm.qualityTier + "  " + vm.activeParticles + "p"
            font.pixelSize: 9; color: Qt.rgba(1, 1, 1, 0.70)
        }
    }

    // ================================================================
    //  LAYER 7 — SYNTHETIC badge  (REQ-UI-02)
    // ================================================================
    Rectangle {
        anchors { top: parent.top; right: parent.right; margins: 5 }
        width:  synLabel.implicitWidth + 8
        height: synLabel.implicitHeight + 4
        color:  Qt.rgba(0.9, 0.7, 0.0, 0.88)
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

    // ================================================================
    //  LAYER 8 — Developer diagnostics overlay  (press D to toggle)
    // ================================================================
    Rectangle {
        id: diagPanel
        visible: root.showDiag
        anchors { top: parent.top; left: parent.left; margins: 0 }
        width:  diagContent.implicitWidth  + 16
        height: diagContent.implicitHeight + 16
        color:  Qt.rgba(0.0, 0.0, 0.0, 0.82)
        radius: 6

        Column {
            id: diagContent
            anchors { top: parent.top; left: parent.left; margins: 8 }
            spacing: 3

            Text { text: "── ORGANISM ──"; font.pixelSize: 9; font.bold: true; color: "#aaffaa" }
            Text { text: "Energy    " + (vm.energy    * 100).toFixed(1) + "%"; font.pixelSize: 9; color: "white" }
            Text { text: "Health    " + (vm.health    * 100).toFixed(1) + "%"; font.pixelSize: 9; color: "white" }
            Text { text: "Stress    " + (vm.stress    * 100).toFixed(1) + "%"; font.pixelSize: 9; color: "white" }
            Text { text: "Curiosity " + (vm.curiosity * 100).toFixed(1) + "%"; font.pixelSize: 9; color: "white" }
            Text { text: "Adapt     " + (vm.adaptation* 100).toFixed(1) + "%"; font.pixelSize: 9; color: "white" }
            Text { text: "Behaviour " + vm.behaviour;                          font.pixelSize: 9; color: "#ffdd88" }
            Text { text: "Tier      " + vm.qualityTier;                        font.pixelSize: 9; color: "white" }

            Text { text: "── HOST ──"; font.pixelSize: 9; font.bold: true; color: "#aaaaff"; topPadding: 4 }
            Text {
                text: "CPU  " + (vm.hostCpuValid ? vm.hostCpuPct.toFixed(1) + "%" : "N/A")
                font.pixelSize: 9; color: vm.hostCpuValid ? "white" : "#ff8888"
            }
            Text {
                text: "RAM  " + (vm.hostRamValid ? vm.hostRamPct.toFixed(1) + "%" : "N/A")
                font.pixelSize: 9; color: vm.hostRamValid ? "white" : "#ff8888"
            }
            Text { text: "Pressure " + (vm.pressure * 100).toFixed(1) + "%";  font.pixelSize: 9; color: "white" }

            Text { text: "── RUNTIME ──"; font.pixelSize: 9; font.bold: true; color: "#ffaaaa"; topPadding: 4 }
            Text { text: "Particles " + vm.activeParticles;                     font.pixelSize: 9; color: "white" }
            Text { text: "Sim Hz    " + vm.measuredSimHz.toFixed(1);            font.pixelSize: 9; color: "white" }
            Text { text: "Age       " + vm.simTimeSeconds.toFixed(0) + "s";     font.pixelSize: 9; color: "white" }
            Text {
                text: vm.syntheticMode ? "● SYNTHETIC" : "● LIVE"
                font.pixelSize: 9; font.bold: true
                color: vm.syntheticMode ? "#ffcc00" : "#44ff88"
                topPadding: 2
            }
        }
    }

    // ================================================================
    //  Press 'D' hint (bottom-right corner, fades after 4s)
    // ================================================================
    Text {
        id: diagHint
        anchors { right: parent.right; bottom: parent.bottom; margins: 5 }
        text: "[D] diag"
        font.pixelSize: 8
        color: Qt.rgba(1, 1, 1, 0.35)
        visible: !root.showDiag

        SequentialAnimation on opacity {
            running: true
            PauseAnimation { duration: 4000 }
            NumberAnimation { to: 0; duration: 1500 }
        }
    }
}
