import QtQuick
import QtQuick
import QtQuick.Window

/**
 * PROMETHEUS – Main organism window
 *
 * The organism body is now the cat SVG asset.
 * All behaviour-driven animations are applied via QML properties layered
 * on top of the image — no SMIL animation required from the SVG itself.
 *
 * Visual mapping:
 *   Body size       ← energy       (larger = more energetic)
 *   Body opacity    ← health       (dimmer = damaged)
 *   Hue rotation    ← behaviour    (colour shift per state)
 *   Stress ring     ← stress       (pulsing halo)
 *   Particle dots   ← activeParticles (orbiting dots)
 *   Adaptation glow ← adaptation   (golden outer ring)
 *   Dormancy        ← dormant      (shrinks, greyscale, slow breathing)
 *   Bounce anim     ← curiosity    (subtle bounce when curious)
 *
 * Press D to toggle the developer diagnostics panel.
 * SYNTHETIC badge shown when syntheticMode == true (REQ-UI-02).
 */
Window {
    id: root

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"

    width:  280
    height: 300
    minimumWidth:  180
    minimumHeight: 200

    visible: true
    title: "PROMETHEUS"

    // ---- ViewModel is exposed as "vm" context property from C++ (main.cpp) ----
    // Do NOT instantiate OrganismViewModel here — it lives on the C++ side.

    // ---- Keyboard: toggle diagnostics ----
    property bool showDiag: false
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_D) root.showDiag = !root.showDiag
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
    //  LAYER 1 — Adaptation glow (outermost golden ring)
    // ================================================================
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

    // ================================================================
    //  LAYER 2 — Stress pulse ring
    // ================================================================
    Rectangle {
        id: pulseRing
        anchors.centerIn: catBody
        width:  catBody.width  + 14
        height: catBody.height + 14
        radius: width / 2
        color: "transparent"
        border.color: Qt.rgba(1.0, 0.2, 0.05, vm.stress * 0.75)
        border.width: 2 + vm.stress * 5
        visible: vm.stress > 0.10

        SequentialAnimation on opacity {
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
    }

    // ================================================================
    //  LAYER 3 — Orbiting particle dots
    // ================================================================
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

        // Orbit rotation — speed driven by curiosity and tier
        NumberAnimation on rotation {
            from: 0; to: 360; loops: Animation.Infinite
            duration: vm.dormant ? 999999
                : Math.max(4000, 18000 - vm.curiosity * 12000 - vm.qualityTier * 1000)
        }
    }

    // ================================================================
    //  LAYER 4 — The cat SVG organism body
    // ================================================================
    Item {
        id: catBody
        anchors {
            horizontalCenter: parent.horizontalCenter
            top: parent.top
            topMargin: 10
        }

        // Size driven by energy — dormant is smallest
        property real targetSize: vm.dormant
            ? 90
            : Math.round(90 + vm.energy * 80)

        width:  targetSize
        height: targetSize

        // Smooth size changes
        Behavior on width  { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }

        // Dormancy greyscale + dim; health dims the image further
        opacity: vm.dormant
            ? 0.5
            : (0.55 + vm.health * 0.45)
        Behavior on opacity { NumberAnimation { duration: 600 } }

        // Greyscale filter when dormant using saturation = 0
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

        // The cat SVG
        Image {
            id: catImage
            anchors.fill: parent
            source: "qrc:/Prometheus/resources/organism.svg"
            fillMode: Image.PreserveAspectFit
            smooth: true
            antialiasing: true

            // Colour tint overlay driven by behaviour (using Item + Rectangle blend)
        }

        // Behaviour colour tint (semi-transparent overlay blended on top)
        Rectangle {
            anchors.fill: catImage
            radius: width / 2
            opacity: vm.dormant ? 0.0 : 0.18
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

        // Curious bounce animation
        SequentialAnimation on y {
            running: vm.curiosity > 0.65 && !vm.dormant
            loops: Animation.Infinite
            NumberAnimation { to: -6; duration: 350; easing.type: Easing.OutQuad }
            NumberAnimation { to:  0; duration: 350; easing.type: Easing.InQuad }
            PauseAnimation   { duration: Math.max(200, 1200 - vm.curiosity * 1000) }
        }

        // Dormancy slow breathing scale
        SequentialAnimation on scale {
            running: vm.dormant
            loops: Animation.Infinite
            NumberAnimation { to: 0.95; duration: 2500; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.00; duration: 2500; easing.type: Easing.InOutSine }
        }
    }

    // ================================================================
    //  LAYER 5 — Health bar (below cat)
    // ================================================================
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

    // ================================================================
    //  LAYER 6 — Status strip (bottom-left)
    // ================================================================
    Column {
        anchors { left: parent.left; bottom: parent.bottom; margins: 7 }
        spacing: 2

        Text { text: "⚡ " + Math.round(vm.energy    * 100) + "%"; font.pixelSize: 9; color: Qt.rgba(1,1,1,0.75) }
        Text { text: "🔥 " + Math.round(vm.stress    * 100) + "%"; font.pixelSize: 9; color: Qt.rgba(1,1,1,0.75) }
        Text { text: "T" + vm.qualityTier + "  " + vm.activeParticles + "p"; font.pixelSize: 9; color: Qt.rgba(1,1,1,0.70) }
    }

    // ================================================================
    //  LAYER 7 — SYNTHETIC badge (REQ-UI-02)
    // ================================================================
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

    // ================================================================
    //  LAYER 8 — Developer diagnostics panel (press D)
    // ================================================================
    Rectangle {
        visible: root.showDiag
        anchors { top: parent.top; left: parent.left }
        width:  diagContent.implicitWidth  + 16
        height: diagContent.implicitHeight + 16
        color:  Qt.rgba(0.0, 0.0, 0.0, 0.84)
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
        }
    }

    // Press D hint
    Text {
        anchors { right: parent.right; bottom: parent.bottom; margins: 5 }
        text: "[D] diag"
        font.pixelSize: 8
        color: Qt.rgba(1, 1, 1, 0.3)
        visible: !root.showDiag
        SequentialAnimation on opacity {
            running: true
            PauseAnimation { duration: 4000 }
            NumberAnimation { to: 0; duration: 1500 }
        }
    }
}
