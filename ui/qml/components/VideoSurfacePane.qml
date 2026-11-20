import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiniPlayer

Rectangle {
    id: root

    property QtObject theme
    property bool emptyState: true
    property bool loading: false
    property string currentFileTitle: ""
    property string emptyTitle: "请选择视频播放"
    property string emptySubtitle: "请从视频菜单选择内容"
    property string loadingTitle: "视频加载中"
    property string loadingSubtitle: "正在连接资源与缓冲，请稍候"

    signal doubleClicked()
    signal pressAndHoldStarted()
    signal pressAndHoldEnded()
    signal wheelScrolled(int delta)
    signal clicked()
    signal pointerMoved(real y, real height)
    signal pointerExited()

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

    // Video frame renderer (shown when file is open)
    VideoFrame {
        id: videoFrame
        anchors.fill: parent
        anchors.margins: 1
        visible: !root.emptyState

        Component.onCompleted: {
            playerController.setVideoBridge(videoFrame)
        }
    }

    // 与移动端播放器一致：单击控制栏、双击播放/暂停、长按临时快进。
    MouseArea {
        id: gestureArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        focus: false
        pressAndHoldInterval: 450
        preventStealing: true
        onPositionChanged: function(mouse) {
            root.pointerMoved(mouse.y, height)
        }
        onExited: root.pointerExited()
        property bool holding: false
        onDoubleClicked: root.doubleClicked()
        onPressAndHold: {
            holding = true
            playerController.beginHoldSpeed()
            root.pressAndHoldStarted()
        }
        onWheel: function(event) {
            root.wheelScrolled(event.angleDelta.y)
        }
        onPressed: function(mouse) {
            mouse.accepted = true
            root.clicked()
        }
        onReleased: {
            if (holding) {
                holding = false
                playerController.endHoldSpeed()
                root.pressAndHoldEnded()
            }
        }
        onCanceled: {
            if (holding) {
                holding = false
                playerController.endHoldSpeed()
                root.pressAndHoldEnded()
            }
        }
    }

    // Drag-and-drop handled by Main.qml DropArea

    // Empty state / placeholder
    Rectangle {
        width: Math.min(parent.width - 60, parent.height * 1.55)
        height: width / 1.77
        anchors.centerIn: parent
        radius: theme ? theme.panelRadius : 14
        color: "#050505"
        border.color: theme ? theme.borderColor : "#353535"
        border.width: 1
        visible: root.emptyState

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

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                text: root.emptyState ? root.emptyTitle : root.currentFileTitle
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: root.theme ? root.theme.sectionTitleSize : 16
                font.bold: true
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                color: root.theme ? root.theme.textMutedColor : "#858585"
                text: root.emptyState
                    ? root.emptySubtitle
                    : "Ready for playback"
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: root.theme ? root.theme.bodySize : 13
            }
        }
    }

    // Loading overlay
    Rectangle {
        id: loadingOverlay
        anchors.fill: parent
        anchors.margins: 1
        radius: root.radius - 1
        color: "#0a0b10"
        visible: root.loading
        z: 100

        // Deep cinema gradient
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#0f121a" }
                GradientStop { position: 0.5; color: "#0a0b10" }
                GradientStop { position: 1.0; color: "#06070a" }
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: 16

            // Center Animated Loading Indicator
            Item {
                width: 64
                height: 64
                anchors.horizontalCenter: parent.horizontalCenter

                // Outer Track Ring
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                    border.width: 3
                }

                // Outer Rotating Glowing Arc
                Item {
                    id: outerSpinner
                    anchors.fill: parent

                    Rectangle {
                        width: 18
                        height: 3
                        radius: 1.5
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: root.theme ? root.theme.accentColor : "#38bdf8"
                    }

                    NumberAnimation on rotation {
                        from: 0
                        to: 360
                        duration: 1100
                        loops: Animation.Infinite
                        running: root.loading
                    }
                }

                // Inner Counter-Rotating Ring
                Item {
                    id: innerSpinner
                    width: 44
                    height: 44
                    anchors.centerIn: parent

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: "transparent"
                        border.color: Qt.rgba(1, 1, 1, 0.05)
                        border.width: 2
                    }

                    Rectangle {
                        width: 12
                        height: 2
                        radius: 1
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: root.theme ? (root.theme.accentHoverColor || root.theme.accentColor) : "#60a5fa"
                    }

                    NumberAnimation on rotation {
                        from: 360
                        to: 0
                        duration: 1600
                        loops: Animation.Infinite
                        running: root.loading
                    }
                }

                // Center Pulsing Core Dot
                Rectangle {
                    id: centerPulseDot
                    width: 10
                    height: 10
                    radius: 5
                    anchors.centerIn: parent
                    color: root.theme ? root.theme.accentColor : "#38bdf8"

                    SequentialAnimation on scale {
                        loops: Animation.Infinite
                        running: root.loading
                        NumberAnimation { from: 0.8; to: 1.25; duration: 800; easing.type: Easing.InOutQuad }
                        NumberAnimation { from: 1.25; to: 0.8; duration: 800; easing.type: Easing.InOutQuad }
                    }

                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        running: root.loading
                        NumberAnimation { from: 0.45; to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
                        NumberAnimation { from: 1.0; to: 0.45; duration: 800; easing.type: Easing.InOutQuad }
                    }
                }
            }

            // Primary Label with animated pulsing dots
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 2

                Text {
                    text: root.loadingTitle || "视频加载中"
                    color: root.theme ? root.theme.textPrimaryColor : "#f3f4f6"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: 15
                    font.bold: true
                }

                Text {
                    text: "..."
                    color: root.theme ? root.theme.accentColor : "#38bdf8"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: 15
                    font.bold: true

                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        running: root.loading
                        NumberAnimation { from: 0.25; to: 1.0; duration: 600 }
                        NumberAnimation { from: 1.0; to: 0.25; duration: 600 }
                    }
                }
            }

            // Secondary Subtitle
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: root.theme ? root.theme.textMutedColor : "#9ca3af"
                text: root.loadingSubtitle || "精彩即将呈现，请稍候"
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: 12
            }

            // Stream Shimmer Track Bar
            Rectangle {
                width: 140
                height: 3
                radius: 1.5
                color: "#181f2a"
                anchors.horizontalCenter: parent.horizontalCenter
                clip: true

                Rectangle {
                    width: 50
                    height: 3
                    radius: 1.5
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.5; color: root.theme ? root.theme.accentColor : "#38bdf8" }
                        GradientStop { position: 1.0; color: "transparent" }
                    }

                    NumberAnimation on x {
                        from: -50
                        to: 140
                        duration: 1500
                        loops: Animation.Infinite
                        running: root.loading
                        easing.type: Easing.InOutQuad
                    }
                }
            }
        }
    }
}
