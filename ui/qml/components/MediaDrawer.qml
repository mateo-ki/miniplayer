import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property QtObject theme
    property bool isOpen: false
    property string currentFileTitle: ""
    property string currentFileSubtitle: ""
    property var infoModel: []
    property bool emptyState: true
    property int currentIndex: -1
    property var playlistModel
    property string drawerMode: "info" // "info" or "playlist"
    property var audioTracks: []
    property int currentAudioTrack: -1
    property var subtitleTracks: []
    property int currentSubtitleTrack: -1
    property bool subtitlesEnabled: false
    property var apiSiteModel
    property var sourceSearchModel
    property string currentVodName: ""
    signal playlistItemRequested(int index, string filePath, string title)
    property int sourceSiteIndex: -1

    function episodeCountFromPlayUrl(playUrl) {
        if (!playUrl || playUrl.length === 0) return 0
        var maxCount = 0
        var sources = playUrl.split("$$$")
        for (var i = 0; i < sources.length; i++) {
            var count = 0
            var episodes = sources[i].split("#")
            for (var j = 0; j < episodes.length; j++) {
                if (episodes[j].trim().length > 0) count++
            }
            maxCount = Math.max(maxCount, count)
        }
        return maxCount
    }

    function episodeCountText(remarks, playUrl) {
        if (remarks && remarks.length > 0) return remarks
        var count = episodeCountFromPlayUrl(playUrl)
        return count > 0 ? ("共 " + count + " 集") : "暂无集数"
    }

    function episodesFromPlayUrl(playFrom, playUrl) {
        if (!playUrl || playUrl.length === 0) return []
        var froms = playFrom ? playFrom.split("$$$") : []
        var urls = playUrl.split("$$$")
        var bestIndex = 0
        for (var i = 0; i < urls.length; i++) {
            if (urls[i].indexOf(".m3u8") >= 0 || urls[i].indexOf(".mp4") >= 0) {
                bestIndex = i
                break
            }
        }

        var result = []
        var parts = urls[bestIndex].split("#")
        for (var j = 0; j < parts.length; j++) {
            var kv = parts[j].split("$")
            if (kv.length >= 2 && kv[1].length > 0) {
                result.push({ title: kv[0] || ("第 " + (j + 1) + " 集"), url: kv[1] })
            } else if (kv.length === 1 && kv[0].length > 0) {
                result.push({ title: "播放 " + (j + 1), url: kv[0] })
            }
        }
        return result
    }

    function searchSourceSite(index) {
        if (!root.apiSiteModel || !root.currentVodName || root.currentVodName.length === 0) return
        root.sourceSiteIndex = index
        playerController.searchCurrentVodOnSite(index)
        autoHideTimer.stop()
    }

    function activateSourceResult(vodName, vodPlayFrom, vodPlayUrl) {
        var episodes = episodesFromPlayUrl(vodPlayFrom, vodPlayUrl)
        if (episodes.length === 0) return
        playerController.setCurrentVodName(vodName)
        playerController.setPlaylistEpisodes(episodes, 0)
        root.currentIndex = 0
        autoHideTimer.stop()
    }

    function open() {
        isOpen = true
        autoHideTimer.restart()
        scrollToCurrentEpisode()
    }

    function scrollToCurrentEpisode() {
        if (drawerMode !== "playlist" || currentIndex < 0)
            return
        Qt.callLater(function() {
            playlistView.positionViewAtIndex(currentIndex, ListView.Center)
        })
    }

    onCurrentIndexChanged: scrollToCurrentEpisode()

    function close() {
        isOpen = false
        autoHideTimer.stop()
    }

    function toggle() {
        if (isOpen) close()
        else open()
    }

    width: 320
    x: parent.width - (isOpen ? width : 0)
    z: 50

    color: theme ? theme.panelColor : "#1c1c1c"
    border.color: theme ? theme.subtleBorderColor : "#282828"
    border.width: 1

    Behavior on x {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    Timer {
        id: autoHideTimer
        interval: 3000
        repeat: false
        onTriggered: root.close()
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onContainsMouseChanged: {
            if (containsMouse) {
                autoHideTimer.stop()
            } else if (root.isOpen) {
                autoHideTimer.restart()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme ? theme.edgePadding : 20
        spacing: 14

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                color: theme ? theme.textPrimaryColor : "#f3f3f3"
                text: root.drawerMode === "playlist" ? "Playlist" : "Media"
                font.family: theme ? theme.fontFamily : "Segoe UI"
                font.pixelSize: theme ? theme.sectionTitleSize : 16
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                radius: 12
                color: closeMouseArea.hovered ? (theme ? theme.panelRaisedColor : "#242424") : "transparent"

                HoverHandler { id: closeMouseArea }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }

                Text {
                    anchors.centerIn: parent
                    color: theme ? theme.textMutedColor : "#858585"
                    text: "×"
                    font.pixelSize: 16
                }
            }
        }

        // Now playing card
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            radius: theme ? theme.controlRadius : 10
            color: theme ? theme.panelRaisedColor : "#242424"
            border.color: theme ? theme.borderColor : "#353535"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    radius: 8
                    color: theme ? theme.accentMutedColor : "#7a4a17"

                    Text {
                        anchors.centerIn: parent
                        color: theme ? theme.accentColor : "#f28c28"
                        text: root.emptyState ? "?" : "Now"
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        font.pixelSize: 14
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        color: theme ? theme.textPrimaryColor : "#f3f3f3"
                        text: root.currentFileTitle.length > 0
                            ? root.currentFileTitle.split('/').pop().split('\\').pop()
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
                            : "Open a file to start"
                        elide: Text.ElideRight
                        font.family: theme ? theme.fontFamily : "Segoe UI"
                        font.pixelSize: theme ? theme.captionSize : 11
                    }
                }
            }
        }

        // Info mode: stream info (scrollable)
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: root.drawerMode === "info"
            visible: root.drawerMode === "info"
            contentHeight: infoColumn.height
            clip: true
            flickableDirection: Flickable.VerticalFlick
            interactive: contentHeight > height

            ColumnLayout {
                id: infoColumn
                width: parent.width
                spacing: 6

                Repeater {
                    model: root.infoModel

                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        radius: root.theme ? root.theme.controlRadius : 10
                        color: root.theme ? root.theme.surfaceColor : "#151515"
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 8

                            Text {
                                Layout.preferredWidth: 70
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

                // Audio track selector
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.audioTracks.length > 1
                    spacing: 6

                    Text {
                        color: root.theme ? root.theme.textMutedColor : "#858585"
                        text: "Audio Track"
                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                        font.pixelSize: root.theme ? root.theme.captionSize : 11
                        font.bold: true
                    }

                    Repeater {
                        model: root.audioTracks

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            radius: 6
                            color: modelData.index === root.currentAudioTrack
                                ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                                : (itemTrackHover.hovered ? (root.theme ? root.theme.panelRaisedColor : "#1a1a1a") : (root.theme ? root.theme.surfaceColor : "#151515"))
                            border.color: modelData.index === root.currentAudioTrack
                                ? (root.theme ? root.theme.accentColor : "#f28c28")
                                : "transparent"
                            border.width: 1

                            HoverHandler { id: itemTrackHover }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    playerController.switchAudioTrack(modelData.index)
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                                text: (modelData.title.length > 0 ? modelData.title : "Track " + (modelData.index + 1))
                                    + (modelData.language !== "unknown" ? " (" + modelData.language + ")" : "")
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                font.pixelSize: root.theme ? root.theme.bodySize : 13
                            }
                        }
                    }
                }

                // Subtitle track selector
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.subtitleTracks.length > 0
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            color: root.theme ? root.theme.textMutedColor : "#858585"
                            text: "Subtitles"
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: root.theme ? root.theme.captionSize : 11
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 18
                            radius: 9
                            color: root.subtitlesEnabled
                                ? (root.theme ? root.theme.accentColor : "#f28c28")
                                : (root.theme ? root.theme.panelRaisedColor : "#242424")
                            border.color: root.theme ? root.theme.borderColor : "#353535"
                            border.width: 1

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: playerController.setSubtitlesEnabled(!root.subtitlesEnabled)
                            }

                            Rectangle {
                                width: 14
                                height: 14
                                radius: 7
                                anchors.verticalCenter: parent.verticalCenter
                                x: root.subtitlesEnabled ? parent.width - width - 2 : 2
                                color: "#ffffff"

                                Behavior on x {
                                    NumberAnimation { duration: 150 }
                                }
                            }
                        }
                    }

                    Repeater {
                        model: root.subtitleTracks

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            radius: 6
                            color: modelData.index === root.currentSubtitleTrack
                                ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                                : (subtitleTrackHover.hovered ? (root.theme ? root.theme.panelRaisedColor : "#1a1a1a") : (root.theme ? root.theme.surfaceColor : "#151515"))
                            border.color: modelData.index === root.currentSubtitleTrack
                                ? (root.theme ? root.theme.accentColor : "#f28c28")
                                : "transparent"
                            border.width: 1
                            opacity: root.subtitlesEnabled ? 1.0 : 0.4

                            HoverHandler { id: subtitleTrackHover }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    playerController.switchSubtitleTrack(modelData.index)
                                    if (!root.subtitlesEnabled) {
                                        playerController.setSubtitlesEnabled(true)
                                    }
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                                text: (modelData.title.length > 0 ? modelData.title : "Track " + (modelData.index + 1))
                                    + (modelData.language !== "unknown" ? " (" + modelData.language + ")" : "")
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                font.pixelSize: root.theme ? root.theme.bodySize : 13
                            }
                        }
                    }
                }
            }
        }

        // Playlist mode: source switcher + playlist (scrollable)
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: root.drawerMode === "playlist"
            visible: root.drawerMode === "playlist"
            radius: theme ? theme.controlRadius : 10
            color: theme ? theme.surfaceColor : "#121212"
            border.color: theme ? theme.subtleBorderColor : "#282828"
            border.width: 1

            Rectangle {
                id: sourceSwitcher
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 4
                height: 214
                radius: root.theme ? root.theme.controlRadius : 10
                color: root.theme ? root.theme.panelColor : "#151515"
                border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                            text: root.currentVodName.length > 0 ? ("换源: " + root.currentVodName) : "先播放或打开一个视频详情"
                            elide: Text.ElideRight
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: root.theme ? root.theme.bodySize : 13
                            font.bold: true
                        }

                        Rectangle {
                            Layout.preferredWidth: 56
                            Layout.preferredHeight: 24
                            radius: 6
                            color: sourceSearchMouse.hovered ? (root.theme ? root.theme.accentMutedColor : "#7a4a17") : (root.theme ? root.theme.panelRaisedColor : "#242424")
                            border.color: root.theme ? root.theme.borderColor : "#353535"
                            border.width: 1
                            enabled: root.currentVodName.length > 0 && root.apiSiteModel
                            opacity: enabled ? 1.0 : 0.45

                            HoverHandler { id: sourceSearchMouse }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.searchSourceSite(root.sourceSiteIndex >= 0 ? root.sourceSiteIndex : playerController.apiSiteModel.currentIndex)
                            }

                            Text {
                                anchors.centerIn: parent
                                color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                                text: "搜索"
                                font.pixelSize: 11
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            }
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        orientation: ListView.Horizontal
                        spacing: 6
                        clip: true
                        model: root.apiSiteModel

                        delegate: Rectangle {
                            width: Math.max(72, sourceSiteLabel.implicitWidth + 18)
                            height: 28
                            radius: 6
                            color: index === root.sourceSiteIndex
                                ? (root.theme ? root.theme.accentMutedColor : "#7a4a17")
                                : (siteHover.hovered ? (root.theme ? root.theme.panelRaisedColor : "#202020") : (root.theme ? root.theme.panelColor : "#242424"))
                            border.color: index === root.sourceSiteIndex
                                ? (root.theme ? root.theme.accentColor : "#f28c28")
                                : (root.theme ? root.theme.borderColor : "#353535")
                            border.width: 1

                            HoverHandler { id: siteHover }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.searchSourceSite(index)
                            }

                            Text {
                                id: sourceSiteLabel
                                anchors.centerIn: parent
                                color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                                text: name
                                font.pixelSize: 11
                                font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                elide: Text.ElideRight
                                width: parent.width - 10
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: "#101010"
                        border.color: root.theme ? root.theme.subtleBorderColor : "#282828"
                        border.width: 1

                        BusyIndicator {
                            anchors.centerIn: parent
                            running: root.sourceSearchModel && root.sourceSearchModel.loading
                            visible: running
                        }

                        ListView {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 4
                            clip: true
                            visible: root.sourceSearchModel && !root.sourceSearchModel.loading
                            model: root.sourceSearchModel

                            delegate: Rectangle {
                                width: ListView.view.width
                                height: 42
                                radius: 8
                                color: sourceResultHover.hovered ? "#1d1d1d" : "transparent"
                                border.color: sourceResultHover.hovered ? (root.theme ? root.theme.accentColor : "#f28c28") : "transparent"
                                border.width: 1

                                HoverHandler { id: sourceResultHover }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.activateSourceResult(model.vodName || root.currentVodName, model.vodPlayFrom || "", model.vodPlayUrl || "")
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 8

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        Text {
                                            Layout.fillWidth: true
                                            color: root.theme ? root.theme.textPrimaryColor : "#f3f3f3"
                                            text: model.vodName || ""
                                            elide: Text.ElideRight
                                            font.pixelSize: 12
                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                            font.bold: true
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            color: root.theme ? root.theme.textMutedColor : "#858585"
                                            text: (model.typeName ? (model.typeName + " · ") : "") + root.episodeCountText(model.vodRemarks || "", model.vodPlayUrl || "")
                                            elide: Text.ElideRight
                                            font.pixelSize: 10
                                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                        }
                                    }

                                    Text {
                                        color: root.theme ? root.theme.accentColor : "#f28c28"
                                        text: "载入"
                                        font.pixelSize: 11
                                        font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                                    }
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: root.sourceSearchModel && !root.sourceSearchModel.loading && root.sourceSearchModel.count === 0
                            color: root.theme ? root.theme.textMutedColor : "#858585"
                            text: root.currentVodName.length > 0 ? "选择站点搜索同名视频" : "没有当前视频名称"
                            font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                            font.pixelSize: root.theme ? root.theme.captionSize : 11
                        }
                    }
                }
            }

            ListView {
                id: playlistView
                anchors.fill: parent
                anchors.margins: 4
                anchors.topMargin: sourceSwitcher.height + 10
                spacing: 2
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.playlistModel
                displaced: Transition {
                    NumberAnimation { properties: "x,y"; duration: 200 }
                }

                delegate: Rectangle {
                    id: playlistDelegate
                    width: ListView.view.width
                    height: 40
                    radius: root.theme ? root.theme.controlRadius : 10
                    color: index === root.currentIndex ? "#1a2a1a" : (itemHover.hovered ? "#1a1a1a" : "transparent")
                    border.color: index === root.currentIndex
                        ? (root.theme ? root.theme.successColor : "#6ec16e")
                        : "transparent"
                    border.width: index === root.currentIndex ? 1 : 0

                    HoverHandler { id: itemHover }

                    MouseArea {
                        anchors.fill: parent
                        preventStealing: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.playlistItemRequested(index, filePath, title)
                            autoHideTimer.restart()
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
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
                                font.pixelSize: 10
                                font.bold: index === root.currentIndex
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

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
                }

                Text {
                    anchors.centerIn: parent
                    visible: root.playlistModel && root.playlistModel.count === 0
                    color: root.theme ? root.theme.textMutedColor : "#858585"
                    text: "暂无当前剧集"
                    font.family: root.theme ? root.theme.fontFamily : "Segoe UI"
                    font.pixelSize: root.theme ? root.theme.captionSize : 11
                }
            }
        }
    }
}
