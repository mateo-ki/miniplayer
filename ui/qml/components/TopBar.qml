import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property QtObject theme
    property string currentFileTitle: ""
    property string currentFileSubtitle: ""

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
                text: root.currentFileTitle.length > 0
                    ? root.currentFileTitle
                    : "Desktop shell foundation"
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.captionSize : 11
                elide: Text.ElideRight
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
                    text: root.currentFileTitle.length > 0 ? "Current file" : "Open local media"
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                    font.pixelSize: theme ? theme.bodySize : 13
                }

                Item {
                    Layout.fillWidth: true
                }

                Text {
                    color: theme ? theme.accentColor : "#f28c28"
                    text: root.currentFileSubtitle.length > 0 ? root.currentFileSubtitle : "Ctrl+O"
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                    font.pixelSize: theme ? theme.captionSize : 11
                    elide: Text.ElideLeft
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 170
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
                    text: root.currentFileTitle.length > 0 ? "Mock data loaded" : "Awaiting file open"
                    font.family: theme ? theme.fontFamily : "Segoe UI"
                    font.pixelSize: theme ? theme.bodySize : 13
                }
            }
        }
    }
}
