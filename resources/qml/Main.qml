import QtQuick
import QtQuick.Window
import Prometheus

/**
 * PROMETHEUS – Main organism window
 *
 * Transparent, frameless desktop window (TASK-1.6).
 * Reads only from OrganismViewModel (read-only Q_PROPERTYs).
 * No simulation logic lives here — only visual bindings.
 *
 * Visual mapping (placeholder, Phase 1):
 *   - Body radius   ← energy (larger = more energetic)
 *   - Body colour   ← behaviour (green=Idle, blue=Exploring, orange=Stressed,
 *                                 grey=Dormant, yellow=Resting, purple=Observing)
 *   - Pulse opacity ← stress (higher stress = more visible pulse ring)
 *   - Particle count indicator at bottom-left
 *
 * SYNTHETIC overlay shown when syntheticMode == true (REQ-UI-02).
 */
Window {
    id: root

    // Frameless transparent desktop window
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"

    width:  200
    height: 200

    minimumWidth:  120
    minimumHeight: 120

    visible: true
    title: "PROMETHEUS"

    // ---- ViewModel binding ----
    OrganismViewModel {
        id: vm
    }

    // ---- Drag to reposition ----
    MouseArea {
        id: dragArea
        anchors.fill: parent
        property point startPos

        onPressed: (mouse) => { startPos = Qt.point(mouse.x, mouse.y) }
        onPositionChanged: (mouse) => {
            if (pressed) {
                root.x += mouse.x - startPos.x
                root.y += mouse.y - startPos.y
            }
        }
    }

    // ---- Stress pulse ring (behind body) ----
    Rectangle {
        id: pulseRing
        anchors.centerIn: parent
        width:  bodyCircle.width  + 24
        height: bodyCircle.height + 24
        radius: width / 2
        color:  "transparent"
        border.color: Qt.rgba(1, 0.3, 0.1, vm.stress * 0.6)
        border.width: 3

        // Pulse animation driven by stress level
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { to: 0.2; duration: 400 + (1.0 - vm.stress) * 800 }
            NumberAnimation { to: 1.0; duration: 400 + (1.0 - vm.stress) * 800 }
        }
        visible: vm.stress > 0.15
    }

    // ---- Main organism body ----
    Rectangle {
        id: bodyCircle
        anchors.centerIn: parent

        // Radius scales with energy: dormant is smallest, thriving is largest
        width:  Math.round(60 + vm.energy * 60)
        height: width
        radius: width / 2

        // Colour encodes behaviour
        color: {
            switch (vm.behaviour) {
                case "Exploring":  return Qt.rgba(0.2, 0.6, 1.0, 0.85)
                case "Observing":  return Qt.rgba(0.6, 0.2, 0.9, 0.85)
                case "Stressed":   return Qt.rgba(1.0, 0.45, 0.1, 0.85)
                case "Resting":    return Qt.rgba(0.9, 0.85, 0.2, 0.85)
                case "Dormant":    return Qt.rgba(0.3, 0.3, 0.35, 0.70)
                default:           return Qt.rgba(0.3, 0.85, 0.5, 0.85) // Idle
            }
        }

        Behavior on width  { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
        Behavior on color  { ColorAnimation  { duration: 400 } }

        // ---- Behaviour label ----
        Text {
            anchors.centerIn: parent
            text: vm.dormant ? "💤" : vm.behaviour
            font.pixelSize: 11
            color: "white"
            opacity: 0.9
        }
    }

    // ---- Bottom-left diagnostics ----
    Column {
        anchors {
            left:   parent.left
            bottom: parent.bottom
            margins: 6
        }
        spacing: 2

        Text {
            text: "⚡ " + Math.round(vm.energy    * 100) + "%"
            font.pixelSize: 9
            color: Qt.rgba(1, 1, 1, 0.75)
        }
        Text {
            text: "🔥 " + Math.round(vm.stress    * 100) + "%"
            font.pixelSize: 9
            color: Qt.rgba(1, 1, 1, 0.75)
        }
        Text {
            text: "T" + vm.qualityTier + " · " + vm.activeParticles + "p"
            font.pixelSize: 9
            color: Qt.rgba(1, 1, 1, 0.75)
        }
    }

    // ---- SYNTHETIC telemetry overlay (REQ-UI-02) ----
    Rectangle {
        anchors {
            top:   parent.top
            right: parent.right
            margins: 4
        }
        width:  synLabel.implicitWidth + 8
        height: synLabel.implicitHeight + 4
        color:  Qt.rgba(0.9, 0.7, 0.0, 0.85)
        radius: 3
        visible: vm.syntheticMode

        Text {
            id: synLabel
            anchors.centerIn: parent
            text: "SYNTHETIC"
            font.pixelSize: 8
            font.bold: true
            color: "black"
        }
    }
}
