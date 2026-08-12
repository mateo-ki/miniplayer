import QtQuick
import QtQuick.Controls

Window {
    id: root

    required property var hostWindow
    required property var controller
    required property var theme
    property bool pageActive: false
    property bool playbackActive: false
    property bool sidebarVisible: false
    property real sidebarWidth: 0
    property string sourceTitle: "短视频"
    property string errorMessage: ""
    property int prefetchCount: 0
    property real pressY: 0
    property real rememberedVolume: 1.0

    signal togglePlayPauseRequested()
    signal wheelScrolled(real delta)
    signal nextRequested()
    signal retryRequested()

    width: Math.max(1, hostWindow.width - (sidebarVisible ? sidebarWidth : 0))
    height: Math.max(1, hostWindow.height)
    x: hostWindow.x + (sidebarVisible ? sidebarWidth : 0)
    y: hostWindow.y
    visible: hostWindow.visible && pageActive && playbackActive
    flags: Qt.Tool | Qt.FramelessWindowHint
    transientParent: hostWindow
    color: "transparent"

    onVisibleChanged: if (visible) raise()

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        onPressed: function(mouse) { root.pressY = mouse.y }
        onReleased: function(mouse) {
            var delta = mouse.y - root.pressY
            if (Math.abs(delta) >= 72)
                root.nextRequested()
            else
                root.togglePlayPauseRequested()
        }
        onWheel: function(wheel) {
            root.wheelScrolled(wheel.angleDelta.y)
            wheel.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: "#16000000"
        border.width: 1
    }

    Column {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 28
        anchors.bottomMargin: 28
        spacing: 6
        z: 3

        Text {
            text: root.sourceTitle
            color: "#ffffff"
            font.family: root.theme.fontFamily
            font.pixelSize: 20
            font.bold: true
            style: Text.Outline
            styleColor: "#80000000"
        }
        Text {
            text: "上下滑动切换 · 单击播放或暂停"
            color: "#d9ffffff"
            font.family: root.theme.fontFamily
            font.pixelSize: 13
            style: Text.Outline
            styleColor: "#80000000"
        }
    }

    Column {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 24
        spacing: 14
        z: 4

        Repeater {
            model: [
                { icon: root.controller.volume <= 0.001 ? "×" : "♪", label: "音量", action: "volume" },
                { icon: "↻", label: "重载", action: "reload" },
                { icon: "↓", label: "下一条", action: "next" }
            ]

            delegate: Column {
                required property var modelData
                spacing: 4
                width: 58

                Rectangle {
                    width: 52
                    height: 52
                    radius: 26
                    color: actionHover.hovered ? "#e02f7cf6" : "#b5121720"
                    border.color: "#66ffffff"
                    border.width: 1

                    HoverHandler { id: actionHover }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.action === "volume") {
                                if (root.controller.volume > 0.001) {
                                    root.rememberedVolume = root.controller.volume
                                    root.controller.setVolume(0)
                                } else {
                                    root.controller.setVolume(Math.max(0.1, root.rememberedVolume))
                                }
                            } else if (modelData.action === "reload") {
                                root.retryRequested()
                            } else {
                                root.nextRequested()
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: modelData.icon
                        color: "#ffffff"
                        font.pixelSize: 22
                        font.bold: true
                    }
                }
                Text {
                    width: 52
                    text: modelData.label
                    color: "#e6ffffff"
                    horizontalAlignment: Text.AlignHCenter
                    font.family: root.theme.fontFamily
                    font.pixelSize: 11
                }
            }
        }

        Column {
            width: 52
            spacing: 5

            Text {
                width: parent.width
                text: "缓存池"
                color: "#e6ffffff"
                horizontalAlignment: Text.AlignHCenter
                font.family: root.theme.fontFamily
                font.pixelSize: 11
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 3
                Repeater {
                    model: 5
                    Rectangle {
                        required property int index
                        width: 6
                        height: 20
                        radius: 3
                        color: index < root.prefetchCount
                            ? root.theme.accentColor : "#55ffffff"
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(360, parent.width - 48)
        height: errorColumn.implicitHeight + 32
        radius: 16
        color: "#d91b202a"
        border.color: "#55ffffff"
        visible: root.errorMessage.length > 0
        z: 6

        Column {
            id: errorColumn
            anchors.centerIn: parent
            width: parent.width - 32
            spacing: 12

            Text {
                width: parent.width
                text: root.errorMessage
                color: "#ffffff"
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.family: root.theme.fontFamily
                font.pixelSize: 14
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 104
                height: 36
                radius: 18
                color: retryHover.hovered ? root.theme.accentMutedColor : root.theme.accentColor
                HoverHandler { id: retryHover }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.retryRequested()
                }
                Text {
                    anchors.centerIn: parent
                    text: "重试当前源"
                    color: "#ffffff"
                    font.family: root.theme.fontFamily
                    font.pixelSize: 13
                    font.bold: true
                }
            }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: root.controller.loading
        visible: running && root.errorMessage.length === 0
        z: 5
    }

    Rectangle {
        anchors.centerIn: parent
        width: 68
        height: 68
        radius: 34
        color: "#99000000"
        visible: root.controller.isPaused && root.errorMessage.length === 0
        z: 5
        Text {
            anchors.centerIn: parent
            text: "■"
            color: "#ffffff"
            font.pixelSize: 28
        }
    }
}
