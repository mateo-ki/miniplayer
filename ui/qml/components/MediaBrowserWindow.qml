import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root

    required property var hostWindow
    required property var controller
    required property var theme
    required property var strings
    required property var sidebar
    required property var shortVideoSiteModel
    property string activeTab: "video"
    property string shortVideoPlaybackMode: "random"
    property int selectedShortVideoSiteIndex: -1

    signal rebuildShortVideoSitesRequested()
    signal shortVideoPlaybackModeRequested(string mode)
    signal playShortVideoRequested(int sourceIndex)
    signal manageSitesRequested()

    width: Math.min(hostWindow.width * 0.86, 1040)
    height: Math.min(hostWindow.height * 0.86, 740)
    x: hostWindow.x + (hostWindow.width - width) / 2
    y: hostWindow.y + (hostWindow.height - height) / 2
    visible: false
    flags: Qt.Window | Qt.FramelessWindowHint
    transientParent: hostWindow
    color: "transparent"

    onVisibleChanged: if (visible) raise()
    onActiveChanged: if (!active && visible && !hostWindow.active) hide()

    Rectangle {
        anchors.fill: parent
        radius: root.theme.panelRadius
        color: root.theme.panelColor
        border.color: root.theme.borderColor
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            ColumnLayout {
                Layout.minimumWidth: 82
                Layout.preferredWidth: 82
                Layout.maximumWidth: 82
                Layout.fillHeight: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: root.strings.typeLabel
                    color: root.theme.textMutedColor
                    font.family: root.theme.fontFamily
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                }

                Repeater {
                    model: [
                        { label: root.strings.videoLabel, tab: "video" },
                        { label: root.strings.shortVideoLabel, tab: "shortvideo" }
                    ]

                    delegate: Rectangle {
                        required property var modelData
                        Layout.alignment: Qt.AlignHCenter
                        Layout.minimumWidth: 74
                        Layout.preferredWidth: 74
                        Layout.maximumWidth: 74
                        Layout.minimumHeight: 34
                        Layout.preferredHeight: 34
                        Layout.maximumHeight: 34
                        radius: 9
                        color: root.activeTab === modelData.tab
                            ? root.theme.accentColor
                            : (mediaTabHover.hovered ? root.theme.panelRaisedColor : root.theme.surfaceColor)
                        border.color: root.activeTab === modelData.tab
                            ? root.theme.accentColor : root.theme.borderColor
                        border.width: 1

                        HoverHandler { id: mediaTabHover }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.activeTab = modelData.tab
                                if (modelData.tab === "shortvideo")
                                    root.rebuildShortVideoSitesRequested()
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: root.activeTab === modelData.tab ? "#ffffff" : root.theme.textSecondaryColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.captionSize
                            font.bold: true
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: root.activeTab === "video" ? root.strings.videoLabel : root.strings.shortVideoLabel
                            color: root.theme.textPrimaryColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.sectionTitleSize
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.activeTab === "video"
                                ? root.strings.videoSearchHint : root.strings.shortVideoSearchHint
                            color: root.theme.textMutedColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.captionSize
                            elide: Text.ElideRight
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        radius: 17
                        color: mediaBrowserCloseHover.hovered ? root.theme.dangerColor : root.theme.surfaceColor
                        border.color: root.theme.borderColor
                        border.width: 1
                        HoverHandler { id: mediaBrowserCloseHover }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.hide()
                        }
                        Text {
                            anchors.centerIn: parent
                            text: root.strings.closeIcon
                            color: root.theme.textPrimaryColor
                            font.family: root.theme.fontFamily
                            font.pixelSize: 18
                            font.bold: true
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.activeTab === "shortvideo"
                    spacing: 8

                    Text {
                        text: root.strings.playbackTypeLabel
                        color: root.theme.textSecondaryColor
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.captionSize
                        font.bold: true
                    }

                    Repeater {
                        model: [
                            { label: root.strings.playModeOnce, mode: "once" },
                            { label: root.strings.playModeSame, mode: "same" },
                            { label: root.strings.playModeRandom, mode: "random" }
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            Layout.preferredWidth: 96
                            Layout.preferredHeight: 32
                            radius: root.theme.controlRadius
                            color: root.shortVideoPlaybackMode === modelData.mode
                                ? root.theme.accentMutedColor
                                : (shortVideoModeHover.hovered ? root.theme.panelRaisedColor : "transparent")
                            border.color: root.shortVideoPlaybackMode === modelData.mode
                                ? root.theme.accentColor : root.theme.borderColor
                            border.width: 1
                            HoverHandler { id: shortVideoModeHover }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.shortVideoPlaybackModeRequested(modelData.mode)
                            }
                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                color: root.shortVideoPlaybackMode === modelData.mode
                                    ? root.theme.accentColor : root.theme.textSecondaryColor
                                font.family: root.theme.fontFamily
                                font.pixelSize: root.theme.captionSize
                                font.bold: true
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                SearchView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.activeTab === "video"
                    theme: root.theme
                    sidebar: root.sidebar
                    onManageSitesRequested: {
                        root.hide()
                        root.manageSitesRequested()
                    }
                }

                GridView {
                    id: mediaShortVideoGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    visible: root.activeTab === "shortvideo"
                    model: root.shortVideoSiteModel
                    property int columnCount: Math.max(1, Math.floor(width / 220))
                    cellWidth: Math.max(200, width / columnCount)
                    cellHeight: 150

                    delegate: Rectangle {
                        required property int sourceIndex
                        required property string title
                        required property string subtitle
                        width: mediaShortVideoGrid.cellWidth - 12
                        height: mediaShortVideoGrid.cellHeight - 12
                        radius: root.theme.panelRadius
                        color: mediaShortVideoCardHover.hovered || root.selectedShortVideoSiteIndex === sourceIndex
                            ? root.theme.panelRaisedColor : root.theme.surfaceColor
                        border.color: root.selectedShortVideoSiteIndex === sourceIndex
                            ? root.theme.accentColor
                            : (mediaShortVideoCardHover.hovered ? root.theme.accentColor : root.theme.subtleBorderColor)
                        border.width: root.selectedShortVideoSiteIndex === sourceIndex ? 2 : 1

                        HoverHandler { id: mediaShortVideoCardHover }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.ArrowCursor
                            onClicked: root.playShortVideoRequested(sourceIndex)
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: title
                                color: root.theme.textPrimaryColor
                                font.family: root.theme.fontFamily
                                font.pixelSize: 16
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: subtitle
                                color: root.theme.textMutedColor
                                font.family: root.theme.fontFamily
                                font.pixelSize: root.theme.captionSize
                                elide: Text.ElideRight
                            }
                            Item { Layout.fillHeight: true }
                            Text {
                                Layout.fillWidth: true
                                text: root.strings.clickPlayLabel
                                color: root.theme.accentColor
                                font.family: root.theme.fontFamily
                                font.pixelSize: root.theme.captionSize
                                font.bold: true
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.activeTab === "shortvideo" && root.shortVideoSiteModel.count === 0
                    text: root.strings.shortVideoNoSites
                    color: root.theme.textMutedColor
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.bodySize
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
