import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    property var queueItems: [
        { title: "Open a local file", meta: "PlayerController will replace this placeholder queue later" }
    ]
    property var runtimeFlags: [
        { label: "Controller wired in Task 4", tone: "success" },
        { label: "Media pipeline disabled in Task 4", tone: "warning" },
        { label: "FFmpeg remains optional until media tasks", tone: "warning" }
    ]
    property var statusTags: [
        { label: "Repeat off" },
        { label: "Volume live" },
        { label: "Renderer: QML shell" }
    ]

    width: 1440
    height: 900
    visible: true
    title: "miniPlayer"
    minimumWidth: 1220
    minimumHeight: 780
    color: appTheme.windowColor

    Theme {
        id: appTheme
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#191919" }
            GradientStop { position: 0.32; color: "#121212" }
            GradientStop { position: 1.0; color: appTheme.windowColor }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: appTheme.edgePadding
        spacing: appTheme.gap

        TopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: appTheme.topBarHeight
            theme: appTheme
            currentFileTitle: playerController.currentFile
            currentFileSubtitle: playerController.currentFile.length > 0 ? "Ready" : "Ctrl+O"
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: appTheme.gap

            CurrentMediaPanel {
                Layout.preferredWidth: appTheme.sidebarWidth
                Layout.fillHeight: true
                theme: appTheme
                currentFileTitle: playerController.currentFile
                currentFileSubtitle: playerController.currentFile.length > 0 ? "Controller-backed shell state" : ""
                queueModel: window.queueItems
                itemCount: playerController.currentFile.length > 0 ? 1 : 0
                queuedCount: Math.max(0, window.queueItems.length - 1)
                emptyState: playerController.currentFile.length === 0
            }

            VideoSurfacePane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: appTheme
                currentFileTitle: playerController.currentFile
                emptyState: playerController.currentFile.length === 0
                emptyTitle: "Open a local video file to begin"
                emptySubtitle: "This pane is now wired to PlayerController shell state and ready for future rendering."
            }

            MediaInfoPanel {
                Layout.preferredWidth: appTheme.infoPanelWidth
                Layout.fillHeight: true
                theme: appTheme
                selectedTitle: playerController.currentFile.length > 0 ? playerController.currentFile : "No media selected"
                selectedSubtitle: playerController.currentFile.length > 0
                    ? "Metadata model is ready for future extractor results."
                    : "Open a local file to populate MediaInfoModel-backed details."
                infoModel: playerController.mediaInfoModel
                flagModel: window.runtimeFlags
            }
        }

        PlaybackControlBar {
            Layout.fillWidth: true
            Layout.preferredHeight: appTheme.controlBarHeight
            theme: appTheme
            positionText: Qt.formatTime(new Date(Math.max(0, playerController.positionMs)), "mm:ss")
            durationText: Qt.formatTime(new Date(Math.max(0, playerController.durationMs)), "mm:ss")
            progressValue: playerController.positionMs
            progressTo: Math.max(playerController.durationMs, 1)
            volumeValue: playerController.volume * 100
            statusTags: window.statusTags
            emptyState: playerController.currentFile.length === 0
            onOpenRequested: playerController.openFile()
            onPlayRequested: playerController.play()
            onPauseRequested: playerController.pause()
            onStopRequested: playerController.stop()
            onSeekRequested: playerController.seek(value)
            onVolumeRequested: playerController.setVolume(value / 100.0)
        }

        RuntimeLogPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: appTheme.logPanelHeight
            theme: appTheme
            badgeText: "controller"
            statusText: playerController.isPlaying ? "Playing" : (playerController.isPaused ? "Paused" : "Idle")
            logModel: playerController.runtimeLogModel
        }
    }
}
