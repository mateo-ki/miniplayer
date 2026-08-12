import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: musicPage
    property var controller
    property var theme
    property var strings
    property string selectedServer: "netease"

    readonly property var servers: [
        { label: strings.neteaseLabel, value: "netease" },
        { label: strings.qqMusicLabel, value: "tencent" }
    ]

    Component.onCompleted: selectedServer = controller.uiSetting("musicServer", selectedServer)
    onSelectedServerChanged: controller.saveUiSetting("musicServer", selectedServer)

    function playOrPause() {
        if (controller.isPlaying && !controller.isPaused)
            controller.pause()
        else
            controller.play()
    }

    function formatTime(ms) {
        var seconds = Math.max(0, Math.floor(ms / 1000))
        var minutes = Math.floor(seconds / 60)
        var remainder = seconds % 60
        return minutes + ":" + (remainder < 10 ? "0" : "") + remainder
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: theme.edgePadding
        color: "transparent"

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                spacing: 12

                ColumnLayout {
                    Layout.preferredWidth: 164
                    spacing: 0

                    Text {
                        text: strings.musicTitle
                        color: theme.textPrimaryColor
                        font.family: theme.fontFamily
                        font.pixelSize: theme.titleSize
                        font.bold: true
                    }

                    Text {
                        text: "在线曲库与歌词"
                        color: theme.textMutedColor
                        font.family: theme.fontFamily
                        font.pixelSize: theme.captionSize
                    }
                }

                ComboBox {
                    id: serverSelector
                    Layout.preferredWidth: 118
                    Layout.preferredHeight: 34
                    model: musicPage.servers
                    textRole: "label"
                    currentIndex: musicPage.selectedServer === "tencent" ? 1 : 0
                    onActivated: function(index) { musicPage.selectedServer = model[index].value }
                    ToolTip.visible: hovered
                    ToolTip.text: "选择音乐来源"
                }

                Item { Layout.fillWidth: true }

                Text {
                    visible: controller.currentMusicTitle.length > 0
                    text: "正在播放  " + controller.currentMusicTitle
                    color: theme.accentColor
                    font.family: theme.fontFamily
                    font.pixelSize: theme.bodySize
                    elide: Text.ElideRight
                    Layout.maximumWidth: 280
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                spacing: 8

                TextField {
                    id: searchInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    placeholderText: strings.musicProSearchPlaceholder
                    color: theme.textPrimaryColor
                    font.family: theme.fontFamily
                    font.pixelSize: theme.bodySize
                    leftPadding: 12
                    rightPadding: 12
                    background: Rectangle {
                        color: theme.surfaceColor
                        radius: theme.controlRadius
                        border.color: searchInput.activeFocus ? theme.accentColor : theme.borderColor
                        border.width: 1
                    }
                    onAccepted: controller.searchMusic(text, musicPage.selectedServer)
                }

                Button {
                    id: searchButton
                    Layout.preferredWidth: 76
                    Layout.preferredHeight: 40
                    text: "搜索"
                    enabled: !controller.musicLoading
                    onClicked: controller.searchMusic(searchInput.text, musicPage.selectedServer)
                    ToolTip.visible: hovered
                    ToolTip.text: "搜索歌曲"
                }

                TextField {
                    id: playlistInput
                    Layout.preferredWidth: 150
                    Layout.preferredHeight: 40
                    placeholderText: strings.musicPlaylistCompactPlaceholder
                    color: theme.textPrimaryColor
                    font.family: theme.fontFamily
                    leftPadding: 10
                    rightPadding: 10
                    background: Rectangle {
                        color: theme.surfaceColor
                        radius: theme.controlRadius
                        border.color: playlistInput.activeFocus ? theme.accentColor : theme.borderColor
                        border.width: 1
                    }
                    onAccepted: controller.loadMusicPlaylist(text, musicPage.selectedServer)
                }

                Button {
                    Layout.preferredWidth: 82
                    Layout.preferredHeight: 40
                    text: strings.musicLoadPlaylist
                    enabled: !controller.musicLoading
                    onClicked: controller.loadMusicPlaylist(playlistInput.text, musicPage.selectedServer)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.panelColor
                border.color: theme.subtleBorderColor
                border.width: 1
                radius: theme.panelRadius

                GridLayout {
                    id: contentGrid
                    anchors.fill: parent
                    anchors.margins: 0
                    columns: width < 760 ? 1 : 2
                    columnSpacing: 0
                    rowSpacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: Math.max(290, Math.min(360, contentGrid.width * 0.36))
                        Layout.minimumWidth: 280
                        Layout.minimumHeight: contentGrid.columns === 1 ? 240 : 0
                        color: theme.panelColor
                        border.color: "transparent"
                        border.width: 0
                        radius: 0

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 0
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                Layout.leftMargin: 16
                                Layout.rightMargin: 14
                                Text {
                                    text: strings.musicPlaylistTitle
                                    color: theme.textPrimaryColor
                                    font.family: theme.fontFamily
                                    font.pixelSize: theme.sectionTitleSize
                                    font.bold: true
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: controller.musicResults.length + strings.musicTrackUnit
                                    color: theme.textMutedColor
                                    font.family: theme.fontFamily
                                    font.pixelSize: theme.captionSize
                                }
                            }

                            ListView {
                                id: resultList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 0
                                model: controller.musicResults
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { policy: resultList.contentHeight > resultList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

                                delegate: Rectangle {
                                    width: resultList.width
                                    height: 62
                                    color: resultHover.hovered ? theme.panelRaisedColor : "transparent"
                                    border.color: "transparent"
                                    border.width: 0
                                    radius: 0

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: 3
                                        color: resultHover.hovered ? theme.accentColor : "transparent"
                                    }

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 1
                                        color: theme.subtleBorderColor
                                    }

                                    HoverHandler { id: resultHover }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: controller.resolveAndPlayMusic(modelData.source, modelData.id, modelData.title, modelData.artist, modelData.pic)
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 14
                                        anchors.rightMargin: 10
                                        spacing: 10

                                        Text {
                                            Layout.preferredWidth: 24
                                            text: index + 1
                                            color: resultHover.hovered ? theme.accentColor : theme.textMutedColor
                                            font.family: theme.fontFamily
                                            font.pixelSize: theme.captionSize
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        Rectangle {
                                            Layout.preferredWidth: 44
                                            Layout.preferredHeight: 44
                                            color: theme.panelRaisedColor
                                            radius: 4
                                            clip: true
                                            Image {
                                                anchors.fill: parent
                                                source: modelData.pic || ""
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                                cache: true
                                            }
                                            Text {
                                                anchors.centerIn: parent
                                                visible: parent.children.length < 2 || !modelData.pic
                                                text: strings.musicIcon
                                                color: theme.accentColor
                                                font.pixelSize: 18
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData.title || strings.musicNoTrack
                                                color: theme.textPrimaryColor
                                                font.family: theme.fontFamily
                                                font.pixelSize: theme.bodySize
                                                font.bold: true
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData.artist || strings.musicUnknownArtist
                                                color: theme.textMutedColor
                                                font.family: theme.fontFamily
                                                font.pixelSize: theme.captionSize
                                                elide: Text.ElideRight
                                            }
                                        }
                                        ToolButton {
                                            text: "▶"
                                            onClicked: controller.resolveAndPlayMusic(modelData.source, modelData.id, modelData.title, modelData.artist, modelData.pic)
                                            ToolTip.visible: hovered
                                            ToolTip.text: "播放"
                                        }
                                    }
                                }

                                Text {
                                    anchors.centerIn: parent
                                    visible: resultList.count === 0 && !controller.musicLoading
                                    text: strings.musicEmptyResults
                                    color: theme.textMutedColor
                                    font.family: theme.fontFamily
                                    font.pixelSize: theme.bodySize
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    width: Math.min(parent.width - 32, 260)
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: 300
                        Layout.minimumHeight: contentGrid.columns === 1 ? 360 : 0
                        color: theme.surfaceColor
                        border.color: theme.subtleBorderColor
                        border.width: contentGrid.columns === 2 ? 0 : 1
                        radius: 0

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 92
                                spacing: 14

                                Rectangle {
                                    Layout.preferredWidth: 84
                                    Layout.preferredHeight: 84
                                    color: theme.panelRaisedColor
                                    radius: 6
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: controller.currentMusicPic || ""
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        visible: controller.currentMusicPic.length === 0
                                        text: strings.musicIcon
                                        color: theme.accentColor
                                        font.pixelSize: 28
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text {
                                        Layout.fillWidth: true
                                        text: controller.currentMusicTitle.length > 0 ? controller.currentMusicTitle : strings.musicNoTrack
                                        color: theme.textPrimaryColor
                                        font.family: theme.fontFamily
                                        font.pixelSize: 20
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: controller.currentMusicArtist.length > 0 ? controller.currentMusicArtist : strings.musicSelectTrackHint
                                        color: theme.textSecondaryColor
                                        font.family: theme.fontFamily
                                        font.pixelSize: theme.bodySize
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: controller.currentMusicLyricSource
                                        color: theme.textMutedColor
                                        font.family: theme.fontFamily
                                        font.pixelSize: theme.captionSize
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    text: musicPage.formatTime(controller.positionMs)
                                    color: theme.textMutedColor
                                    font.family: theme.fontFamily
                                    font.pixelSize: theme.captionSize
                                }

                                Slider {
                                    id: progressSlider
                                    Layout.fillWidth: true
                                    from: 0
                                    to: Math.max(1, controller.durationMs)
                                    value: Math.min(controller.positionMs, to)
                                    enabled: controller.isSeekable && controller.durationMs > 0
                                    onMoved: controller.seek(value)
                                }

                                Text {
                                    text: musicPage.formatTime(controller.durationMs)
                                    color: theme.textMutedColor
                                    font.family: theme.fontFamily
                                    font.pixelSize: theme.captionSize
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                spacing: 8
                                ToolButton { text: "■"; onClicked: controller.stop(); ToolTip.visible: hovered; ToolTip.text: "停止" }
                                ToolButton { text: "↓"; enabled: controller.currentMusicUrl.length > 0; onClicked: controller.saveCurrentMusic(); ToolTip.visible: hovered; ToolTip.text: strings.downloadLabel }
                                ToolButton { text: "▣"; onClicked: controller.openMusicDownloadFolder(); ToolTip.visible: hovered; ToolTip.text: strings.musicFolderLabel }
                                Item { Layout.fillWidth: true }
                                ToolButton { text: "|◀"; onClicked: controller.playPrevious(); ToolTip.visible: hovered; ToolTip.text: "上一首" }
                                Rectangle {
                                    Layout.preferredWidth: 46
                                    Layout.preferredHeight: 46
                                    radius: 23
                                    color: playHover.hovered ? theme.warningColor : theme.accentColor
                                    HoverHandler { id: playHover }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: musicPage.playOrPause() }
                                    Text {
                                        anchors.centerIn: parent
                                        text: controller.isPlaying && !controller.isPaused ? "Ⅱ" : "▶"
                                        color: theme.windowColor
                                        font.family: theme.fontFamily
                                        font.pixelSize: 19
                                        font.bold: true
                                    }
                                    ToolTip.visible: playHover.hovered
                                    ToolTip.text: controller.isPlaying && !controller.isPaused ? "暂停" : "播放"
                                }
                                ToolButton { text: "▶|"; onClicked: controller.playNext(); ToolTip.visible: hovered; ToolTip.text: "下一首" }
                                Item { Layout.fillWidth: true }
                                Text { text: "音量"; color: theme.textMutedColor; font.family: theme.fontFamily; font.pixelSize: theme.captionSize }
                                Slider { Layout.preferredWidth: 110; from: 0; to: 1; value: controller.volume; onMoved: controller.setVolume(value) }
                                Text { Layout.preferredWidth: 34; text: Math.round(controller.volume * 100) + "%"; color: theme.textSecondaryColor; font.family: theme.fontFamily; font.pixelSize: theme.captionSize }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: "transparent"
                                border.color: "transparent"
                                border.width: 0
                                radius: 0

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 8

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: strings.musicLyricsLabel; color: theme.textPrimaryColor; font.family: theme.fontFamily; font.pixelSize: theme.sectionTitleSize; font.bold: true }
                                        Text { text: controller.musicLyricOffsetMs + " ms"; color: theme.textMutedColor; font.family: theme.fontFamily; font.pixelSize: theme.captionSize }
                                        Item { Layout.fillWidth: true }
                                        ToolButton { text: "−0.5s"; onClicked: controller.adjustMusicLyricOffset(-500) }
                                        ToolButton { text: "归零"; enabled: controller.musicLyricOffsetMs !== 0; onClicked: controller.resetMusicLyricOffset() }
                                        ToolButton { text: "+0.5s"; onClicked: controller.adjustMusicLyricOffset(500) }
                                        ToolButton { text: "↻"; onClicked: controller.reloadCurrentMusicLyrics(); ToolTip.visible: hovered; ToolTip.text: "重新匹配歌词" }
                                        Button {
                                            text: "翻译"
                                            checkable: true
                                            checked: controller.musicShowTranslation
                                            onClicked: controller.setMusicLyricDisplayOptions(checked, controller.musicShowRomanization)
                                        }
                                        Button {
                                            text: "罗马音"
                                            checkable: true
                                            checked: controller.musicShowRomanization
                                            onClicked: controller.setMusicLyricDisplayOptions(controller.musicShowTranslation, checked)
                                        }
                                    }

                                    ListView {
                                        id: lyricList
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        spacing: 10
                                        boundsBehavior: Flickable.StopAtBounds
                                        model: controller.currentMusicLyricLines.length > 0 ? controller.currentMusicLyricLines : [{ "timeMs": -1, "text": controller.currentMusicLrc.length > 0 ? controller.currentMusicLrc : strings.musicLyricsEmpty }]
                                        ScrollBar.vertical: ScrollBar { policy: lyricList.contentHeight > lyricList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

                                        delegate: Text {
                                            width: lyricList.width
                                            text: {
                                                var value = modelData.text || ""
                                                if (controller.musicShowTranslation && modelData.translation) value += "\n" + modelData.translation
                                                if (controller.musicShowRomanization && modelData.romanization) value += "\n" + modelData.romanization
                                                return value
                                            }
                                            color: index === controller.currentMusicLyricIndex ? theme.accentColor : theme.textSecondaryColor
                                            font.family: theme.fontFamily
                                            font.pixelSize: index === controller.currentMusicLyricIndex ? 17 : 13
                                            font.bold: index === controller.currentMusicLyricIndex
                                            horizontalAlignment: Text.AlignHCenter
                                            wrapMode: Text.WordWrap
                                            opacity: index === controller.currentMusicLyricIndex ? 1.0 : 0.48
                                        }

                                        Connections {
                                            target: controller
                                            function onMusicLyricIndexChanged() {
                                                if (controller.currentMusicLyricIndex >= 0 && controller.currentMusicLyricIndex < lyricList.count)
                                                    lyricList.positionViewAtIndex(controller.currentMusicLyricIndex, ListView.Center)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                visible: controller.musicLoading || controller.musicMessage.length > 0
                color: theme.panelColor
                border.color: theme.subtleBorderColor
                border.width: 1
                radius: theme.controlRadius
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    verticalAlignment: Text.AlignVCenter
                    text: controller.musicLoading ? strings.musicProcessing : controller.musicMessage
                    color: theme.textSecondaryColor
                    font.family: theme.fontFamily
                    font.pixelSize: theme.captionSize
                    elide: Text.ElideRight
                }
            }
        }
    }
}
