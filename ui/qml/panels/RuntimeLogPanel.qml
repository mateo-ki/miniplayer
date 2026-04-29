import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property QtObject theme

    color: theme ? theme.chromeColor : "#171717"
    radius: theme ? theme.panelRadius : 14
    border.color: theme ? theme.subtleBorderColor : "#282828"
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme ? theme.edgePadding : 20
        spacing: 12

        RowLayout {
            spacing: 8

            Text {
                color: theme ? theme.textPrimaryColor : "#f3f3f3"
                text: "Runtime Log"
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.sectionTitleSize : 16
                font.bold: true
            }

            Rectangle {
                Layout.preferredWidth: 76
                Layout.preferredHeight: 24
                radius: 12
                color: theme ? theme.accentMutedColor : "#7a4a17"

                Text {
                    anchors.centerIn: parent
                    color: theme ? theme.accentColor : "#f28c28"
                    text: "shell mock"
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                    font.pixelSize: theme ? theme.captionSize : 11
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Text {
                color: theme ? theme.textMutedColor : "#858585"
                text: "No backend wires yet"
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.captionSize : 11
            }
        }

        ScrollView {
            id: logScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            background: Rectangle {
                radius: root.theme ? root.theme.controlRadius : 10
                color: "#121212"
                border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                border.width: 1
            }

            Item {
                width: logScroll.availableWidth
                implicitHeight: logColumn.height + 28

                Column {
                    id: logColumn

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 14
                    spacing: 8

                    Repeater {
                        model: [
                            { level: "INFO", message: "QML shell loaded from MiniPlayer.Main." },
                            { level: "INFO", message: "Theme constants applied to chrome and panels." },
                            { level: "WARN", message: "Playback transport remains disconnected for Task 2." },
                            { level: "INFO", message: "Log panel reserves room for future runtime events." }
                        ]

                        delegate: Rectangle {
                            width: logColumn.width
                            height: 34
                            radius: root.theme ? root.theme.controlRadius : 10
                            color: "#181818"
                            border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 10

                                Text {
                                    Layout.preferredWidth: 40
                                    color: modelData.level === "WARN"
                                        ? (root.theme ? root.theme.warningColor : "#f0b34a")
                                        : (root.theme ? root.theme.accentColor : "#f28c28")
                                    text: modelData.level
                                    font.family: "Consolas"
                                    font.pixelSize: root.theme ? root.theme.captionSize : 11
                                }

                                Text {
                                    Layout.fillWidth: true
                                    color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                                    text: modelData.message
                                    elide: Text.ElideRight
                                    font.family: "Consolas"
                                    font.pixelSize: root.theme ? root.theme.captionSize : 11
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
