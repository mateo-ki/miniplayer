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

    RowLayout {
        anchors.fill: parent
        anchors.margins: theme ? theme.edgePadding : 20
        spacing: theme ? theme.gap : 14

        Rectangle {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            radius: 12
            color: theme ? theme.accentColor : "#f28c28"

            Text {
                anchors.centerIn: parent
                color: "#121212"
                text: "M"
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: 22
                font.bold: true
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 2

            Text {
                color: theme ? theme.textPrimaryColor : "#f3f3f3"
                text: "miniPlayer"
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.titleSize : 22
                font.bold: true
            }

            Text {
                color: theme ? theme.textMutedColor : "#858585"
                text: "Desktop shell foundation"
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.captionSize : 11
            }
        }

        Item {
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.preferredWidth: 320
            Layout.preferredHeight: 40
            radius: theme ? theme.controlRadius : 10
            color: theme ? theme.panelRaisedColor : "#242424"
            border.color: theme ? theme.subtleBorderColor : "#282828"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                Text {
                    color: theme ? theme.textMutedColor : "#858585"
                    text: "Search"
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                    font.pixelSize: theme ? theme.bodySize : 13
                }

                Item {
                    Layout.fillWidth: true
                }

                Text {
                    color: theme ? theme.accentColor : "#f28c28"
                    text: "Ctrl+K"
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                    font.pixelSize: theme ? theme.captionSize : 11
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 150
            Layout.preferredHeight: 40
            radius: theme ? theme.controlRadius : 10
            color: theme ? theme.panelRaisedColor : "#242424"
            border.color: theme ? theme.subtleBorderColor : "#282828"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    radius: 4
                    color: theme ? theme.successColor : "#6ec16e"
                }

                Text {
                    color: theme ? theme.textSecondaryColor : "#bebebe"
                    text: "Shell only"
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                    font.pixelSize: theme ? theme.bodySize : 13
                }
            }
        }

        Repeater {
            model: [
                { label: "Queue" },
                { label: "Prefs" },
                { label: "About" }
            ]

            delegate: ToolButton {
                id: navButton

                Layout.preferredWidth: 72
                Layout.preferredHeight: 40

                background: Rectangle {
                    radius: root.theme ? root.theme.controlRadius : 10
                    color: navButton.down ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                                          : (navButton.hovered ? (root.theme ? root.theme.panelRaisedColor : "#242424")
                                                               : "transparent")
                    border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                    border.width: 1
                }

                contentItem: Text {
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                    text: modelData.label
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.bodySize : 13
                }
            }
        }
    }
}
