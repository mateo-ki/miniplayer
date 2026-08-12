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
    property string emptyTitle: "Open a local video file to begin"
    property string emptySubtitle: "Drag and drop files or use the Open button"

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
        onPositionChanged: function(mouse) {
            root.pointerMoved(mouse.y, height)
        }
        onExited: root.pointerExited()
        property bool holding: false
        onDoubleClicked: root.doubleClicked()
        onPressAndHold: {
            holding = true
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
                root.pressAndHoldEnded()
            }
        }
        onCanceled: {
            if (holding) {
                holding = false
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

            Rectangle {
                width: 88
                height: 88
                radius: 44
                anchors.horizontalCenter: parent.horizontalCenter
                color: openMouseArea.containsMouse
                    ? (root.theme ? root.theme.accentColor : "#f28c28")
                    : (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                border.color: root.theme ? root.theme.accentColor : "#f28c28"
                border.width: 1

                MouseArea {
                    id: openMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: playerController.openFile()
                }

                Text {
                    anchors.centerIn: parent
                    color: openMouseArea.containsMouse
                        ? "#101010"
                        : (root.theme ? root.theme.accentColor : "#f28c28")
                    text: "Open"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: 18
                    font.bold: true
                }
            }

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
        anchors.fill: parent
        color: "#cc0b0f15"
        visible: root.loading
        z: 100

        Column {
            anchors.centerIn: parent
            spacing: 12

            // Spinning indicator
            Rectangle {
                width: 44
                height: 44
                radius: 22
                anchors.horizontalCenter: parent.horizontalCenter
                color: "transparent"
                border.color: root.theme ? root.theme.accentColor : "#f28c28"
                border.width: 3

                Rectangle {
                    width: 20
                    height: 3
                    anchors.verticalCenter: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: root.theme ? root.theme.accentColor : "#f28c28"
                    transformOrigin: Item.Bottom

                    NumberAnimation on rotation {
                        from: 0
                        to: 360
                        duration: 800
                        loops: Animation.Infinite
                    }
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                text: "正在缓冲视频..."
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: root.theme ? root.theme.bodySize : 13
                font.bold: true
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: root.theme ? root.theme.textMutedColor : "#858585"
                text: "缓存足够后自动播放"
                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                font.pixelSize: root.theme ? root.theme.captionSize : 11
            }
        }
    }
}
