import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    property var strings
    property var siteModel
    property var hostContentItem
    property bool playbackActive: false
    property int selectedSiteIndex: -1
    signal playRequested(int siteIndex)
    signal siteSelected(int siteIndex)
    signal togglePlayPauseRequested()
    signal focusHostRequested()
    signal revealControlsRequested()
    signal hideControlsRequested()
    signal beginHoldSpeedRequested()
    signal endHoldSpeedRequested()
    signal wheelScrolled(real delta)

    function syncMpvVideoGeometry() {
        shortVideoVideoPane.syncMpvVideoGeometry()
    }

    readonly property var sources: [
        { title: "小姐姐", subtitle: "zzxjj.php?type=video", sourceIndex: 0 },
        { title: "女大", subtitle: "nvda.php?type=video", sourceIndex: 1 },
        { title: "女高", subtitle: "nvgao.php?type=video", sourceIndex: 2 },
        { title: "怼脸", subtitle: "duilian.php?type=video", sourceIndex: 3 },
        { title: "黑丝", subtitle: "heisis.php?type=video", sourceIndex: 4 },
        { title: "白丝", subtitle: "baisis.php?type=video", sourceIndex: 5 },
        { title: "漫展", subtitle: "manzhan.php?type=video", sourceIndex: 6 },
        { title: "聚合小姐姐", subtitle: "juhexjj.php?type=video", sourceIndex: 7 },
        { title: "完美身材", subtitle: "wmsc.php?type=video", sourceIndex: 8 },
        { title: "cosplay", subtitle: "COS.php?type=video", sourceIndex: 9 },
        { title: "特色服装", subtitle: "hanfu.php", sourceIndex: 10 },
        { title: "吊带", subtitle: "diaodai.php?type=video", sourceIndex: 11 },
        { title: "慢摇", subtitle: "manyao.php?type=video", sourceIndex: 12 },
        { title: "足控", subtitle: "jpmt.php?type=video", sourceIndex: 13 },
        { title: "清纯", subtitle: "qingchun.php?type=video", sourceIndex: 14 },
        { title: "快手便装", subtitle: "ksbianzhuang.php?type=video", sourceIndex: 15 },
        { title: "倍速变装", subtitle: "ksbianzhuang.php?type=video", sourceIndex: 16 },
        { title: "萝莉", subtitle: "luoli.php?type=video", sourceIndex: 17 },
        { title: "热舞视频", subtitle: "rewu.php?type=video", sourceIndex: 18 },
        { title: "变装", subtitle: "bianzhuang.php??", sourceIndex: 19 },
        { title: "快手小姐姐", subtitle: "ksxjjsp.php", sourceIndex: 20 },
        { title: "小姐姐", subtitle: "zzxjj.php", sourceIndex: 21 }
    ]

    VideoSurfacePane {
        id: shortVideoVideoPane
        anchors.fill: parent
        visible: root.playbackActive
        theme: root.theme
        currentFileTitle: root.controller.currentShortVideoUrl
        emptyState: false
        loading: root.controller.seeking
            || (root.controller.loading && root.controller.positionMs <= 0)
            || (root.controller.isPlaying && root.controller.positionMs <= 0 && root.controller.durationMs <= 0)
        onDoubleClicked: root.togglePlayPauseRequested()
        onClicked: root.focusHostRequested()
        onPointerMoved: function(y, height) {
            if (y >= Math.max(0, height - 96))
                root.revealControlsRequested()
            else
                root.hideControlsRequested()
        }
        onPressAndHoldStarted: {
            root.beginHoldSpeedRequested()
        }
        onPressAndHoldEnded: {
            root.endHoldSpeedRequested()
        }
        onPointerExited: root.hideControlsRequested()
        onWheelScrolled: function(delta) {
            root.wheelScrolled(delta)
        }
        function syncMpvVideoGeometry() {
            if (!visible)
                return
            var point = shortVideoVideoPane.mapToItem(root.hostContentItem, 0, 0)
            root.controller.setMpvVideoGeometry(point.x, point.y,
                                                 shortVideoVideoPane.width,
                                                 shortVideoVideoPane.height)
        }
        Component.onCompleted: syncMpvVideoGeometry()
        onXChanged: syncMpvVideoGeometry()
        onYChanged: syncMpvVideoGeometry()
        onWidthChanged: syncMpvVideoGeometry()
        onHeightChanged: syncMpvVideoGeometry()
        onVisibleChanged: if (visible) syncMpvVideoGeometry()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.theme.edgePadding
        spacing: root.theme.gap
        visible: !root.playbackActive

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: root.theme.panelRadius
            color: root.theme.panelColor
            border.color: root.theme.subtleBorderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.theme.edgePadding
                spacing: 18

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: root.strings.shortVideoRandomTitle
                        color: root.theme.textPrimaryColor
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.sectionTitleSize
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.strings.shortVideoRandomDescription
                        color: root.theme.textMutedColor
                        font.family: root.theme.fontFamily
                        font.pixelSize: root.theme.captionSize
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignRight
                    spacing: 10

                    ComboBox {
                        id: shortVideoSourceMenu
                        Layout.preferredWidth: 180
                        Layout.preferredHeight: 34
                        model: root.siteModel
                        textRole: "title"
                        currentIndex: {
                            for (var i = 0; i < root.siteModel.count; ++i) {
                                if (root.siteModel.get(i).sourceIndex === root.selectedSiteIndex)
                                    return i
                            }
                            return 0
                        }
                        onActivated: function(index) {
                            root.siteSelected(root.siteModel.get(index).sourceIndex)
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 78
                        Layout.preferredHeight: 34
                        radius: root.theme.controlRadius
                        color: refreshShortVideoHover.hovered ? root.theme.accentMutedColor : root.theme.accentColor

                        HoverHandler { id: refreshShortVideoHover }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.ArrowCursor
                            onClicked: {
                                root.playRequested(root.selectedSiteIndex)
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: root.strings.refreshLabel
                            color: "#ffffff"
                            font.family: root.theme.fontFamily
                            font.pixelSize: root.theme.bodySize
                            font.bold: true
                        }
                    }
                }

                GridView {
                    id: shortVideoGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 300
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.siteModel
                    property int columnCount: Math.max(1, Math.floor(width / 230))
                    cellWidth: Math.max(210, width / columnCount)
                    cellHeight: 164

                    delegate: Rectangle {
                        width: shortVideoGrid.cellWidth - 12
                        height: shortVideoGrid.cellHeight - 12
                        radius: root.theme.panelRadius
                        color: shortVideoHover.hovered || root.selectedSiteIndex === model.sourceIndex
                            ? root.theme.panelRaisedColor
                            : root.theme.surfaceColor
                        border.color: root.selectedSiteIndex === model.sourceIndex
                            ? root.theme.accentColor
                            : (shortVideoHover.hovered ? root.theme.accentColor : root.theme.subtleBorderColor)
                        border.width: root.selectedSiteIndex === model.sourceIndex ? 2 : 1

                        HoverHandler { id: shortVideoHover }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.ArrowCursor
                            onClicked: root.playRequested(model.sourceIndex)
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Rectangle {
                                    Layout.preferredWidth: 42
                                    Layout.preferredHeight: 42
                                    radius: 14
                                    color: root.theme.accentMutedColor

                                    Text {
                                        anchors.centerIn: parent
                                        text: root.strings.playIcon
                                        color: root.theme.accentColor
                                        font.pixelSize: 20
                                        font.bold: true
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: model.title
                                    color: root.theme.textPrimaryColor
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: 16
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: model.subtitle
                                color: root.theme.textMutedColor
                                font.family: root.theme.fontFamily
                                font.pixelSize: root.theme.captionSize
                                elide: Text.ElideRight
                            }

                            Item { Layout.fillHeight: true }

                            Text {
                                Layout.fillWidth: true
                                text: root.selectedSiteIndex === model.sourceIndex ? root.strings.shortVideoCurrentPlay : root.strings.shortVideoRandomPlay
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
                    visible: root.controller.shortVideoMessage.length > 0
                    text: root.controller.shortVideoMessage
                        + (root.controller.currentShortVideoUrl.length > 0
                            ? " · " + root.controller.currentShortVideoUrl : "")
                    color: root.theme.accentColor
                    font.family: root.theme.fontFamily
                    font.pixelSize: root.theme.captionSize
                    elide: Text.ElideRight
                }

            }
        }
    }
}
