import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property QtObject theme

    color: theme ? theme.surfaceColor : "#101010"
    radius: theme ? theme.panelRadius : 14
    border.color: theme ? theme.subtleBorderColor : "#282828"
    border.width: 1

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: root.radius - 1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#191919" }
            GradientStop { position: 0.55; color: "#101010" }
            GradientStop { position: 1.0; color: "#0b0b0b" }
        }
    }

    Rectangle {
        width: Math.min(parent.width - 60, parent.height * 1.55)
        height: width / 1.77
        anchors.centerIn: parent
        radius: theme ? theme.panelRadius : 14
        color: "#050505"
        border.color: theme ? theme.borderColor : "#353535"
        border.width: 1

        Rectangle {
            anchors.fill: parent
            anchors.margins: 18
            radius: root.theme ? root.theme.controlRadius : 10
            color: "#0d0d0d"
            border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
            border.width: 1
        }

        Column {
            anchors.centerIn: parent
            spacing: 12

            Rectangle {
                width: 88
                height: 88
                radius: 44
                anchors.horizontalCenter: parent.horizontalCenter
                color: root.theme ? root.theme.accentMutedColor : "#7a4a17"
                border.color: root.theme ? root.theme.accentColor : "#f28c28"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    color: root.theme ? root.theme.accentColor : "#f28c28"
                    text: ">"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: 40
                    font.bold: true
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                text: "Video surface placeholder"
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: root.theme ? root.theme.sectionTitleSize : 16
                font.bold: true
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                color: root.theme ? root.theme.textMutedColor : "#858585"
                text: "PlayerController stays disconnected in Task 2.\nThis pane reserves the largest region for future rendering."
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: root.theme ? root.theme.bodySize : 13
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 20
        width: 144
        height: 34
        radius: theme ? theme.controlRadius : 10
        color: "#0f0f0f"
        border.color: theme ? theme.subtleBorderColor : "#282828"
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: root.theme ? root.theme.warningColor : "#f0b34a"
            }

            Text {
                color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                text: "Preview staging"
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: root.theme ? root.theme.captionSize : 11
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        width: 200
        height: 74
        radius: theme ? theme.controlRadius : 10
        color: "#0f0f0f"
        border.color: theme ? theme.subtleBorderColor : "#282828"
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 4

            Text {
                color: root.theme ? root.theme.textMutedColor : "#858585"
                text: "Viewport"
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: root.theme ? root.theme.captionSize : 11
            }

            Text {
                color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                text: "1920 x 1080"
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: root.theme ? root.theme.sectionTitleSize : 16
                font.bold: true
            }

            Text {
                color: root.theme ? root.theme.textMutedColor : "#858585"
                text: "GPU handoff pending later tasks"
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: root.theme ? root.theme.captionSize : 11
            }
        }
    }
}
