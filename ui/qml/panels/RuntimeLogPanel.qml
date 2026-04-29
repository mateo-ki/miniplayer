import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property QtObject theme
    property string badgeText: "shell mock"
    property string statusText: "Ready for RuntimeLogModel"
    property var logModel

    color: theme ? theme.chromeColor : "#171717"
    radius: theme ? theme.panelRadius : 14
    border.color: theme ? theme.subtleBorderColor : "#282828"
    border.width: 1

    function levelColor(level) {
        if (level === "ERROR")
            return theme ? theme.dangerColor : "#e06c63"
        if (level === "WARN")
            return theme ? theme.warningColor : "#f0b34a"
        return theme ? theme.accentColor : "#f28c28"
    }

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
                Layout.preferredWidth: 92
                Layout.preferredHeight: 24
                radius: 12
                color: theme ? theme.accentMutedColor : "#7a4a17"

                Text {
                    anchors.centerIn: parent
                    color: theme ? theme.accentColor : "#f28c28"
                    text: root.badgeText
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                    font.pixelSize: theme ? theme.captionSize : 11
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Text {
                color: theme ? theme.textMutedColor : "#858585"
                text: root.statusText
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.captionSize : 11
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: root.theme ? root.theme.controlRadius : 10
            color: "#121212"
            border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
            border.width: 1

            ListView {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8
                clip: true
                model: root.logModel

                delegate: Rectangle {
                    width: ListView.view.width
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
                            Layout.preferredWidth: 46
                            color: root.levelColor(level)
                            text: level
                            font.family: "Consolas"
                            font.pixelSize: root.theme ? root.theme.captionSize : 11
                        }

                        Text {
                            Layout.fillWidth: true
                            color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                            text: message
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
