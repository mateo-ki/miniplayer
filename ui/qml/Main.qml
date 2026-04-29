import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

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
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: appTheme.gap

            CurrentMediaPanel {
                Layout.preferredWidth: appTheme.sidebarWidth
                Layout.fillHeight: true
                theme: appTheme
            }

            VideoSurfacePane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: appTheme
            }

            MediaInfoPanel {
                Layout.preferredWidth: appTheme.infoPanelWidth
                Layout.fillHeight: true
                theme: appTheme
            }
        }

        PlaybackControlBar {
            Layout.fillWidth: true
            Layout.preferredHeight: appTheme.controlBarHeight
            theme: appTheme
        }

        RuntimeLogPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: appTheme.logPanelHeight
            theme: appTheme
        }
    }
}
