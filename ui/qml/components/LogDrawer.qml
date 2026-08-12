import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property QtObject theme
    property bool isOpen: false
    property string badgeText: "controller"
    property string statusText: "Idle"
    property var logModel

    function open() {
        isOpen = true
        autoHideTimer.restart()
    }

    function close() {
        isOpen = false
        autoHideTimer.stop()
    }

    function toggle() {
        if (isOpen) close()
        else open()
    }

    width: 400
    x: parent.width - (isOpen ? width : 0)
    z: 50

    color: theme ? theme.panelColor : "#1c1c1c"
    border.color: theme ? theme.subtleBorderColor : "#282828"
    border.width: 1

    Behavior on x {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    Timer {
        id: autoHideTimer
        interval: 3000
        repeat: false
        onTriggered: root.close()
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onContainsMouseChanged: {
            if (containsMouse) {
                autoHideTimer.stop()
            } else if (root.isOpen) {
                autoHideTimer.restart()
            }
        }
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

            Item { Layout.fillWidth: true }

            Text {
                color: theme ? theme.textMutedColor : "#858585"
                text: root.statusText
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.captionSize : 11
            }

            Rectangle {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                radius: 12
                color: closeMouseArea.containsMouse ? (theme ? theme.panelRaisedColor : "#242424") : "transparent"

                HoverHandler { id: closeMouseArea }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }

                Text {
                    anchors.centerIn: parent
                    color: theme ? theme.textMutedColor : "#858585"
                    text: "×"
                    font.pixelSize: 16
                }
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
                            color: level === "ERROR" ? (root.theme ? root.theme.dangerColor : "#e06c63")
                                : level === "WARN" ? (root.theme ? root.theme.warningColor : "#f0b34a")
                                : (root.theme ? root.theme.accentColor : "#f28c28")
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
