import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root

    required property var hostWindow
    required property var hostContentItem
    required property var controller
    required property var theme
    required property var sidebar
    property bool browserVisible: true
    property bool immersiveMode: false
    property bool pageActive: false
    readonly property alias detailActive: embeddedVideoBrowser.detailActive

    signal togglePlayPauseRequested()
    signal focusHostRequested()
    signal revealControlsRequested()
    signal hideControlsRequested()
    signal beginHoldSpeedRequested()
    signal endHoldSpeedRequested()
    signal volumeAdjustmentRequested(real delta)
    signal manageSitesRequested()
    signal playbackRequested()
    signal returnToDetailRequested()

    function openListPage() {
        embeddedVideoBrowser.openListPage()
    }

    function showDetail() {
        embeddedVideoBrowser.detailActive = true
    }

    function syncMpvVideoGeometry() {
        videoPane.syncMpvVideoGeometry()
    }

    VideoSurfacePane {
        id: videoPane
        anchors.fill: parent
        anchors.leftMargin: root.immersiveMode ? 0 : root.theme.edgePadding
        anchors.rightMargin: root.immersiveMode ? 0 : root.theme.edgePadding
        anchors.topMargin: root.immersiveMode ? 0 : root.theme.edgePadding
        visible: !root.browserVisible
        theme: root.theme
        currentFileTitle: root.controller.currentFile
        emptyState: root.controller.currentFile.length === 0
        loading: root.controller.seeking
            || (root.controller.loading && root.controller.positionMs <= 0)
            || (root.controller.isPlaying && root.controller.positionMs <= 0
                && root.controller.durationMs <= 0)
        onDoubleClicked: root.togglePlayPauseRequested()
        onClicked: root.focusHostRequested()
        onPointerMoved: function(y, height) {
            if (y >= Math.max(0, height - 96))
                root.revealControlsRequested()
            else
                root.hideControlsRequested()
        }
        onPointerExited: root.hideControlsRequested()
        onPressAndHoldStarted: root.beginHoldSpeedRequested()
        onPressAndHoldEnded: root.endHoldSpeedRequested()
        onWheelScrolled: function(delta) {
            root.volumeAdjustmentRequested(delta > 0 ? 0.05 : -0.05)
        }

        function syncMpvVideoGeometry() {
            if (!visible)
                return
            var point = videoPane.mapToItem(root.hostContentItem, 0, 0)
            root.controller.setMpvVideoGeometry(point.x, point.y, videoPane.width, videoPane.height)
        }

        Component.onCompleted: syncMpvVideoGeometry()
        onXChanged: syncMpvVideoGeometry()
        onYChanged: syncMpvVideoGeometry()
        onWidthChanged: syncMpvVideoGeometry()
        onHeightChanged: syncMpvVideoGeometry()
        onVisibleChanged: if (visible) syncMpvVideoGeometry()
    }

    SearchView {
        id: embeddedVideoBrowser
        anchors.fill: parent
        anchors.margins: root.immersiveMode ? 0 : root.theme.edgePadding
        z: 70
        visible: root.browserVisible
        theme: root.theme
        sidebar: root.sidebar
        onManageSitesRequested: root.manageSitesRequested()
        onPlaybackRequested: root.playbackRequested()
    }

    Window {
        id: videoDetailBackWindow
        width: 92
        height: 36
        x: root.hostWindow.x
            + (root.sidebar.visible && !root.immersiveMode ? root.sidebar.width : 0)
            + (root.immersiveMode ? 12 : root.theme.edgePadding + 12)
        y: root.hostWindow.y + (root.immersiveMode ? 12 : root.theme.edgePadding + 12)
        visible: root.hostWindow.visible && root.pageActive && !root.browserVisible
            && embeddedVideoBrowser.detailActive
        flags: Qt.Tool | Qt.FramelessWindowHint
        transientParent: root.hostWindow
        color: "transparent"
        onVisibleChanged: if (visible) raise()

        Rectangle {
            anchors.fill: parent
            radius: 6
            color: backMouse.containsMouse ? root.theme.panelRaisedColor : root.theme.panelColor
            border.color: backMouse.containsMouse ? root.theme.accentColor : root.theme.borderColor
            border.width: 1
            opacity: 0.94

            MouseArea {
                id: backMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.returnToDetailRequested()
            }
            Text {
                anchors.centerIn: parent
                text: "返回详情"
                color: backMouse.containsMouse ? root.theme.accentColor : root.theme.textSecondaryColor
                font.pixelSize: 12
                font.family: root.theme.fontFamily
            }
        }
    }

    Connections {
        target: root.hostWindow
        function onWidthChanged() { root.syncMpvVideoGeometry() }
        function onHeightChanged() { root.syncMpvVideoGeometry() }
        function onVisibilityChanged() { root.syncMpvVideoGeometry() }
    }
}
