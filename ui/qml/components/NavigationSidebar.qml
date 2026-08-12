import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    width: 64
    color: appTheme.chromeColor

    property int currentIndex: 1
    property var theme
    readonly property int logoHeight: 56
    readonly property var navItems: [
        { icon: "\u25b6", label: "\u89c6\u9891", page: 1 },
        { icon: "\u25a6", label: "\u77ed\u89c6\u9891", page: 6 },
        { icon: "\u25ce", label: "\u8d44\u8baf", page: 8 },
        { icon: "\u25a1", label: "\u56fe\u7247", page: 4 },
        { icon: "\u266a", label: "\u97f3\u4e50", page: 10 },
        { icon: "\u263a", label: "\u8868\u60c5", page: 5 },
        { icon: "\u25c9", label: "\u8bed\u97f3", page: 7 },
        { icon: "\u2699", label: "\u7ad9\u70b9", page: 9 }
    ]
    readonly property var bottomNavItems: [
        { icon: "\u21ba", label: "\u5386\u53f2", page: 2 },
        { icon: "\u21e9", label: "\u4e0b\u8f7d", page: 3 }
    ]
    readonly property real navButtonHeight: Math.max(50, Math.min(62, (height - logoHeight) / (navItems.length + bottomNavItems.length)))

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Logo area
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.logoHeight
            color: "transparent"

            Rectangle {
                anchors.centerIn: parent
                width: 38
                height: 38
                radius: 10
                color: theme.surfaceColor
                border.color: theme.subtleBorderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "M"
                    color: theme.accentColor
                    font.pixelSize: 20
                    font.bold: true
                    font.family: theme.fontFamily
                }
            }
        }

        // Navigation buttons
        Repeater {
            model: root.navItems

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.navButtonHeight
                color: "transparent"

                MouseArea {
                    id: bottomMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.currentIndex = modelData.page
                }

                Rectangle {
                    width: 3
                    height: 28
                    radius: 2
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    color: theme.accentColor
                    visible: root.currentIndex === modelData.page
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 48
                    height: 44
                    radius: 12
                    color: root.currentIndex === modelData.page
                        ? theme.panelRaisedColor
                        : (bottomMouseArea.containsMouse ? theme.panelColor : "transparent")
                    border.color: root.currentIndex === modelData.page ? theme.subtleBorderColor : "transparent"
                    border.width: 1
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 4

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.icon
                        font.pixelSize: 19
                        color: root.currentIndex === modelData.page ? theme.accentColor : theme.textSecondaryColor
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.label
                        font.pixelSize: 10
                        font.family: theme.fontFamily
                        color: root.currentIndex === modelData.page ? theme.accentColor : theme.textMutedColor
                    }
                }
            }
        }

        // Spacer
        Item { Layout.fillHeight: true }

        Repeater {
            model: root.bottomNavItems

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.navButtonHeight
                color: "transparent"

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.currentIndex = modelData.page
                }

                Rectangle {
                    width: 3
                    height: 28
                    radius: 2
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    color: theme.accentColor
                    visible: root.currentIndex === modelData.page
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 48
                    height: 44
                    radius: 12
                    color: root.currentIndex === modelData.page
                        ? theme.panelRaisedColor
                        : (mouseArea.containsMouse ? theme.panelColor : "transparent")
                    border.color: root.currentIndex === modelData.page ? theme.subtleBorderColor : "transparent"
                    border.width: 1
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 4

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.icon
                        font.pixelSize: 19
                        color: root.currentIndex === modelData.page ? theme.accentColor : theme.textSecondaryColor
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.label
                        font.pixelSize: 10
                        font.family: theme.fontFamily
                        color: root.currentIndex === modelData.page ? theme.accentColor : theme.textMutedColor
                    }
                }
            }
        }
    }
}
