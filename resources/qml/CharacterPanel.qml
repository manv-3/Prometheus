import QtQuick

/**
 * CharacterPanel.qml — Companion Character Selector Panel
 *
 * Allows switching between built-in PROMETHEUS organism characters
 * (Ember Cat, Aegis Owl, Spark Fox, Atlas Bot, Nova Slime) or dropping
 * a local .svg / .png / .jpg / .webp image for a custom companion.
 *
 * Pure UI presentation — emits closeRequested() when dismissed and reads/calls
 * `vm.availableCharacters`, `vm.characterId`, `vm.selectCharacter(...)`, and
 * `vm.selectCustomCharacter(...)`.
 */
Rectangle {
    id: panel

    signal closeRequested()

    property string dropError: ""

    color: Qt.rgba(0.06, 0.08, 0.13, 0.93)
    radius: 10
    border.color: Qt.rgba(1.0, 1.0, 1.0, 0.16)
    border.width: 1

    // Absorb mouse clicks inside the panel so they don't drag the parent window
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: (m) => { m.accepted = true }
    }

    // ---- Header ----
    Row {
        id: headerRow
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            margins: 10
        }
        height: 20

        Text {
            text: "Companion Skin"
            font.pixelSize: 11
            font.bold: true
            color: "#eef4ff"
            width: parent.width - closeBtn.width
            anchors.verticalCenter: parent.verticalCenter
        }

        Rectangle {
            id: closeBtn
            width: 20
            height: 20
            radius: 10
            color: closeHover.containsMouse
                ? Qt.rgba(1.0, 1.0, 1.0, 0.22)
                : Qt.rgba(1.0, 1.0, 1.0, 0.08)
            anchors.verticalCenter: parent.verticalCenter

            Text {
                anchors.centerIn: parent
                text: "✕"
                font.pixelSize: 10
                color: "#d8e2f3"
            }

            MouseArea {
                id: closeHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: panel.closeRequested()
            }
        }
    }

    // ---- Scrollable character grid ----
    Flickable {
        id: gridFlick
        anchors {
            top: headerRow.bottom
            topMargin: 8
            left: parent.left
            leftMargin: 10
            right: parent.right
            rightMargin: 10
            bottom: dropFooter.top
            bottomMargin: 8
        }
        contentWidth: width
        contentHeight: charGrid.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Grid {
            id: charGrid
            width: gridFlick.width
            columns: 2
            spacing: 6

            Repeater {
                model: vm.availableCharacters
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    readonly property bool isActive: vm.characterId === modelData.id

                    width: Math.floor((charGrid.width - charGrid.spacing) / 2)
                    height: 70
                    radius: 7
                    color: isActive
                        ? Qt.rgba(1.0, 1.0, 1.0, 0.14)
                        : (cardMouse.containsMouse
                            ? Qt.rgba(1.0, 1.0, 1.0, 0.09)
                            : Qt.rgba(1.0, 1.0, 1.0, 0.04))
                    border.width: isActive ? 1.5 : 1
                    border.color: isActive
                        ? modelData.accent
                        : (cardMouse.containsMouse
                            ? Qt.rgba(1.0, 1.0, 1.0, 0.28)
                            : Qt.rgba(1.0, 1.0, 1.0, 0.10))

                    scale: cardMouse.containsMouse ? 1.02 : 1.0
                    Behavior on scale { NumberAnimation { duration: 100 } }
                    Behavior on color { ColorAnimation { duration: 120 } }

                    // Active badge dot
                    Rectangle {
                        visible: card.isActive
                        width: 7
                        height: 7
                        radius: 3.5
                        color: card.modelData.accent
                        anchors { top: parent.top; right: parent.right; margins: 5 }
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        width: parent.width - 8

                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 34
                            height: 34
                            source: card.modelData.source
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            antialiasing: true
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: card.modelData.name
                            font.pixelSize: 10
                            font.bold: card.isActive
                            color: card.isActive ? "#ffffff" : "#d8e0ee"
                            elide: Text.ElideRight
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: card.modelData.tagline
                            font.pixelSize: 8
                            color: "#8f9cb5"
                            elide: Text.ElideRight
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    MouseArea {
                        id: cardMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            panel.dropError = ""
                            vm.selectCharacter(card.modelData.id)
                        }
                    }
                }
            }

            // Custom uploaded/dropped character slot (shown once a custom file was loaded)
            Rectangle {
                id: customCard
                visible: vm.customCharacterPath.length > 0
                readonly property bool isActive: vm.characterId === "custom"

                width: Math.floor((charGrid.width - charGrid.spacing) / 2)
                height: 70
                radius: 7
                color: isActive
                    ? Qt.rgba(0.75, 0.52, 0.99, 0.18)
                    : (customMouse.containsMouse
                        ? Qt.rgba(1.0, 1.0, 1.0, 0.09)
                        : Qt.rgba(1.0, 1.0, 1.0, 0.04))
                border.width: isActive ? 1.5 : 1
                border.color: isActive ? "#c084fc" : Qt.rgba(1.0, 1.0, 1.0, 0.12)

                Rectangle {
                    visible: customCard.isActive
                    width: 7
                    height: 7
                    radius: 3.5
                    color: "#c084fc"
                    anchors { top: parent.top; right: parent.right; margins: 5 }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 2
                    width: parent.width - 8

                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 34
                        height: 34
                        source: vm.customCharacterSource
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        antialiasing: true
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Custom"
                        font.pixelSize: 10
                        font.bold: customCard.isActive
                        color: customCard.isActive ? "#ffffff" : "#d8e0ee"
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Local asset"
                        font.pixelSize: 8
                        color: "#8f9cb5"
                    }
                }

                MouseArea {
                    id: customMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        panel.dropError = ""
                        vm.selectCharacter("custom")
                    }
                }
            }
        }
    }

    // ---- Custom file drop zone footer ----
    Rectangle {
        id: dropFooter
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: 10
        }
        height: 30
        radius: 6
        color: fileDrop.containsDrag
            ? Qt.rgba(0.35, 0.65, 1.0, 0.22)
            : Qt.rgba(1.0, 1.0, 1.0, 0.05)
        border.width: 1
        border.color: fileDrop.containsDrag
            ? "#60a5fa"
            : (panel.dropError.length > 0 ? "#f87171" : Qt.rgba(1.0, 1.0, 1.0, 0.16))

        Text {
            anchors.centerIn: parent
            width: parent.width - 12
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            font.pixelSize: 9
            color: panel.dropError.length > 0
                ? "#fca5a5"
                : (fileDrop.containsDrag ? "#bfdbfe" : "#9aa8c4")
            text: {
                if (panel.dropError.length > 0) return panel.dropError
                if (fileDrop.containsDrag) return "Release to load character"
                return "Drop .svg / .png here for custom"
            }
        }
    }

    DropArea {
        id: fileDrop
        anchors.fill: parent
        onEntered: (drag) => {
            drag.accepted = drag.hasUrls && drag.urls.length > 0
        }
        onDropped: (drop) => {
            if (!drop.hasUrls || drop.urls.length === 0) {
                panel.dropError = "No file found"
                return
            }
            const ok = vm.selectCustomCharacter(drop.urls[0].toString())
            if (ok) {
                panel.dropError = ""
                drop.acceptProposedAction()
            } else {
                panel.dropError = "Use local .svg/.png/.jpg/.webp"
            }
        }
    }
}
