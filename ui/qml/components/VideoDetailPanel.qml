import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Embedded video detail page (no longer a modal overlay).
// Fills its parent; emits backRequested() when the user wants to return to the list.
Rectangle {
    id: root
    color: "transparent"
    property var theme
    property var sidebar: null

    signal backRequested()
    signal playbackRequested()

    AppStrings {
        id: appText
    }

    // Vod data
    property int vodId: 0
    property string vodName
    property string vodPic
    property string vodRemarks
    property string vodYear
    property string vodArea
    property string vodClass
    property string vodActor
    property string vodDirector
    property string vodBlurb
    property string vodContent
    property string vodPlayFrom
    property string vodPlayUrl
    property string vodScore

    // Parse play sources
    property var playSources: parseSources(vodPlayFrom, vodPlayUrl)
    property int currentSource: 0
    readonly property int episodesPerPage: 50
    property int currentEpisodePage: 0

    onPlaySourcesChanged: {
        currentEpisodePage = 0
        var bestSource = 0
        var bestScore = -1
        for (var i = 0; i < playSources.length; i++) {
            var score = sourcePlaybackScore(playSources[i])
            if (score > bestScore) {
                bestScore = score
                bestSource = i
            }
        }
        currentSource = bestSource
    }

    onCurrentSourceChanged: currentEpisodePage = 0

    function parseSources(fromStr, urlStr) {
        if (!fromStr || !urlStr) return []
        var froms = fromStr.split("$$$")
        var urls = urlStr.split("$$$")
        var result = []
        for (var i = 0; i < Math.min(froms.length, urls.length); i++) {
            var episodes = []
            var parts = urls[i].split("#")
            for (var j = 0; j < parts.length; j++) {
                var kv = parts[j].split("$")
                if (kv.length >= 2) {
                    episodes.push({ name: kv[0], url: kv[1] })
                } else if (kv.length === 1 && kv[0].length > 0) {
                    episodes.push({ name: appText.playLabel, url: kv[0] })
                }
            }
            result.push({ name: froms[i], episodes: episodes })
        }
        return result
    }

    function normalizedEpisodeUrl(rawUrl) {
        var url = String(rawUrl || "").trim()
        if (url.length === 0) return ""
        if (url.indexOf("//") === 0) return "https:" + url
        if (url.indexOf("://") > 0) return url

        var current = playerController.apiSiteModel.currentIndex
        var base = current >= 0 ? playerController.apiSiteModel.baseUrlAt(current) : ""
        if (!base || base.length === 0) return url

        var match = base.match(/^(https?:\/\/[^\/]+)/)
        var origin = match && match.length > 1 ? match[1] : base
        if (url.charAt(0) === "/") return origin + url
        return origin + "/" + url
    }

    function episodePlaybackScore(rawUrl) {
        var url = String(rawUrl || "").toLowerCase()
        if (url.indexOf(".m3u8") >= 0) return 400
        if (url.indexOf("https://") === 0 && url.indexOf(".mp4") >= 0) return 320
        if (url.indexOf("http://") === 0 && url.indexOf(".mp4") >= 0) return 260
        if (url.indexOf("https://") === 0) return 220
        if (url.indexOf("http://") === 0) return 180
        if (url.indexOf("rtsp://") === 0 || url.indexOf("rtmp://") === 0) return 120
        return url.length > 0 ? 80 : 0
    }

    function sourcePlaybackScore(source) {
        if (!source || !source.episodes || source.episodes.length === 0) return 0
        var bestEpisodeScore = 0
        var m3u8Count = 0
        for (var i = 0; i < source.episodes.length; i++) {
            var score = episodePlaybackScore(source.episodes[i].url)
            bestEpisodeScore = Math.max(bestEpisodeScore, score)
            if (score === 400) m3u8Count++
        }
        return bestEpisodeScore + m3u8Count
    }

    function currentEpisodes() {
        if (playSources.length === 0) return []
        var source = playSources[currentSource]
        return source && source.episodes ? source.episodes : []
    }

    function episodePageCount() {
        return Math.ceil(currentEpisodes().length / episodesPerPage)
    }

    function episodesOnCurrentPage() {
        var episodes = currentEpisodes()
        var start = currentEpisodePage * episodesPerPage
        return episodes.slice(start, Math.min(start + episodesPerPage, episodes.length))
    }

    function normalizedPlaybackSources() {
        var result = []
        for (var sourceIndex = 0; sourceIndex < playSources.length; sourceIndex++) {
            var source = playSources[sourceIndex]
            var episodes = []
            if (source && source.episodes) {
                for (var episodeIndex = 0; episodeIndex < source.episodes.length; episodeIndex++) {
                    episodes.push({
                        title: source.episodes[episodeIndex].name,
                        url: normalizedEpisodeUrl(source.episodes[episodeIndex].url)
                    })
                }
            }
            result.push({ name: source.name, episodes: episodes })
        }
        return result
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // Top bar: back button + title
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 32
                radius: 6
                color: backMouse.containsMouse ? theme.panelRaisedColor : theme.panelColor
                border.color: backMouse.containsMouse ? theme.accentColor : theme.borderColor
                border.width: 1

                MouseArea {
                    id: backMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.backRequested()
                }

                Text {
                    anchors.centerIn: parent
                    text: "← 返回"
                    color: backMouse.containsMouse ? theme.accentColor : theme.textSecondaryColor
                    font.pixelSize: 12
                    font.family: theme.fontFamily
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.vodName
                color: theme.textPrimaryColor
                font.pixelSize: 16
                font.bold: true
                font.family: theme.fontFamily
                elide: Text.ElideRight
            }
        }

        // Content area
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // Poster
            Rectangle {
                Layout.preferredWidth: Math.max(320, root.width * 0.5 - 8)
                Layout.maximumWidth: Math.max(320, root.width * 0.5 - 8)
                Layout.fillWidth: false
                Layout.fillHeight: true
                radius: 8
                color: theme.surfaceColor
                clip: true

                Image {
                    anchors.fill: parent
                    source: root.vodPic || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
                                        Math.ceil(height * Screen.devicePixelRatio))

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: parent.status === Image.Loading
                        visible: running
                        width: 32
                        height: 32
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "馃幀"
                        font.pixelSize: 64
                        visible: parent.status === Image.Error || !root.vodPic
                        color: theme.textMutedColor
                    }
                }
            }

            // Info column
            ColumnLayout {
                Layout.preferredWidth: Math.max(320, root.width * 0.5 - 8)
                Layout.minimumWidth: 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                // Meta info
                Flow {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: {
                            var parts = []
                            if (root.vodYear) parts.push(root.vodYear)
                            if (root.vodArea) parts.push(root.vodArea)
                            if (root.vodClass) parts.push(root.vodClass)
                            if (root.vodScore && root.vodScore !== "0.0") parts.push("评分 " + root.vodScore)
                            if (root.vodRemarks) parts.push(root.vodRemarks)
                            return parts
                        }

                        Rectangle {
                            radius: 4
                            color: theme.surfaceColor
                            border.color: theme.borderColor
                            border.width: 1
                            implicitWidth: metaLabel.implicitWidth + 12
                            implicitHeight: metaLabel.implicitHeight + 6

                            Text {
                                id: metaLabel
                                anchors.centerIn: parent
                                text: modelData
                                color: theme.textSecondaryColor
                                font.pixelSize: 11
                                font.family: theme.fontFamily
                            }
                        }
                    }
                }

                // Director
                Text {
                    visible: root.vodDirector.length > 0
                    Layout.fillWidth: true
                    text: "导演: " + root.vodDirector
                    color: theme.textMutedColor
                    font.pixelSize: 12
                    font.family: theme.fontFamily
                    elide: Text.ElideRight
                }

                // Actor
                Text {
                    visible: root.vodActor.length > 0
                    Layout.fillWidth: true
                    text: appText.actorPrefix + root.vodActor
                    color: theme.textMutedColor
                    font.pixelSize: 14
                    font.family: theme.fontFamily
                    elide: Text.ElideRight
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                }

                Text {
                    Layout.fillWidth: true
                    text: "简介"
                    color: theme.textPrimaryColor
                    font.pixelSize: 16
                    font.bold: true
                    font.family: theme.fontFamily
                }

                // Description
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentHeight: descText.implicitHeight

                    Text {
                        id: descText
                        width: parent.width
                        text: {
                            var t = root.vodBlurb || root.vodContent || ""
                            // Strip HTML tags
                            return t.replace(/<[^>]*>/g, '')
                        }
                        color: theme.textSecondaryColor
                        font.pixelSize: 14
                        font.family: theme.fontFamily
                        wrapMode: Text.Wrap
                        lineHeight: 1.35
                        lineHeightMode: Text.ProportionalHeight
                    }
                }
            }
        }

        // Source tabs + episodes
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            radius: 8
            color: theme.surfaceColor
            border.color: theme.borderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                // Source tabs
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Repeater {
                        model: root.playSources

                        Rectangle {
                            Layout.preferredWidth: sourceLabel.implicitWidth + 16
                            Layout.preferredHeight: 26
                            radius: 4
                            color: root.currentSource === index ? theme.accentColor : (sourceMouse.containsMouse ? theme.panelRaisedColor : theme.panelColor)

                            MouseArea {
                                id: sourceMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.currentSource = index
                            }

                            Text {
                                id: sourceLabel
                                anchors.centerIn: parent
                                text: modelData.name
                                color: root.currentSource === index ? "#ffffff" : theme.textSecondaryColor
                                font.pixelSize: 11
                                font.family: theme.fontFamily
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        Layout.preferredWidth: 86
                        Layout.preferredHeight: 26
                        radius: 4
                        color: downloadSourceMouse.containsMouse ? theme.accentMutedColor : theme.panelColor
                        border.color: theme.accentColor
                        border.width: 1
                        visible: root.playSources.length > 0

                        MouseArea {
                            id: downloadSourceMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                var src = root.playSources[root.currentSource]
                                if (!src || !src.episodes) return
                                var downloadEpisodes = []
                                for (var i = 0; i < src.episodes.length; i++) {
                                    downloadEpisodes.push({
                                        title: src.episodes[i].name,
                                        url: src.episodes[i].url
                                    })
                                }
                                playerController.downloadVideoEpisodes(root.vodName, root.vodPic, downloadEpisodes)
                                if (root.sidebar) root.sidebar.currentIndex = 3
                                root.backRequested()
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "Download source"
                            color: theme.accentColor
                            font.pixelSize: 11
                            font.family: theme.fontFamily
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 86
                        Layout.preferredHeight: 26
                        radius: 4
                        color: saveM3u8Mouse.containsMouse ? theme.accentMutedColor : theme.panelColor
                        border.color: theme.accentColor
                        border.width: 1
                        visible: root.playSources.length > 0

                        MouseArea {
                            id: saveM3u8Mouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                var src = root.playSources[root.currentSource]
                                if (!src || !src.episodes) return
                                var downloadEpisodes = []
                                for (var i = 0; i < src.episodes.length; i++) {
                                    downloadEpisodes.push({
                                        title: src.episodes[i].name,
                                        url: src.episodes[i].url
                                    })
                                }
                                playerController.saveVideoM3u8Episodes(root.vodName, root.vodPic, downloadEpisodes)
                                if (root.sidebar) root.sidebar.currentIndex = 3
                                root.backRequested()
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "仅存 m3u8"
                            color: theme.accentColor
                            font.pixelSize: 11
                            font.family: theme.fontFamily
                        }
                    }
                }

                // Episode ranges
                Flickable {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 28 : 0
                    visible: root.episodePageCount() > 1
                    clip: true
                    contentWidth: episodeRangeRow.width
                    contentHeight: height
                    boundsBehavior: Flickable.StopAtBounds

                    Row {
                        id: episodeRangeRow
                        height: parent.height
                        spacing: 4

                        Repeater {
                            model: root.episodePageCount()

                            Rectangle {
                                width: rangeLabel.implicitWidth + 20
                                height: 28
                                radius: 4
                                color: root.currentEpisodePage === index
                                       ? theme.accentColor
                                       : (rangeMouse.containsMouse ? theme.panelRaisedColor : theme.panelColor)
                                border.color: root.currentEpisodePage === index
                                              ? theme.accentColor
                                              : theme.borderColor
                                border.width: 1

                                MouseArea {
                                    id: rangeMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.currentEpisodePage = index
                                }

                                Text {
                                    id: rangeLabel
                                    anchors.centerIn: parent
                                    text: {
                                        var start = index * root.episodesPerPage + 1
                                        var end = Math.min((index + 1) * root.episodesPerPage,
                                                           root.currentEpisodes().length)
                                        return start + "-" + end
                                    }
                                    color: root.currentEpisodePage === index
                                           ? "#ffffff"
                                           : theme.textSecondaryColor
                                    font.pixelSize: 11
                                    font.family: theme.fontFamily
                                }
                            }
                        }
                    }
                }

                // Episodes grid
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: episodeFlow.height

                    Flow {
                        id: episodeFlow
                        width: parent.width
                        spacing: 4

                        Repeater {
                            model: root.episodesOnCurrentPage()

                            Rectangle {
                                width: 72
                                height: 28
                                radius: 4
                                color: epMouse.containsMouse ? theme.accentMutedColor : theme.panelColor
                                border.color: epMouse.containsMouse ? theme.accentColor : theme.borderColor
                                border.width: 1

                                MouseArea {
                                    id: epMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        var url = root.normalizedEpisodeUrl(modelData.url)
                                        console.log("Episode click:", modelData.name, url)
                                        if (url.length <= 0) return

                                        playerController.setCurrentVodName(root.vodName)
                                        var episodeIndex = root.currentEpisodePage
                                                           * root.episodesPerPage + index
                                        playerController.setPlaybackSources(
                                            root.normalizedPlaybackSources(),
                                            root.currentSource,
                                            episodeIndex)
                                        var src = root.playSources[root.currentSource]
                                        var playlistEpisodes = []
                                        if (src && src.episodes) {
                                            for (var i = 0; i < src.episodes.length; i++) {
                                                playlistEpisodes.push({
                                                    title: src.episodes[i].name,
                                                    url: root.normalizedEpisodeUrl(src.episodes[i].url)
                                                })
                                            }
                                            playerController.setPlaylistEpisodes(playlistEpisodes,
                                                                                 episodeIndex)
                                        }

                                        if (root.sidebar) root.sidebar.currentIndex = 1
                                        root.playbackRequested()
                                        playerController.playVodUrl(url)
                                    }
                                }

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.name
                                    color: theme.textPrimaryColor
                                    font.pixelSize: 11
                                    font.family: theme.fontFamily
                                    elide: Text.ElideRight
                                    width: parent.width - 8
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

