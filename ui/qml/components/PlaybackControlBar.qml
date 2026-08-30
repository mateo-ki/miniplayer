import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: root

    property QtObject theme
    property string positionText: "00:00"
    property string durationText: "00:00"
    property real progressFrom: 0
    property real progressTo: 100
    property real progressValue: 0
    property real bufferProgress: -1 // -1 = unknown, 0.0~1.0 = buffer fill
    property real volumeValue: 100
    property bool emptyState: true
    property bool isPlaying: false
    property int decodedFrames: 0
    property bool backendSeeking: false
    property bool fullScreenMode: false
    readonly property bool hovered: controlBarHover.hovered

    signal togglePlayPause()
    signal seekRequested(real value)
    signal volumeRequested(real value)
    signal playlistRequested()
    signal commentsRequested()
    signal downloadRequested()
    signal fullScreenToggleRequested()
    signal playbackRateChangeRequested(real rate)
    signal repeatModeChangeRequested(int mode)
    signal thumbnailRequested(real positionMs)
    signal danmakuRequested()
    signal danmakuSettingsRequested()
    signal danmakuOpacityRequested(real value)
    signal danmakuDisplayModeRequested(int mode)
    signal danmakuFontScaleRequested(real value)

    function closePopups() {
        volumeWindow.hide()
        volumeAutoHideTimer.stop()
        danmakuSettingsWindow.hide()
    }

    function openDanmakuSettings(point) {
        if (point) {
            var parentWindow = root.Window.window
            danmakuSettingsWindow.x = parentWindow.x + point.x + (root.width - danmakuSettingsWindow.width) / 2
            danmakuSettingsWindow.y = parentWindow.y + point.y - danmakuSettingsWindow.height - 10
        } else {
            danmakuSettingsWindow.showAt()
        }
        danmakuSettingsWindow.show()
        danmakuSettingsWindow.raise()
        danmakuSettingsWindow.requestActivate()
    }

    function keepVolumePopupOpen() {
        if (volumeWindow.visible)
            volumeAutoHideTimer.restart()
    }

    property real playbackRate: 1.0
    property int repeatMode: 0
    property bool drawerOpen: false
    property bool playlistOpen: false
    property bool commentsAvailable: false
    property bool commentsOpen: false
    property bool danmakuAvailable: false
    property bool danmakuEnabled: false
    // 绑定到弹窗可见性:设置弹窗打开时,通知 Main.qml 的控制条自动隐藏
    // 守卫放过它(否则鼠标移出控制条热区去够弹窗,控制条会连带弹窗一起被关闭)。
    property bool danmakuSettingsOpen: danmakuSettingsWindow.visible
    property real danmakuOpacityValue: 0.85
    property int danmakuDisplayModeValue: 0
    property real danmakuFontScaleValue: 1.0
    property bool seeking: false
    property real seekTarget: 0
    property var thumbnailImage: null
    property real thumbnailPos: -1
    property bool thumbnailReady: false
    property real hoverTimeMs: -1
    property bool timelineHovered: false

    color: "#e6171d27"
    radius: theme ? theme.controlRadius : 10
    border.color: root.theme ? root.theme.subtleBorderColor : Qt.rgba(1, 1, 1, 0.12)
    border.width: 1

    HoverHandler {
        id: controlBarHover
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 4

        RowLayout {
            spacing: 10

            Text {
                color: theme ? theme.textSecondaryColor : "#bebebe"
                text: root.positionText
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.bodySize : 13
            }

            Slider {
                id: timeline
                Layout.fillWidth: true
                from: root.progressFrom
                to: root.progressTo
                value: (timeline.pressed || root.seeking || root.backendSeeking) ? root.seekTarget : root.progressValue
                onMoved: {
                    root.seeking = true
                    root.seekTarget = timeline.value
                    seekDebounce.restart()
                }
                onPressedChanged: {
                    if (!pressed && root.seeking) {
                        seekDebounce.restart()
                    }
                }

                Timer {
                    id: seekDebounce
                    interval: 200
                    repeat: false
                    onTriggered: {
                        root.seekRequested(root.seekTarget)
                        seekReleaseTimer.restart()
                    }
                }

                Timer {
                    id: seekReleaseTimer
                    interval: 1200
                    repeat: false
                    onTriggered: {
                        if (!timeline.pressed && !root.backendSeeking) {
                            root.seeking = false
                        }
                    }
                }

                Connections {
                    target: root
                    function onBackendSeekingChanged() {
                        if (!root.backendSeeking && !timeline.pressed) {
                            root.seeking = false
                        }
                    }
                }

                Timer {
                    id: thumbnailTimer
                    interval: 150
                    repeat: false
                    onTriggered: {
                        root.thumbnailRequested(root.hoverTimeMs)
                    }
                }

                background: Rectangle {
                    x: timeline.leftPadding
                    y: timeline.topPadding + timeline.availableHeight / 2 - height / 2
                    width: timeline.availableWidth
                    height: 6
                    radius: 3
                    color: root.theme ? root.theme.panelRaisedColor : "#242424"

                    // Buffer progress (light gray)
                    Rectangle {
                        visible: root.bufferProgress > 0
                        width: Math.min(parent.width, root.bufferProgress * parent.width)
                        height: parent.height
                        radius: parent.radius
                        color: root.theme ? root.theme.borderColor : "#353535"
                    }

                    // Playback progress (accent color)
                    Rectangle {
                        width: timeline.visualPosition * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: root.theme ? root.theme.accentColor : "#f28c28"
                    }

                    // Hover area for thumbnail preview
                    MouseArea {
                        id: timelineHoverArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                        onPositionChanged: function(mouse) {
                            if (timeline.availableWidth <= 0) return
                            var ratio = Math.max(0, Math.min(1, mouse.x / timeline.availableWidth))
                            root.hoverTimeMs = ratio * root.progressTo
                            root.timelineHovered = true
                            thumbnailTimer.restart()
                        }
                        onEntered: root.timelineHovered = true
                        onExited: {
                            root.timelineHovered = false
                            root.hoverTimeMs = -1
                        }
                    }
                }

                handle: Rectangle {
                    x: timeline.leftPadding + timeline.visualPosition * (timeline.availableWidth - width)
                    y: timeline.topPadding + timeline.availableHeight / 2 - height / 2
                    width: 14
                    height: 14
                    radius: 7
                    color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                    border.color: root.theme ? root.theme.accentColor : "#f28c28"
                    border.width: 2
                }

                // Thumbnail preview popup
                Item {
                    id: thumbnailPopup
                    property real popupX: {
                        if (root.seeking) {
                            return timeline.leftPadding + timeline.visualPosition * timeline.availableWidth - width / 2
                        }
                        if (root.hoverTimeMs >= 0 && timeline.availableWidth > 0) {
                            var ratio = root.hoverTimeMs / Math.max(1, root.progressTo)
                            return timeline.leftPadding + ratio * timeline.availableWidth - width / 2
                        }
                        return 0
                    }
                    visible: (root.timelineHovered || root.seeking || root.backendSeeking) && root.thumbnailReady
                    x: Math.max(0, Math.min(popupX, timeline.availableWidth - width))
                    y: -height - 10
                    width: 340
                    height: 220

                    Rectangle {
                        anchors.fill: parent
                        radius: 8
                        color: "#1a1a1a"
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 2

                            Image {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                source: root.thumbnailImage ? "image://thumbnail/" + Number(root.thumbnailPos).toFixed(3) : ""
                                fillMode: Image.PreserveAspectFit
                                cache: false
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                            text: Qt.formatTime(new Date((root.seeking || root.backendSeeking) ? root.seekTarget : root.hoverTimeMs), "mm:ss")
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            Text {
                color: theme ? theme.textSecondaryColor : "#bebebe"
                text: root.durationText
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.bodySize : 13
        }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 16
                color: theme ? theme.subtleBorderColor : "#282828"
            }

            Text {
                color: theme ? theme.textMutedColor : "#858585"
                text: root.decodedFrames + " frames"
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.captionSize : 11
            }

        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 34

            RowLayout {
                anchors.centerIn: parent
                spacing: 8

                // Balance the four controls on the right so play/pause stays
                // at the exact geometric center of the control bar.
                Item {
                    Layout.preferredWidth: 108
                    Layout.preferredHeight: 1
                }

                Rectangle {
                    id: volumeButton
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 34
                    radius: theme ? theme.controlRadius : 10
                    color: volumeWindow.visible
                        ? (theme ? theme.accentMutedColor : "#7a4a17")
                        : (volumeMouseArea.containsMouse ? (theme ? theme.panelRaisedColor : "#242424") : (theme ? theme.panelColor : "#1c1c1c"))
                    border.color: theme ? theme.subtleBorderColor : "#282828"
                    border.width: 1

                    property bool muted: false

                    MouseArea {
                        id: volumeMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (volumeWindow.visible) {
                                volumeWindow.hide()
                                volumeAutoHideTimer.stop()
                            } else {
                                var point = volumeButton.mapToItem(null, 0, 0)
                                var parentWindow = root.Window.window
                                volumeWindow.x = parentWindow.x + point.x + (volumeButton.width - volumeWindow.width) / 2
                                volumeWindow.y = parentWindow.y + point.y - volumeWindow.height - 10
                                volumeWindow.show()
                                volumeWindow.raise()
                                volumeWindow.requestActivate()
                                volumeAutoHideTimer.restart()
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        color: volumeButton.muted
                            ? (theme ? theme.dangerColor : "#e06c63")
                            : (theme ? theme.textSecondaryColor : "#bebebe")
                        text: volumeButton.muted ? "\uE74F" : "\uE767"
                        font.family: "Segoe Fluent Icons"
                        font.pixelSize: 17
                    }

                }

                ToolButton {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 34
                    onClicked: root.playlistRequested()

                    background: Rectangle {
                        radius: root.theme ? root.theme.controlRadius : 10
                        color: root.playlistOpen
                            ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                            : (root.theme ? root.theme.panelColor : "#1c1c1c")
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1
                    }

                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: root.playlistOpen
                            ? (root.theme ? root.theme.accentColor : "#f28c28")
                            : (root.theme ? root.theme.textSecondaryColor : "#bebebe")
                        text: "\uEA37"
                        font.family: "Segoe Fluent Icons"
                        font.pixelSize: 17
                    }
                }
                ToolButton {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 34
                    visible: root.commentsAvailable
                    onClicked: root.commentsRequested()

                    background: Rectangle {
                        radius: root.theme ? root.theme.controlRadius : 10
                        color: root.commentsOpen
                            ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                            : (root.theme ? root.theme.panelColor : "#1c1c1c")
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1
                    }

                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: root.commentsOpen
                            ? (root.theme ? root.theme.accentColor : "#f28c28")
                            : (root.theme ? root.theme.textSecondaryColor : "#bebebe")
                        text: ""
                        font.family: "Segoe Fluent Icons"
                        font.pixelSize: 17
                    }
                }

                ToolButton {
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 34
                    enabled: !root.emptyState
                    opacity: enabled ? 1.0 : 0.3
                    onClicked: root.togglePlayPause()

                    background: Rectangle {
                        radius: 19
                        color: root.theme ? root.theme.accentColor : "#f28c28"
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1
                    }

                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: "#101010"
                        text: root.isPlaying ? "\uE769" : "\uE768"
                        font.family: "Segoe Fluent Icons"
                        font.pixelSize: 17
                    }
                }



                // 弹幕开关(仅动漫播放时可见)。
                ToolButton {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 34
                    visible: root.danmakuAvailable
                    onClicked: {
                        if (danmakuSettingsWindow.visible)
                            danmakuSettingsWindow.hide()
                        else
                            root.danmakuRequested()
                    }
                    onPressAndHold: root.danmakuSettingsRequested()

                    background: Rectangle {
                        radius: root.theme ? root.theme.controlRadius : 10
                        color: root.danmakuEnabled
                            ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                            : (root.theme ? root.theme.panelColor : "#1c1c1c")
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1
                    }

                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: root.danmakuEnabled
                            ? (root.theme ? root.theme.accentColor : "#f28c28")
                            : (root.theme ? root.theme.textSecondaryColor : "#bebebe")
                        text: "弹"
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: 13
                        font.bold: true
                    }

                    ToolTip.visible: hovered
                    ToolTip.text: root.danmakuEnabled ? "弹幕开(长按设置)" : "弹幕关(长按设置)"
                }

                ToolButton {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 34
                    enabled: !root.emptyState
                    opacity: enabled ? 1.0 : 0.3
                    onClicked: {
                        var rates = [0.5, 1.0, 1.5, 2.0]
                        var current = Number(root.playbackRate.toFixed(1))
                        var idx = rates.indexOf(current)
                        root.playbackRateChangeRequested(rates[(idx + 1) % rates.length])
                    }

                    background: Rectangle {
                        radius: root.theme ? root.theme.controlRadius : 10
                        color: root.playbackRate !== 1.0
                            ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                            : (root.theme ? root.theme.panelColor : "#1c1c1c")
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1
                    }

                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: root.playbackRate !== 1.0
                            ? (root.theme ? root.theme.accentColor : "#f28c28")
                            : (root.theme ? root.theme.textSecondaryColor : "#bebebe")
                        text: Number(root.playbackRate.toFixed(1)).toString() + "x"
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: 12
                        font.bold: true
                    }
                }

                ToolButton {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 34
                    enabled: !root.emptyState
                    opacity: enabled ? 1.0 : 0.3
                    onClicked: root.fullScreenToggleRequested()

                    background: Rectangle {
                        radius: root.theme ? root.theme.controlRadius : 10
                        color: root.fullScreenMode
                            ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                            : (root.theme ? root.theme.panelColor : "#1c1c1c")
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1
                    }

                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: root.fullScreenMode
                            ? (root.theme ? root.theme.accentColor : "#f28c28")
                            : (root.theme ? root.theme.textSecondaryColor : "#bebebe")
                        text: root.fullScreenMode ? "\uE73F" : "\uE740"
                        font.family: "Segoe Fluent Icons"
                        font.pixelSize: 17
                    }
                }
            }

        }

    }

    Window {
        id: volumeWindow
        width: 56
        height: 172
        flags: Qt.Tool | Qt.FramelessWindowHint
        transientParent: root.Window.window
        color: "transparent"
        onVisibleChanged: {
            if (visible)
                volumeAutoHideTimer.restart()
            else
                volumeAutoHideTimer.stop()
        }

        Rectangle {
            anchors.fill: parent
            radius: root.theme ? root.theme.controlRadius : 10
            color: root.theme ? root.theme.panelColor : "#1c1c1c"
            border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                    text: Math.round(volumeSlider.value) + "%"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.captionSize : 11
                }

                Slider {
                    id: volumeSlider
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignHCenter
                    orientation: Qt.Vertical
                    from: 0
                    to: 100
                    Component.onCompleted: value = root.volumeValue
                    Connections {
                        target: root
                        function onVolumeValueChanged() {
                            volumeSlider.value = root.volumeValue
                        }
                    }
                    onMoved: {
                        root.volumeRequested(value)
                        root.keepVolumePopupOpen()
                    }
                    onPressedChanged: {
                        if (pressed)
                            volumeAutoHideTimer.stop()
                        else
                            root.keepVolumePopupOpen()
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    color: root.theme ? root.theme.textMutedColor : "#858585"
                    text: volumeButton.muted ? "\uE767" : "\uE74F"
                    font.family: "Segoe Fluent Icons"
                    font.pixelSize: 15

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            volumeButton.muted = !volumeButton.muted
                            root.keepVolumePopupOpen()
                        }
                    }
                }

                ToolButton {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 38
                    enabled: !root.emptyState
                    opacity: enabled ? 1.0 : 0.3
                    onClicked: root.downloadRequested()

                    background: Rectangle {
                        radius: root.theme ? root.theme.controlRadius : 10
                        color: root.theme ? root.theme.panelColor : "#1c1c1c"
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1
                    }

                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                        text: "\uE896"
                        font.family: "Segoe Fluent Icons"
                        font.pixelSize: 17
                    }
                }
            }
        }
    }

    Timer {
        id: volumeAutoHideTimer
        interval: 3000
        repeat: false
        onTriggered: {
            if (volumeSlider.pressed)
                restart()
            else
                volumeWindow.hide()
        }
    }

    // 弹幕设置弹窗:透明度/模式/字号。沿用 volumeWindow 的顶层 Tool 窗口 idiom。
    Window {
        id: danmakuSettingsWindow
        width: 220
        height: 230
        flags: Qt.Tool | Qt.FramelessWindowHint
        transientParent: root.Window.window
        color: "transparent"
        visible: false
        onVisibleChanged: if (visible) raise()

        function showAt() {
            var point = root.mapToItem(null, 0, 0)
            var parentWindow = root.Window.window
            x = parentWindow.x + point.x + (root.width - width) / 2
            y = parentWindow.y + point.y - height - 10
            show()
            raise()
            requestActivate()
        }

        Rectangle {
            anchors.fill: parent
            radius: root.theme ? root.theme.controlRadius : 10
            color: root.theme ? root.theme.panelColor : "#1c1c1c"
            border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: "弹幕设置"
                    color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.bodySize : 13
                    font.bold: true
                }

                Text {
                    Layout.fillWidth: true
                    text: "透明度 " + Math.round(danmakuOpacitySlider.value * 100) + "%"
                    color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.captionSize : 11
                }
                Slider {
                    id: danmakuOpacitySlider
                    Layout.fillWidth: true
                    from: 0.1
                    to: 1.0
                    value: root.danmakuOpacityValue
                    stepSize: 0.05
                    onMoved: root.danmakuOpacityRequested(value)
                }

                Text {
                    Layout.fillWidth: true
                    text: "显示模式"
                    color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.captionSize : 11
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [
                            { label: "全部", mode: 0 },
                            { label: "滚动", mode: 1 },
                            { label: "顶部", mode: 2 },
                            { label: "底部", mode: 3 }
                        ]
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            radius: root.theme ? root.theme.controlRadius : 8
                            color: root.danmakuDisplayModeValue === modelData.mode
                                ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                                : (root.theme ? root.theme.surfaceColor : "#0b0f15")
                            border.color: root.danmakuDisplayModeValue === modelData.mode
                                ? (root.theme ? root.theme.accentColor : "#f28c28")
                                : (root.theme ? root.theme.subtleBorderColor : "#282828")
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                color: root.danmakuDisplayModeValue === modelData.mode
                                    ? (root.theme ? root.theme.accentColor : "#f28c28")
                                    : (root.theme ? root.theme.textSecondaryColor : "#bebebe")
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                font.pixelSize: 11
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.danmakuDisplayModeRequested(modelData.mode)
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "字号 " + danmakuFontScaleSlider.value.toFixed(1) + "x"
                    color: root.theme ? root.theme.textSecondaryColor : "#bebebe"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.captionSize : 11
                }
                Slider {
                    id: danmakuFontScaleSlider
                    Layout.fillWidth: true
                    from: 0.6
                    to: 2.0
                    value: root.danmakuFontScaleValue
                    stepSize: 0.1
                    onMoved: root.danmakuFontScaleRequested(value)
                }
            }
        }
    }
}
