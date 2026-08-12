import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property QtObject theme
    property string currentFileTitle: ""
    property string currentFileSubtitle: ""
    property var infoModel: []
    property bool emptyState: true
    property int currentIndex: -1

    color: theme ? theme.panelColor : "#1c1c1c"
    radius: theme ? theme.panelRadius : 14
    border.color: theme ? theme.subtleBorderColor : "#282828"
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme ? theme.edgePadding : 20
        spacing: 14

        // Header
        Text {
            color: theme ? theme.textPrimaryColor : "#f3f3f3"
            text: "Media"
            font.family: theme ? theme.fontFamily : "Segoe UI"
            font.pixelSize: theme ? theme.sectionTitleSize : 16
            font.bold: true
        }

        // Now playing card
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            radius: theme ? theme.controlRadius : 10
            color: theme ? theme.panelRaisedColor : "#242424"
            border.color: theme ? theme.borderColor : "#353535"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 60
                    Layout.preferredHeight: 60
                    radius: 10
                    color: theme ? theme.accentMutedColor : "#7a4a17"

                    Text {
                        anchors.centerIn: parent
                        color: theme ? theme.accentColor : "#f28c28"
                        text: root.emptyState ? "Open" : "Now"
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        font.pixelSize: 18
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        color: theme ? theme.textPrimaryColor : "#f3f3f3"
                        text: root.currentFileTitle.length > 0
                            ? Qt.resolvedUrl(root.currentFileTitle).toString().split('/').pop()
                            : "No file loaded"
                        elide: Text.ElideRight
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        font.pixelSize: theme ? theme.bodySize : 13
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        color: theme ? theme.textMutedColor : "#858585"
                        text: root.currentFileSubtitle.length > 0
                            ? root.currentFileSubtitle
                            : "Open a file to start playback"
                        elide: Text.ElideRight
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        font.pixelSize: theme ? theme.captionSize : 11
                    }
                }
            }
        }

        // Stream info
        Repeater {
            model: root.infoModel

            delegate: Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                radius: root.theme ? root.theme.controlRadius : 10
                color: "#151515"
                border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 10

                    Text {
                        Layout.preferredWidth: 80
                        color: root.theme ? root.theme.textMutedColor : "#858585"
                        text: typeof key !== "undefined" ? key : modelData.key
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: root.theme ? root.theme.captionSize : 11
                    }

                    Text {
                        Layout.fillWidth: true
                        color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                        text: typeof value !== "undefined" ? value : modelData.value
                        elide: Text.ElideRight
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: root.theme ? root.theme.bodySize : 13
                    }
                }
            }
        }

        // Playlist header
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                color: theme ? theme.textSecondaryColor : "#bebebe"
                text: "Playlist"
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.bodySize : 13
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: 70
                Layout.preferredHeight: 26
                radius: root.theme ? root.theme.controlRadius : 10
                color: "#151515"
                border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    color: root.theme ? root.theme.textMutedColor : "#858585"
                    text: playerController.playlistModel.count + " items"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.captionSize : 11
                }
            }
        }

        // Playlist
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme ? theme.controlRadius : 10
            color: "#121212"
            border.color: theme ? theme.subtleBorderColor : "#282828"
            border.width: 1

            ListView {
                id: playlistView
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4
                clip: true
                model: playerController.playlistModel

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 44
                    radius: root.theme ? root.theme.controlRadius : 10
                    color: index === root.currentIndex ? "#1a2a1a" : (playlistHover.hovered ? "#1a1a1a" : "transparent")
                    border.color: index === root.currentIndex
                        ? (root.theme ? root.theme.successColor : "#6ec16e")
                        : "transparent"
                    border.width: index === root.currentIndex ? 1 : 0

                    HoverHandler {
                        id: playlistHover
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.currentIndex = index
                            playerController.playFromPlaylist(index)
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            radius: 6
                            color: index === root.currentIndex
                                ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                                : (root.theme ? root.theme.panelRaisedColor : "#242424")

                            Text {
                                anchors.centerIn: parent
                                color: index === root.currentIndex
                                    ? (root.theme ? root.theme.accentColor : "#f28c28")
                                    : (root.theme ? root.theme.textMutedColor : "#858585")
                                text: index + 1
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                font.pixelSize: 11
                                font.bold: index === root.currentIndex
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            color: index === root.currentIndex
                                ? (root.theme ? root.theme.textPrimaryColor : "#f3f3f3")
                                : (root.theme ? root.theme.textSecondaryColor : "#bebebe")
                            text: title
                            elide: Text.ElideRight
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: root.theme ? root.theme.bodySize : 13
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: playerController.playlistModel.count === 0
                    color: root.theme ? root.theme.textMutedColor : "#858585"
                    text: "No files in playlist"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.captionSize : 11
                }
            }
        }
    }
}
