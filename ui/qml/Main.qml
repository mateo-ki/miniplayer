import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: window

    width: 1440
    height: 900
    visible: true
    title: "MeloBox"
    minimumWidth: 900
    minimumHeight: 600
    color: appTheme.windowColor
    visibility: Window.Windowed
    flags: Qt.Window
    Component.onCompleted: {
        visible = true
        rebuildShortVideoSites()
        show()
        raise()
        requestActivate()
    }
    onVisibleChanged: {
        if (!visible) {
            mediaDrawer.close()
            logDrawer.close()
            controlBar.closePopups()
            floatingControlBar.closePopups()
        }
    }
    onActiveChanged: {
        playerController.resetMouseCursor()
        if (!active) {
            window.endHoldSpeed()
            mediaBrowserWindow.hide()
        } else {
            scheduleVideoSurfaceRefresh("window-active")
        }
    }
    onClosing: function(event) {
        window.forceQuit = true
        // 不在这里 stop(): 管线线程等待 (最长 3s x3) 会拖慢退出,
        // aboutToQuit 里的 prepareExit 会快速关停并保存设置。
        event.accepted = true
    }

    // 閿洏蹇嵎閿?(Shortcut 涓嶄緷璧栫劍鐐?
    property int seekAccum: 0
    property int keyboardSeekStepMs: 5000
    property bool trayAvailable: false
    property bool forceQuit: false
    property bool videoFillMode: false
    property real playbackRateBeforeHold: 1.0
    property bool holdSpeedActive: false
    property bool playbackGestureFeedbackVisible: false
    property string playbackGestureFeedbackIcon: ""
    property string playbackGestureFeedbackText: ""
    property bool bottomControlReveal: true
    property int selectedShortVideoSiteIndex: -1
    property string shortVideoPlaybackMode: "random"
    property bool settingsRestored: false
    property bool restoringUiSettings: false
    onVideoFillModeChanged: saveUiSetting("videoFillMode", videoFillMode)
    onSelectedShortVideoSiteIndexChanged: saveSelectedShortVideoSite()
    onShortVideoPlaybackModeChanged: saveUiSetting("shortVideoPlaybackMode", shortVideoPlaybackMode)
    property bool shortVideoAutoAdvanceGuard: false
    property double shortVideoIgnoreEndedUntil: 0
    property double shortVideoLoadStartedAt: 0
    property int shortVideoLoadRetryCount: 0
    property double shortVideoLastProgressAt: 0
    property int shortVideoLastPositionMs: -1
    property int shortVideoNoProgressTicks: 0
    property string shortVideoErrorMessage: ""
    property int shortVideoSwitchCount: 0
    property int shortVideoFailedSourceCount: 0
    property var shortVideoHistory: []
    property int shortVideoHistoryCursor: -1
    property double shortVideoLastWheelAt: 0
    property int shortVideoWheelDirection: 0
    property int shortVideoWheelDelta: 0
    property double shortVideoWheelLockedUntil: 0
    property var shortVideoPrefetchPendingSites: []
    property bool mediaBrowserOpening: false
    property bool videoBrowserActive: true
    property bool animePlaybackActive: false
    property bool videoResolving: false
    property int animePlaybackVodId: 0
    property bool animeCommentsDrawerOpen: false
    property bool animeEpisodesOpen: false
    property string animeCurrentEpisode: ""
    // 弹幕(B 站风格,站内 pc/danmu),持久化到 UI 设置。
    property bool danmakuEnabled: true
    property real danmakuOpacity: 0.85
    property int danmakuDisplayMode: 0        // 0全部 1滚动 2顶部 3底部
    property real danmakuFontScale: 1.0
    onDanmakuOpacityChanged: saveUiSetting("danmakuOpacity", danmakuOpacity)
    onDanmakuDisplayModeChanged: saveUiSetting("danmakuDisplayMode", danmakuDisplayMode)
    onDanmakuFontScaleChanged: saveUiSetting("danmakuFontScale", danmakuFontScale)
    onDanmakuEnabledChanged: saveUiSetting("danmakuEnabled", danmakuEnabled)
    property bool playerPageActive: sidebar.currentIndex === 1 || sidebar.currentIndex === 6
    property bool videoBrowserVisible: sidebar.currentIndex === 1 && videoBrowserActive
    property bool shortVideoPlaybackActive: sidebar.currentIndex === 6
        && isCurrentShortVideo()
    onShortVideoPlaybackActiveChanged: {
        if (sidebar.currentIndex !== 6)
            return
        playerController.setMpvSurfaceVisible(shortVideoPlaybackActive)
        if (shortVideoPlaybackActive)
            scheduleVideoSurfaceRefresh("short-video-active")
    }
    readonly property real playerSearchHotZoneRatio: 0.38
    readonly property int playerSearchHotZoneMinWidth: 240
    readonly property int playerSearchHotZoneMaxWidth: 440
    readonly property int playerSearchHotZoneHeight: 12
    readonly property int playerControlHotZoneHeight: appTheme.controlBarHeight
    property bool immersiveMode: sidebar.currentIndex === 1
        && (videoFillMode || window.visibility === Window.FullScreen)
    property bool playerControlsVisible: sidebar.currentIndex === 1
        && !videoBrowserVisible
        && bottomControlReveal
    onVideoBrowserVisibleChanged: {
        if (sidebar.currentIndex !== 1)
            return
        playerController.setMpvSurfaceVisible(!videoBrowserVisible)
        if (!videoBrowserVisible)
            scheduleVideoSurfaceRefresh("video-browser-close")
    }

    AppStrings {
        id: appText
    }

    ListModel {
        id: shortVideoSiteList
    }

    ListModel {
        id: shortVideoPrefetchPool
    }

    function isCurrentShortVideo() {
        return playerController.currentShortVideoUrl.length > 0
            && playerController.currentFile === playerController.currentShortVideoUrl
    }

    function shortVideoDebug(message) {
        playerController.logShortVideoDebug(message
            + " | mode=" + shortVideoPlaybackMode
            + " guard=" + shortVideoAutoAdvanceGuard
            + " selected=" + selectedShortVideoSiteIndex
            + " pageActive=" + playerPageActive
            + " currentFile=" + playerController.currentFile
            + " shortUrl=" + playerController.currentShortVideoUrl
            + " pos=" + playerController.positionMs
            + " dur=" + playerController.durationMs
            + " loading=" + playerController.loading
            + " playing=" + playerController.isPlaying
            + " seeking=" + playerController.seeking)
    }

    function resetShortVideoProgressWatch() {
        shortVideoLastProgressAt = Date.now()
        shortVideoLastPositionMs = -1
        shortVideoNoProgressTicks = 0
    }

    function refreshVideoSurfaceNow(reason) {
        playerController.resetMouseCursor()
        if (sidebar.currentIndex === 6 && shortVideoPlaybackActive
                && typeof shortVideoVideoPane !== "undefined") {
            shortVideoVideoPane.syncMpvVideoGeometry()
        } else if (typeof playerPage !== "undefined") {
            playerPage.syncMpvVideoGeometry()
        }
        playerController.refreshMpvVideoWindow()
        if (videoGestureWindow.visible) {
            videoGestureWindow.raise()
            playerPage.raisePlaybackOverlays()
        }
        window.raiseEpisodeOverlays()
        if (playerSearchHotZoneWindow.visible)
            playerSearchHotZoneWindow.raise()
        if (!immersiveMode && normalControlWindow.visible)
            normalControlWindow.raise()
        if (immersiveMode && immersiveControlWindow.visible)
            immersiveControlWindow.raise()
    }

    function scheduleVideoSurfaceRefresh(reason) {
        videoSurfaceRefreshTimer.reason = reason || ""
        videoSurfaceRefreshTimer.requestedTicks = reason && reason.indexOf("short-video") >= 0 ? 14 : 6
        videoSurfaceRefreshTimer.restart()
    }

    function rebuildShortVideoSites() {
        shortVideoSiteList.clear()
        for (var i = 0; i < services.apiSiteModel.count; ++i) {
            if (services.apiSiteModel.typeAt(i) !== "shortvideo") continue
            shortVideoSiteList.append({
                "sourceIndex": i,
                "title": services.apiSiteModel.nameAt(i),
                "subtitle": services.apiSiteModel.sanitizeDisplay(services.apiSiteModel.baseUrlAt(i))
            })
        }
        var savedShortVideoUrl = playerController.uiSetting("selectedShortVideoSiteUrl", "")
        var savedShortVideoIndex = findSiteIndexByUrl("shortvideo", savedShortVideoUrl)
        if (savedShortVideoIndex >= 0)
            selectedShortVideoSiteIndex = savedShortVideoIndex
        if (shortVideoSiteList.count > 0
                && (selectedShortVideoSiteIndex < 0
                    || services.apiSiteModel.typeAt(selectedShortVideoSiteIndex) !== "shortvideo")) {
            selectedShortVideoSiteIndex = shortVideoSiteList.get(0).sourceIndex
        }
        refillShortVideoPrefetchPool()
    }

    function shortVideoPoolContains(sourceIndex) {
        for (var i = 0; i < shortVideoPrefetchPool.count; ++i) {
            if (shortVideoPrefetchPool.get(i).sourceIndex === sourceIndex)
                return true
        }
        return shortVideoPrefetchPendingSites.indexOf(sourceIndex) >= 0
    }

    function refillShortVideoPrefetchPool() {
        if (shortVideoSiteList.count <= 0)
            return
        var attempts = 0
        while (shortVideoPrefetchPool.count + shortVideoPrefetchPendingSites.length < 5
                && attempts < shortVideoSiteList.count * 2) {
            attempts += 1
            var candidate = randomShortVideoSiteIndex(selectedShortVideoSiteIndex)
            if (candidate < 0 || shortVideoPoolContains(candidate))
                continue
            var title = "短视频"
            var sourceUrl = ""
            for (var i = 0; i < shortVideoSiteList.count; ++i) {
                if (shortVideoSiteList.get(i).sourceIndex === candidate) {
                    title = shortVideoSiteList.get(i).title
                    sourceUrl = shortVideoSiteList.get(i).subtitle
                    break
                }
            }
            var pending = shortVideoPrefetchPendingSites.slice()
            pending.push(candidate)
            shortVideoPrefetchPendingSites = pending
            playerController.prefetchShortVideo(candidate, sourceUrl, title)
        }
    }
    function restoreUiSettings() {
        if (settingsRestored)
            return

        restoringUiSettings = true
        videoFillMode = playerController.uiSetting("videoFillMode", videoFillMode)
        shortVideoPlaybackMode = playerController.uiSetting("shortVideoPlaybackMode", shortVideoPlaybackMode)
        selectedShortVideoSiteIndex = playerController.uiSetting("selectedShortVideoSiteIndex", selectedShortVideoSiteIndex)
        var rawPage = playerController.uiSetting("currentPage", 1)
        var savedPage = Number(rawPage)
        if (isNaN(savedPage) || savedPage < 1)
            savedPage = 1

        // 现有有效页码：视频(1), 历史(2), 更新(3), 图片(4), 表情(5), 短视频(6), 语音(7), 动漫(9), 我的(10)
        var validPages = [1, 2, 3, 4, 5, 6, 7, 9, 10]
        if (validPages.indexOf(savedPage) !== -1) {
            // 已在合法集合内，直接使用（动漫9、我的10保留本身，不再错误转换为8）
            sidebar.currentIndex = savedPage
        } else if (savedPage === 11) {
            // 旧版平移兼容：动漫旧11 -> 新9
            sidebar.currentIndex = 9
        } else if (savedPage === 12) {
            // 旧版平移兼容：我的旧12 -> 新10
            sidebar.currentIndex = 10
        } else {
            // 0(旧首页)、8(已废弃站点管理)及其它无效数值一律默认回落到视频页(1)
            sidebar.currentIndex = 1
        }
        restoringUiSettings = false

        // 弹幕设置(与 videoFillMode 同款持久化)。
        danmakuEnabled = playerController.uiSetting("danmakuEnabled", danmakuEnabled)
        danmakuOpacity = playerController.uiSetting("danmakuOpacity", danmakuOpacity)
        danmakuDisplayMode = playerController.uiSetting("danmakuDisplayMode", danmakuDisplayMode)
        danmakuFontScale = playerController.uiSetting("danmakuFontScale", danmakuFontScale)

        settingsRestored = true
    }

    function findSiteIndexByUrl(typeName, baseUrl) {
        if (!baseUrl || baseUrl.length === 0)
            return -1
        for (var i = 0; i < services.apiSiteModel.count; ++i) {
            if (services.apiSiteModel.typeAt(i) === typeName
                    && services.apiSiteModel.baseUrlAt(i) === baseUrl) {
                return i
            }
        }
        return -1
    }

    function saveUiSetting(key, value) {
        if (!settingsRestored || restoringUiSettings)
            return
        playerController.saveUiSetting(key, value)
    }

    function saveSelectedShortVideoSite() {
        saveUiSetting("selectedShortVideoSiteIndex", selectedShortVideoSiteIndex)
        if (selectedShortVideoSiteIndex >= 0 && selectedShortVideoSiteIndex < services.apiSiteModel.count)
            saveUiSetting("selectedShortVideoSiteUrl", services.apiSiteModel.baseUrlAt(selectedShortVideoSiteIndex))
    }

    function openMediaBrowser(tabName) {
        if (mediaBrowserOpening)
            return
        var targetTab = tabName === "shortvideo" ? "shortvideo" : "video"
        if (targetTab === "video") {
            videoBrowserActive = true
            bottomControlReveal = false
            sidebar.currentIndex = 1
            Qt.callLater(function() { playerPage.openListPage() })
            mediaBrowserWindow.hide()
            return
        }
        if (mediaBrowserWindow.visible) {
            mediaBrowserWindow.activeTab = targetTab
            rebuildShortVideoSites()
            mediaBrowserWindow.raise()
            mediaBrowserWindow.requestActivate()
            return
        }
        mediaBrowserOpening = true
        rebuildShortVideoSites()
        mediaBrowserWindow.activeTab = targetTab
        mediaBrowserWindow.show()
        mediaBrowserWindow.raise()
        mediaBrowserWindow.requestActivate()
        revealPlayerControls()
        mediaBrowserOpenCooldown.restart()
    }

    function toggleAnimeCommentsDrawer() {
        if (!animePlaybackActive || animePlaybackVodId <= 0)
            return
        animeCommentsDrawerOpen = !animeCommentsDrawerOpen
        if (animeCommentsDrawerOpen) {
            services.dmghgAnimeModel.loadComments(animePlaybackVodId, 1)
            animeCommentsDrawerWindow.raise()
        } else {
            animeCommentsDrawerWindow.close()
        }
    }


    function playShortVideoSite(siteIndex, recordHistory, prefetchedUrl) {
        if (siteIndex < 0 || siteIndex >= services.apiSiteModel.count) {
            shortVideoDebug("playShortVideoSite ignored invalid siteIndex=" + siteIndex)
            return
        }
        if (recordHistory !== false) {
            var history = shortVideoHistory.slice(0, shortVideoHistoryCursor + 1)
            history.push(siteIndex)
            shortVideoHistory = history
            shortVideoHistoryCursor = history.length - 1
        }
        shortVideoDebug("playShortVideoSite enter siteIndex=" + siteIndex
            + " name=" + services.apiSiteModel.nameAt(siteIndex)
            + " url=" + services.apiSiteModel.baseUrlAt(siteIndex))
        playerController.resetMouseCursor()
        selectedShortVideoSiteIndex = siteIndex
        shortVideoErrorMessage = ""
        shortVideoLoadStartedAt = Date.now()
        if (!shortVideoAutoAdvanceGuard)
            shortVideoLoadRetryCount = 0
        resetShortVideoProgressWatch()
        shortVideoAutoAdvanceGuard = false
        shortVideoIgnoreEndedUntil = Date.now() + 1200
        bottomControlReveal = false
        sidebar.currentIndex = 6
        mediaBrowserWindow.hide()
        if (playerSearchHotZoneWindow.visible)
            playerSearchHotZoneWindow.raise()
        if (normalControlWindow.visible)
            normalControlWindow.raise()
        var playbackUrl = prefetchedUrl && prefetchedUrl.length > 0
            ? prefetchedUrl
            : services.apiSiteModel.baseUrlAt(siteIndex)
        playerController.playShortVideoUrl(playbackUrl,
                                           services.apiSiteModel.nameAt(siteIndex))
        shortVideoDebug("playShortVideoSite requested")
        scheduleVideoSurfaceRefresh("short-video-play")
    }

    function randomShortVideoSiteIndex(excludeIndex) {
        if (shortVideoSiteList.count <= 0)
            return -1
        if (shortVideoSiteList.count === 1)
            return shortVideoSiteList.get(0).sourceIndex
        var candidates = []
        for (var i = 0; i < shortVideoSiteList.count; ++i) {
            var sourceIndex = shortVideoSiteList.get(i).sourceIndex
            if (sourceIndex !== excludeIndex)
                candidates.push(sourceIndex)
        }
        return candidates[Math.floor(Math.random() * candidates.length)]
    }

    function playNextShortVideoByMode() {
        shortVideoDebug("playNextShortVideoByMode enter")
        if (sidebar.currentIndex !== 6) {
            shortVideoDebug("playNextShortVideoByMode ignored inactive page")
            return
        }
        if (shortVideoHistoryCursor >= 0
                && shortVideoHistoryCursor < shortVideoHistory.length - 1) {
            shortVideoHistoryCursor += 1
            shortVideoSwitchCount += 1
            playShortVideoSite(shortVideoHistory[shortVideoHistoryCursor], false)
            return
        }
        rebuildShortVideoSites()
        refillShortVideoPrefetchPool()
        var prefetchedItem = shortVideoPrefetchPool.count > 0 ? shortVideoPrefetchPool.get(0) : null
        var nextIndex = prefetchedItem
            ? prefetchedItem.sourceIndex
            : randomShortVideoSiteIndex(selectedShortVideoSiteIndex)
        var prefetchedUrl = prefetchedItem ? prefetchedItem.videoUrl : ""
        if (prefetchedItem)
            shortVideoPrefetchPool.remove(0)
        Qt.callLater(refillShortVideoPrefetchPool)
        if (nextIndex < 0 && shortVideoSiteList.count > 0)
            nextIndex = shortVideoSiteList.get(0).sourceIndex
        shortVideoDebug("playNextShortVideoByMode nextIndex=" + nextIndex + " shortVideoSiteCount=" + shortVideoSiteList.count)
        if (nextIndex >= 0) {
            shortVideoSwitchCount += 1
            playShortVideoSite(nextIndex, true, prefetchedUrl)
        } else {
            shortVideoDebug("playNextShortVideoByMode no next site")
        }
    }

    function playPreviousShortVideo() {
        shortVideoDebug("playPreviousShortVideo enter")
        if (sidebar.currentIndex !== 6 || shortVideoHistoryCursor <= 0) {
            shortVideoDebug("playPreviousShortVideo no previous item")
            return
        }
        shortVideoHistoryCursor -= 1
        shortVideoSwitchCount += 1
        playShortVideoSite(shortVideoHistory[shortVideoHistoryCursor], false)
    }

    function playPreviousEpisode() {
        if (sidebar.currentIndex === 6) {
            playPreviousShortVideo()
            return
        }
        playerController.playPrevious()
    }

    function playNextEpisode() {
        if (sidebar.currentIndex === 6) {
            playNextShortVideoByMode()
            return
        }
        playerController.playNext()
    }

    function handleShortVideoWheel(delta) {
        if (delta === 0)
            return
        var now = Date.now()
        if (now < shortVideoWheelLockedUntil)
            return
        var direction = delta < 0 ? -1 : 1
        if (direction !== shortVideoWheelDirection
                || now - shortVideoLastWheelAt > 1200) {
            shortVideoWheelDirection = direction
            shortVideoWheelDelta = Math.abs(delta)
        } else {
            shortVideoWheelDelta += Math.abs(delta)
        }
        shortVideoLastWheelAt = now
        if (shortVideoWheelDelta < 200)
            return
        shortVideoWheelDelta = 0
        shortVideoWheelDirection = 0
        shortVideoWheelLockedUntil = now + 350
        if (direction < 0)
            playNextShortVideoByMode()
        else
            playPreviousShortVideo()
    }

    function currentShortVideoSourceTitle() {
        for (var i = 0; i < shortVideoSiteList.count; ++i) {
            if (shortVideoSiteList.get(i).sourceIndex === selectedShortVideoSiteIndex)
                return shortVideoSiteList.get(i).title
        }
        return "短视频"
    }

    function setAnimePlaybackPlaylist(vodId, currentPart) {
        var episodes = animePage.model && animePage.model.episodes
            ? animePage.model.episodes : []
        var playlist = []
        var currentIndex = 0
        for (var i = 0; i < episodes.length; ++i) {
            var title = String(episodes[i] || "").trim()
            if (!title) continue
            playlist.push({
                // 动漫地址需要异步解析；占位地址只用于统一承载集数模型。
                url: "dmghg://episode/" + i,
                title: title
            })
            if (title === String(currentPart || "").trim())
                currentIndex = playlist.length - 1
        }
        if (playlist.length > 0)
            playerController.setPlaylistEpisodes(playlist, currentIndex)
        // 两条点集路径都经过这里，观看上报只挂这一处。
        var animeDetail = animePage.model && animePage.model.detail ? animePage.model.detail : ({})
        // 上报用上游真实编号：detail.vodId 在新协议下只是本次会话的句柄，重启就错位。
        services.poolMe.reportPlay(String(animeDetail.vodIdV3 || vodId || ""),
                                   animeDetail.vodName || "", String(currentPart || "").trim())
    }

    function prepareAnimePlayback(part, resolveEpisode) {
        var title = String(part || "").trim()
        if (!title) return
        window.videoResolving = true
        window.animeCurrentEpisode = title
        window.animePlaybackVodId = animePage.model.detail.vodId || animePage.selectedVodId
        window.setAnimePlaybackPlaylist(window.animePlaybackVodId, title)
        window.animeCommentsDrawerOpen = false
        window.animePlaybackActive = true
        sidebar.activePageOverride = 9
        sidebar.currentIndex = 1
        window.videoBrowserActive = false
        window.bottomControlReveal = false
        playerController.stop()
        if (resolveEpisode) {
            services.dmghgAnimeModel.playEpisode(window.animePlaybackVodId, title)
            // 与解析并行预取站内弹幕(按 part 全集分页)。
            services.dmghgAnimeModel.loadDanmaku(window.animePlaybackVodId, title)
        }
    }

    function retryShortVideo() {
        shortVideoFailedSourceCount = 0
        if (selectedShortVideoSiteIndex >= 0)
            playShortVideoSite(selectedShortVideoSiteIndex, false)
    }

    function failoverShortVideo(message) {
        shortVideoErrorMessage = message || "当前短视频源播放失败"
        shortVideoFailedSourceCount += 1
        if (shortVideoFailedSourceCount >= 6) {
            // 连续失败过多 (接口可能整体不可用): 停止自动切换, 保留错误
            // 提示等用户手动重试, 避免无限失败循环刷屏。
            shortVideoAutoAdvanceGuard = true
            shortVideoDebug("failoverShortVideo give up after " + shortVideoFailedSourceCount + " failures")
            return
        }
        shortVideoAutoAdvanceGuard = true
        shortVideoFailoverTimer.restart()
    }

    Connections {
        target: services.apiSiteModel
        function onCountChanged() { window.rebuildShortVideoSites() }
        function onSiteContentChanged() { window.rebuildShortVideoSites() }
        function onOrderChanged() { window.rebuildShortVideoSites() }
    }

    Connections {
        target: playerController
        function onPositionMsChanged() {
            if (!window.animePlaybackActive || window.animePlaybackVodId <= 0)
                return
            services.dmghgAnimeModel.ensureDanmakuProgress(
                window.animePlaybackVodId,
                window.animeCurrentEpisode,
                playerController.positionMs
            )
        }
        function onPlaylistEpisodeRequested(index, title) {
            if (!window.animePlaybackActive || !String(title || "").length)
                return
            window.prepareAnimePlayback(title, true)
        }
        function onShortVideoPrefetched(sourceIndex, videoUrl, label) {
            var pending = window.shortVideoPrefetchPendingSites.slice()
            var pendingIndex = pending.indexOf(sourceIndex)
            if (pendingIndex >= 0)
                pending.splice(pendingIndex, 1)
            window.shortVideoPrefetchPendingSites = pending
            if (videoUrl && videoUrl.length > 0 && !window.shortVideoPoolContains(sourceIndex)) {
                shortVideoPrefetchPool.append({
                    "sourceIndex": sourceIndex,
                    "title": label,
                    "videoUrl": videoUrl,
                    "state": "ready"
                })
            }
            if (videoUrl && videoUrl.length > 0)
                Qt.callLater(window.refillShortVideoPrefetchPool)
            else
                shortVideoPrefetchRetryTimer.restart()
        }

        function onCurrentFileChanged() {
            playbackGestureFeedbackTimer.stop()
            window.playbackGestureFeedbackVisible = false
            if (window.playerPageActive)
                window.scheduleVideoSurfaceRefresh("current-file-changed")
        }
        function onPlaybackStateChanged() {
            if (window.playerPageActive)
                window.scheduleVideoSurfaceRefresh("playback-state-changed")
            if (window.isCurrentShortVideo())
                window.scheduleVideoSurfaceRefresh("short-video-playback-state")
        }
        function onPlaybackEnded() {
            window.shortVideoDebug("onPlaybackEnded enter")
            if (!window.playerPageActive) {
                window.shortVideoDebug("onPlaybackEnded ignored inactive page")
                return
            }
            if (playerController.currentShortVideoUrl.length === 0) {
                var nextEpisodeIndex = playerController.currentIndex + 1
                if (sidebar.currentIndex === 1
                        && !window.videoBrowserVisible
                        && nextEpisodeIndex < playerController.playlistModel.count) {
                    playerController.playFromPlaylist(nextEpisodeIndex)
                }
                return
            }
            if (playerController.currentFile !== playerController.currentShortVideoUrl) {
                window.shortVideoDebug("onPlaybackEnded ignored file mismatch")
                return
            }
            if (Date.now() < window.shortVideoIgnoreEndedUntil) {
                window.shortVideoDebug("onPlaybackEnded ignored within ignore window remainMs=" + (window.shortVideoIgnoreEndedUntil - Date.now()))
                return
            }
            if (window.shortVideoAutoAdvanceGuard) {
                window.shortVideoDebug("onPlaybackEnded ignored guard")
                return
            }
            window.shortVideoAutoAdvanceGuard = true
            window.shortVideoDebug("onPlaybackEnded advancing")
            window.playNextShortVideoByMode()
        }
        function onPlaybackFailed(message) {
            if (sidebar.currentIndex === 6 && window.isCurrentShortVideo())
                window.failoverShortVideo(message)
        }
    }

    // 动漫选集解析完成 -> 切到播放器播放真实流地址。
    // 信号发自 services.dmghgAnimeModel,故此处单独连接。
    Connections {
        target: services.dmghgAnimeModel
        function onEpisodeResolved(url, title, message) {
            window.videoResolving = false
            if (url && url.length > 0) {
                if (title && title.length > 0)
                    window.animeCurrentEpisode = title
                window.animeEpisodesOpen = false
                animeEpisodesWindow.close()
                window.animePlaybackActive = true
                sidebar.activePageOverride = 9
                sidebar.currentIndex = 1
                window.videoBrowserActive = false
                window.bottomControlReveal = false
                playerController.playVodUrl(url)
            } else {
                playerController.logShortVideoDebug("anime episode resolve failed: " + message)
            }
        }
    }

    Timer {
        id: shortVideoPrefetchRetryTimer
        interval: 2000
        repeat: false
        onTriggered: window.refillShortVideoPrefetchPool()
    }
    Timer {
        id: shortVideoFailoverTimer
        interval: 500
        repeat: false
        onTriggered: window.playNextShortVideoByMode()
    }

    Timer {
        id: shortVideoAutoAdvanceTimer
        interval: 700
        repeat: true
        running: window.isCurrentShortVideo()
            && sidebar.currentIndex === 6
        onRunningChanged: window.shortVideoDebug("autoTimer running=" + running)
        onTriggered: {
            window.shortVideoDebug("autoTimer triggered")
            if (Date.now() < window.shortVideoIgnoreEndedUntil) {
                window.shortVideoDebug("autoTimer ignored within ignore window remainMs=" + (window.shortVideoIgnoreEndedUntil - Date.now()))
                return
            }
            if (playerController.seeking) {
                window.shortVideoDebug("autoTimer ignored seeking")
                return
            }
            if (playerController.durationMs <= 0) {
                if (!playerController.isPlaying
                        && Date.now() - window.shortVideoLoadStartedAt > 1800
                        && window.shortVideoPlaybackMode !== "once"
                        && window.shortVideoLoadRetryCount < 3) {
                    window.shortVideoLoadRetryCount += 1
                    window.shortVideoAutoAdvanceGuard = true
                    window.shortVideoDebug("autoTimer retrying stalled short video retry=" + window.shortVideoLoadRetryCount)
                    window.playNextShortVideoByMode()
                    return
                }
                window.shortVideoDebug("autoTimer ignored invalid duration")
                return
            }
            if (playerController.positionMs > window.shortVideoLastPositionMs + 250) {
                window.shortVideoLastPositionMs = playerController.positionMs
                window.shortVideoLastProgressAt = Date.now()
                window.shortVideoNoProgressTicks = 0
            } else if (playerController.positionMs > 500
                    && playerController.positionMs < Math.max(0, playerController.durationMs - 1200)) {
                window.shortVideoNoProgressTicks += 1
            }
            if (!playerController.isPlaying
                    && !playerController.isPaused
                    && playerController.positionMs > 500
                    && playerController.positionMs < Math.max(0, playerController.durationMs - 1200)
                    && window.shortVideoNoProgressTicks >= 4) {
                window.shortVideoAutoAdvanceGuard = true
                window.shortVideoDebug("autoTimer advancing stalled midstream ticks=" + window.shortVideoNoProgressTicks)
                window.playNextShortVideoByMode()
                return
            }
            if (playerController.isPlaying && playerController.positionMs > 500) {
                window.shortVideoLoadRetryCount = 0
                // 视频真正播起来了: 失败熔断计数清零。
                window.shortVideoFailedSourceCount = 0
            }
            if (playerController.positionMs < Math.max(0, playerController.durationMs - 900)) {
                if (window.shortVideoAutoAdvanceGuard)
                    window.shortVideoDebug("autoTimer reset guard before end")
                window.shortVideoAutoAdvanceGuard = false
            }
            if (!window.shortVideoAutoAdvanceGuard
                    && playerController.positionMs >= Math.max(0, playerController.durationMs - 450)) {
                window.shortVideoAutoAdvanceGuard = true
                window.shortVideoDebug("autoTimer advancing near end")
                window.playNextShortVideoByMode()
            }
        }
    }

    Timer {
        id: mediaBrowserOpenCooldown
        interval: 450
        repeat: false
        onTriggered: window.mediaBrowserOpening = false
    }

    Timer {
        id: videoSurfaceRefreshTimer
        property int requestedTicks: 6
        property int remainingTicks: 0
        property string reason: ""
        interval: 120
        repeat: true
        onRunningChanged: if (running) remainingTicks = requestedTicks
        onTriggered: {
            window.refreshVideoSurfaceNow(reason)
            remainingTicks -= 1
            if (remainingTicks <= 0)
                stop()
        }
    }

    function raiseEpisodeOverlays() {
        // 选集/抽屉盖在手势层之上时点击会被 videoGestureWindow 抢走,
        // 每次重排层级后都要把它们重新抬回最上层。
        if (mediaDrawer.isOpen)
            mediaDrawerWindow.raise()
        if (animeEpisodesWindow.visible)
            animeEpisodesWindow.raise()
    }

    function revealPlayerControls() {
        if (!playerPageActive)
            return
        bottomControlReveal = true
        playerControlHideTimer.restart()
        if (!immersiveMode && normalControlWindow.visible)
            normalControlWindow.raise()
        if (immersiveMode && immersiveControlWindow.visible)
            immersiveControlWindow.raise()
        raiseEpisodeOverlays()
    }

    function schedulePlayerControlsHide() {
        if (!playerPageActive
                || controlBar.hovered
                || floatingControlBar.hovered
                || normalControlHotZoneMouse.containsMouse
                || immersiveHotZoneMouse.containsMouse
                || controlBar.seeking
                || floatingControlBar.seeking
                || playerController.seeking
                || controlBar.danmakuSettingsOpen
                || floatingControlBar.danmakuSettingsOpen
                || controlBar.skipSettingsOpen
                || floatingControlBar.skipSettingsOpen
                || controlBar.volumeOpen
                || floatingControlBar.volumeOpen) {
            return
        }
        playerControlHideTimer.restart()
    }

    function hidePlayerControlsImmediately() {
        playerControlHideTimer.stop()
        if (!playerPageActive
                || controlBar.hovered
                || floatingControlBar.hovered
                || normalControlHotZoneMouse.containsMouse
                || immersiveHotZoneMouse.containsMouse
                || controlBar.seeking
                || floatingControlBar.seeking
                || playerController.seeking
                || controlBar.danmakuSettingsOpen
                || floatingControlBar.danmakuSettingsOpen
                || controlBar.skipSettingsOpen
                || floatingControlBar.skipSettingsOpen
                || controlBar.volumeOpen
                || floatingControlBar.volumeOpen) {
            return
        }
        bottomControlReveal = false
        controlBar.closePopups()
        floatingControlBar.closePopups()
    }

    function togglePlayPause() {
        var willPause = playerController.isPlaying
        if (willPause)
            playerController.pause()
        else
            playerController.play()
        // 仅在点击后暂停时显示短暂反馈；点击暂停图标恢复播放时不显示播放图标。
        if (willPause) {
            playbackGestureFeedbackIcon = "\u23F8"
            playbackGestureFeedbackText = "暂停"
            playbackGestureFeedbackVisible = true
            playbackGestureFeedbackTimer.restart()
        } else {
            playbackGestureFeedbackTimer.stop()
            playbackGestureFeedbackVisible = false
        }
        Qt.callLater(function() { holdSpeedIndicatorWindow.raise() })
    }

    function beginHoldSpeed() {
        if (playerController.currentFile.length === 0)
            return
        playerController.beginHoldSpeed()
        playbackGestureFeedbackTimer.stop()
        playbackGestureFeedbackVisible = false
        holdSpeedIndicatorWindow.raise()
    }

    function endHoldSpeed() {
        playerController.endHoldSpeed()
    }

    function seekRelative(deltaMs) {
        playerController.seekRelative(deltaMs)
    }

    function formatSeekTime(ms) {
        if (!ms || ms < 0) return "00:00"
        var totalSec = Math.floor(ms / 1000)
        var h = Math.floor(totalSec / 3600)
        var mm = Math.floor((totalSec % 3600) / 60)
        var ss = totalSec % 60
        var m2 = (mm < 10 ? "0" + mm : "" + mm)
        var s2 = (ss < 10 ? "0" + ss : "" + ss)
        return h > 0 ? h + ":" + m2 + ":" + s2 : m2 + ":" + s2
    }

    property bool volumeFeedbackVisible: false

    function adjustVolume(delta) {
        playerController.setVolume(Math.max(0.0, Math.min(1.0, playerController.volume + delta)))
        window.volumeFeedbackVisible = true
        volumeFeedbackTimer.restart()
    }

    function toggleFullScreen() {
        window.visibility = (window.visibility === Window.FullScreen) ? Window.Windowed : Window.FullScreen
        revealPlayerControls()
        mediaDrawer.close()
        logDrawer.close()
        controlBar.closePopups()
        floatingControlBar.closePopups()
        scheduleVideoSurfaceRefresh("fullscreen-toggle")
    }

    function setVideoFillMode(enabled) {
        if (videoFillMode === enabled) return
        videoFillMode = enabled
        revealPlayerControls()
        mediaDrawer.close()
        logDrawer.close()
        controlBar.closePopups()
        floatingControlBar.closePopups()
        scheduleVideoSurfaceRefresh("fill-mode-toggle")
    }

    Shortcut { sequence: "Space"; enabled: services.authController.isLoggedIn; onActivated: window.togglePlayPause() }
    Shortcut {
        sequence: "Left"
        enabled: services.authController.isLoggedIn
        onActivated: {
            window.seekRelative(-keyboardSeekStepMs)
        }
    }
    Shortcut {
        sequence: "Right"
        enabled: services.authController.isLoggedIn
        onActivated: {
            window.seekRelative(keyboardSeekStepMs)
        }
    }
    Shortcut { sequence: "Up"; enabled: services.authController.isLoggedIn; onActivated: window.adjustVolume(0.05) }
    Shortcut { sequence: "Down"; enabled: services.authController.isLoggedIn; onActivated: window.adjustVolume(-0.05) }
    Shortcut { sequence: "["; enabled: services.authController.isLoggedIn; onActivated: window.adjustVolume(-0.05) }
    Shortcut { sequence: "]"; enabled: services.authController.isLoggedIn; onActivated: window.adjustVolume(0.05) }
    Shortcut { sequence: "F"; enabled: services.authController.isLoggedIn; onActivated: window.toggleFullScreen() }
    Shortcut {
        sequence: "Escape"
        enabled: services.authController.isLoggedIn
        onActivated: {
            if (window.visibility === Window.FullScreen) {
                window.toggleFullScreen()
            } else if (window.videoFillMode) {
                window.setVideoFillMode(false)
            }
        }
    }
    Shortcut { sequence: "Ctrl+U"; enabled: services.authController.isLoggedIn; onActivated: urlDialog.open() }
    Shortcut { sequence: "A"; enabled: services.authController.isLoggedIn; onActivated: playerController.cycleAspectRatio() }
    Shortcut {
        sequence: "PageUp"
        enabled: services.authController.isLoggedIn
        onActivated: window.playPreviousEpisode()
    }
    Shortcut {
        sequence: "PageDown"
        enabled: services.authController.isLoggedIn
        onActivated: window.playNextEpisode()
    }
    Shortcut {
        sequence: "Ctrl+Left"
        enabled: services.authController.isLoggedIn
        onActivated: window.playPreviousEpisode()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        enabled: services.authController.isLoggedIn
        onActivated: window.playNextEpisode()
    }
    Shortcut {
        sequence: "Shift+P"
        enabled: services.authController.isLoggedIn
        onActivated: window.playPreviousEpisode()
    }
    Shortcut {
        sequence: "Shift+N"
        enabled: services.authController.isLoggedIn
        onActivated: window.playNextEpisode()
    }

    Timer {
        id: seekDebounceTimer
        interval: 300
        repeat: false
        onTriggered: {
            var target = Math.max(0, Math.min(playerController.durationMs, playerController.positionMs + seekAccum))
            seekAccum = 0
            playerController.seek(target)
        }
    }

    Timer {
        id: playerControlHideTimer
        interval: 3000
        repeat: false
        onTriggered: {
            if (window.playerPageActive
                    && !controlBar.hovered
                    && !floatingControlBar.hovered
                    && !normalControlHotZoneMouse.containsMouse
                    && !immersiveHotZoneMouse.containsMouse
                    && !controlBar.seeking
                    && !floatingControlBar.seeking
                    && !playerController.seeking
                    && !controlBar.danmakuSettingsOpen
                    && !floatingControlBar.danmakuSettingsOpen
                    && !controlBar.skipSettingsOpen
                    && !floatingControlBar.skipSettingsOpen
                    && !controlBar.volumeOpen
                    && !floatingControlBar.volumeOpen) {
                window.bottomControlReveal = false
            }
        }
    }

    Theme {
        id: appTheme
    }


    // Alias for child components to access
    property alias navSidebar: sidebar

    function syncPageState(index) {
        if (index !== 6 && (isCurrentShortVideo()
                || playerController.currentShortVideoUrl.length > 0)) {
            shortVideoFailoverTimer.stop()
            playerController.stopShortVideo()
        }
        var playerVisible = (index === 1 && !videoBrowserVisible)
            || (index === 6 && shortVideoPlaybackActive)
        playerController.setMpvSurfaceVisible(playerVisible)
        if (index === 4) {
            Qt.callLater(function() {
                imagePage.ensureInitialImage()
            })
        }
        if (index === 6 && playerController.currentShortVideoUrl.length === 0) {
            Qt.callLater(function() {
                rebuildShortVideoSites()
                if (selectedShortVideoSiteIndex >= 0)
                    playShortVideoSite(selectedShortVideoSiteIndex)
            })
        }
        if (playerVisible) {
            if (index === 1)
                revealPlayerControls()
            scheduleVideoSurfaceRefresh("page-enter")
        } else {
            if (playerController.isPlaying)
                playerController.pause()
            shortVideoAutoAdvanceGuard = false
            playerControlHideTimer.stop()
            videoFillMode = false
            bottomControlReveal = false
            mediaDrawer.close()
            logDrawer.close()
            controlBar.closePopups()
            floatingControlBar.closePopups()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: appTheme.windowColor
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Left sidebar navigation
        NavigationSidebar {
            id: sidebar
            Layout.fillHeight: true
            // 鐢婚潰閾烘弧鍙奖鍝嶆挱鏀惧櫒鍐呭锛屼笉搴斿悶鎺夊簲鐢ㄥ鑸€?            // 宸︿晶鑿滃崟浠呭湪鐪熸鐨勭郴缁熷叏灞忕姸鎬佷笅闅愯棌銆?
            Layout.preferredWidth: window.visibility === Window.FullScreen ? 0 : 64
            visible: window.visibility !== Window.FullScreen
            theme: appTheme
            unreadBadge: services.poolChat ? services.poolChat.unreadTotal : 0
            updateAvailable: services.updateController ? services.updateController.updateAvailable : false
            onCurrentIndexChanged: window.saveUiSetting("currentPage", currentIndex)
            onLogoClicked: userProfileDialog.open()
            Component.onCompleted: window.restoreUiSettings()
        }

        // Main content area
        StackLayout {
            id: mainStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: sidebar.currentIndex
            onCurrentIndexChanged: window.syncPageState(currentIndex)
            Component.onCompleted: window.syncPageState(currentIndex)

            // Page 0: Home
            HomeView {
                id: homePage
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: playerController
                theme: appTheme
                onNavigateRequested: function(pageIndex) { sidebar.currentIndex = pageIndex }
            }

            // Page 1: Player
            VideoPlayerView {
                id: playerPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                hostWindow: window
                hostContentItem: window.contentItem
                controller: playerController
                theme: appTheme
                sidebar: sidebar
                browserVisible: window.videoBrowserVisible
                immersiveMode: window.immersiveMode
                pageActive: sidebar.currentIndex === 1
                controlBarReservedHeight: 0
                onTogglePlayPauseRequested: window.togglePlayPause()
                onFocusHostRequested: window.contentItem.forceActiveFocus()
                onRevealControlsRequested: window.revealPlayerControls()
                onHideControlsRequested: window.hidePlayerControlsImmediately()
                onBeginHoldSpeedRequested: window.beginHoldSpeed()
                onEndHoldSpeedRequested: window.endHoldSpeed()
                // 滚轮不再调音量，改用键盘 [ / ] 和方向键 Up/Down 调整。
                onPlaybackRequested: {
                    // 影视开播：清掉可能残留的动漫播放标记，否则动漫弹幕会盖在影视画面上
                    window.videoResolving = false
                    window.animePlaybackActive = false
                    window.videoBrowserActive = false
                    window.bottomControlReveal = false
                }
                onReturnToDetailRequested: {
                    window.videoResolving = false
                    playerController.stop()
                    if (window.visibility === Window.FullScreen)
                        window.visibility = Window.Windowed
                    window.videoBrowserActive = true
                    playerPage.showDetail()
                }
            }

            // Page 2: Playback history
            HistoryView {
                id: historyPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: playerController
                theme: appTheme
                onNavigateRequested: function(pageIndex) { sidebar.currentIndex = pageIndex }
            }
            // Page 3: Update
            UpdateView {
                id: updatePage
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: services.updateController
                theme: appTheme
            }
            // Page 4: Images
            ImageView {
                id: imagePage
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: playerController
                theme: appTheme
                strings: appText
                active: sidebar.currentIndex === 4
                hostWidth: window.width
                hostHeight: window.height
                onSaveUiSettingRequested: function(key, value) { window.saveUiSetting(key, value) }
                onNavigateRequested: function(pageIndex) { sidebar.currentIndex = pageIndex }
            }
// Page 5: Memes
            MemeView {
                id: memePage
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: playerController
                theme: appTheme
                strings: appText
            }
            // Page 6: Short videos
            ShortVideoView {
                id: shortVideoPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: playerController
                theme: appTheme
                strings: appText
                siteModel: shortVideoSiteList
                hostContentItem: window.contentItem
                playbackActive: window.shortVideoPlaybackActive
                selectedSiteIndex: window.selectedShortVideoSiteIndex
                onPlayRequested: function(siteIndex) { window.playShortVideoSite(siteIndex) }
                onSiteSelected: function(siteIndex) { window.selectedShortVideoSiteIndex = siteIndex }
                onTogglePlayPauseRequested: window.togglePlayPause()
                onFocusHostRequested: window.contentItem.forceActiveFocus()
                onRevealControlsRequested: window.revealPlayerControls()
                onHideControlsRequested: window.hidePlayerControlsImmediately()
                onBeginHoldSpeedRequested: window.beginHoldSpeed()
                onEndHoldSpeedRequested: window.endHoldSpeed()
                onWheelScrolled: function(delta) { window.handleShortVideoWheel(delta) }
            }
// Page 7: Voice
            VoiceView {
                id: voicePage
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: playerController
                theme: appTheme
                strings: appText
                onSaveUiSettingRequested: function(key, value) { window.saveUiSetting(key, value) }
            }
            // Page 8（站点管理）已删除：站点由 Pool 统一下发，无需本地管理。
            // StackLayout 按位置索引，保留空占位避免后续页面错位。
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            // Page 9: Anime
            AnimeView {
                id: animePage
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: playerController
                theme: appTheme
                sidebar: sidebar
                onEpisodeRequested: function(part) {
                    window.videoResolving = true
                    window.animeCurrentEpisode = part || ""
                    window.animeEpisodesOpen = false
                    animeEpisodesWindow.close()
                    window.animePlaybackVodId = animePage.model.detail.vodId || animePage.selectedVodId
                    window.setAnimePlaybackPlaylist(window.animePlaybackVodId, part)
                    window.animeCommentsDrawerOpen = false
                    window.animePlaybackActive = true
                    sidebar.activePageOverride = 9
                    sidebar.currentIndex = 1
                    window.videoBrowserActive = false
                    window.bottomControlReveal = false
                    playerController.stop()
                    // 首次点集走此路径(不经 prepareAnimePlayback),需在此预取站内弹幕。
                    // 与 AnimeView 内的 playEpisode 并行;danmakuSerial_ 独立,互不干扰。
                    if (window.animePlaybackVodId > 0 && String(part || "").length > 0)
                        services.dmghgAnimeModel.loadDanmaku(window.animePlaybackVodId, part)
                }
            }

            MyView {
                id: myPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: appTheme
                poolMe: services.poolMe
                poolChat: services.poolChat
                // 聊天轮询的唯一开关点在 MyView.syncPolling：这里只把「停在不在这一页」
                // 这个条件交给它，离开第 10 页 (我的) 定时器就停。
                pageShown: mainStack.currentIndex === 10
                onOpenRequested: function(vodKey, name) {
                    // 服务端存的键是上游编号，新版是 18 位数字，装不进 int，
                    // 全程按字符串传给模型，由客户端自己换算成会话句柄。
                    if (!vodKey)
                        return
                    // 侧栏按 activePage 取页，播放动漫时 activePageOverride 会被占住；
                    // 不清零只改 currentIndex 的话，点历史条目看起来就是"没反应"。
                    sidebar.activePageOverride = -1
                    sidebar.currentIndex = 9
                    animePage.selectedVodId = 0
                    animePage.detailActive = true
                    services.dmghgAnimeModel.loadDetailByKey(vodKey)
                }
            }
        }
    }

    // 顶部"搜索热区"窗口已停用：它在标题栏下方放半透明白条，悬停后跳搜索页，体验突兀。
    Item {
        id: playerSearchHotZoneWindow
        width: 0
        height: 0
        visible: false

        Timer {
            id: playerSearchHotZoneDelay
            interval: 220
            repeat: false
            onTriggered: {
                if (playerSearchHotZoneMouse.containsMouse) {
                    playerSearchHotZoneWindow.raise()
                    window.openMediaBrowser("video")
                }
            }
        }

    }

    MediaBrowserWindow {
        id: mediaBrowserWindow
        hostWindow: window
        controller: playerController
        theme: appTheme
        strings: appText
        sidebar: sidebar
        shortVideoSiteModel: shortVideoSiteList
        shortVideoPlaybackMode: window.shortVideoPlaybackMode
        selectedShortVideoSiteIndex: window.selectedShortVideoSiteIndex
        onRebuildShortVideoSitesRequested: window.rebuildShortVideoSites()
        onShortVideoPlaybackModeRequested: function(mode) { window.shortVideoPlaybackMode = mode }
        onPlayShortVideoRequested: function(sourceIndex) { window.playShortVideoSite(sourceIndex) }
    }

    Window {
        id: videoGestureWindow
        property bool holding: false
        width: Math.max(1, window.width - (sidebar.visible ? sidebar.width : 0)
            - (window.immersiveMode ? 0 : appTheme.edgePadding * 2))
        height: Math.max(1, window.height - (window.immersiveMode ? 0 : appTheme.edgePadding * 2))
        x: window.x + (sidebar.visible ? sidebar.width : 0)
            + (window.immersiveMode ? 0 : appTheme.edgePadding)
        y: window.y + (window.immersiveMode ? 0 : appTheme.edgePadding)
        // 仅视频页(1)使用本手势窗；短视频页(6)的滚轮/滑动/点击由
        // ShortVideoOverlay 窗口负责。此前 playerPageActive 把页 6 也算进来,
        // 且 refreshVideoSurfaceNow 每次都会 raise 本窗, 把短视频覆盖窗压在
        // 下面——滚轮切换等覆盖窗手势全部被这里拦死, 看似"功能丢失"。
        visible: window.visible && sidebar.currentIndex === 1
            && !window.videoBrowserVisible && playerController.currentFile.length > 0
        flags: Qt.Tool | Qt.FramelessWindowHint
        transientParent: window
        color: "transparent"
        onVisibleChanged: {
            if (visible) {
                raise()
                Qt.callLater(playerPage.raisePlaybackOverlays)
            } else if (holding) {
                holding = false
                window.endHoldSpeed()
            }
        }

        MouseArea {
            id: videoGestureMouse
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            enabled: !mediaDrawer.isOpen && !window.animeEpisodesOpen
            hoverEnabled: true
            pressAndHoldInterval: 350
            // 单击切换暂停；pressAndHold 已单独走长按倍速逻辑
            onClicked: if (!videoGestureWindow.holding) window.togglePlayPause()
            onPressAndHold: {
                videoGestureWindow.holding = true
                window.beginHoldSpeed()
            }
            onReleased: {
                if (videoGestureWindow.holding) {
                    videoGestureWindow.holding = false
                    window.endHoldSpeed()
                }
            }
            onCanceled: {
                if (videoGestureWindow.holding) {
                    videoGestureWindow.holding = false
                    window.endHoldSpeed()
                }
            }
            onPositionChanged: function(mouse) {
                if (mouse.y >= Math.max(0, height - window.playerControlHotZoneHeight)) {
                    window.revealPlayerControls()
                    if (normalControlWindow.visible) normalControlWindow.raise()
                    if (immersiveControlWindow.visible) immersiveControlWindow.raise()
                } else {
                    window.hidePlayerControlsImmediately()
                }
            }
            // 滚轮不再调音量，改用键盘 [ / ] 和方向键 Up/Down 调整。
        }

        // 弹幕画在 videoGestureWindow 内、z 低于其 MouseArea,
        // 画在视频上方但不抢手势;控制条窗口 raise() 在其上,自然遮住底部弹幕。
        DanmakuOverlay {
            id: animeDanmakuOverlay
            anchors.fill: parent
            z: -1
            // 动漫走 dmghg 模型；dongpian 系影视走 PlayerController 拉取的 Pool 弹幕
            danmakuEnabled: (window.animePlaybackActive || playerController.videoDanmaku.length > 0)
                && window.danmakuEnabled
                && playerController.currentFile.length > 0
            danmakuOpacity: window.danmakuOpacity
            danmaku: window.animePlaybackActive
                ? services.dmghgAnimeModel.danmaku
                : playerController.videoDanmaku
            positionMs: playerController.positionMs
            isPlaying: playerController.isPlaying
            seeking: playerController.seeking
            displayMode: window.danmakuDisplayMode
            fontScale: window.danmakuFontScale
        }

        Rectangle {
            id: videoDetailBackButton
            width: 92
            height: 36
            x: 12
            y: 12
            z: 10
            visible: playerPage.detailActive && window.visibility !== Window.FullScreen
            radius: 6
            color: videoDetailBackMouse.containsMouse
                ? appTheme.panelRaisedColor : appTheme.panelColor
            border.color: videoDetailBackMouse.containsMouse
                ? appTheme.accentColor : appTheme.borderColor
            border.width: 1
            opacity: 0.94

            Text {
                anchors.centerIn: parent
                text: "\u8fd4\u56de\u8be6\u60c5"
                color: videoDetailBackMouse.containsMouse
                    ? appTheme.accentColor : appTheme.textSecondaryColor
                font.pixelSize: 12
                font.family: appTheme.fontFamily
            }

            MouseArea {
                id: videoDetailBackMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: playerPage.returnToDetailRequested()
            }
        }

        Rectangle {
            id: animeDetailBackButton
            width: 92
            height: 36
            x: 12
            y: 12
            z: 10
            visible: window.animePlaybackActive && window.visibility !== Window.FullScreen
            radius: 6
            color: animeDetailBackMouse.containsMouse
                ? appTheme.panelRaisedColor : appTheme.panelColor
            border.color: animeDetailBackMouse.containsMouse
                ? appTheme.accentColor : appTheme.borderColor
            border.width: 1
            opacity: 0.94

            Text {
                anchors.centerIn: parent
                text: "返回详情"
                color: animeDetailBackMouse.containsMouse
                    ? appTheme.accentColor : appTheme.textSecondaryColor
                font.pixelSize: 12
                font.family: appTheme.fontFamily
            }

            MouseArea {
                id: animeDetailBackMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    playerController.pause()
                    window.animePlaybackActive = false
                    window.animeCommentsDrawerOpen = false
                    window.animeEpisodesOpen = false
                    animeEpisodesWindow.close()
                    sidebar.activePageOverride = -1
                    window.videoBrowserActive = true
                    sidebar.currentIndex = 9
                }
            }
        }

        Rectangle {
            id: animeEpisodesButton
            width: 72
            height: 36
            x: animeDetailBackButton.x + animeDetailBackButton.width + 8
            y: 12
            z: 10
            // 选集统一放到播放控制栏的“选集”按钮中。
            visible: false
            radius: 6
            color: animeEpisodesMouse.containsMouse
                ? appTheme.panelRaisedColor : appTheme.panelColor
            border.color: animeEpisodesMouse.containsMouse
                ? appTheme.accentColor : appTheme.borderColor
            border.width: 1
            opacity: 0.94

            Text {
                anchors.centerIn: parent
                text: "选集"
                color: animeEpisodesMouse.containsMouse
                    ? appTheme.accentColor : appTheme.textSecondaryColor
                font.pixelSize: 12
                font.family: appTheme.fontFamily
            }

            MouseArea {
                id: animeEpisodesMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    window.animeEpisodesOpen = true
                    animeEpisodesWindow.show()
                    animeEpisodesWindow.raise()
                    animeEpisodesWindow.requestActivate()
                }
            }
        }
    }

    property bool seekFeedbackVisible: false
    property real seekFeedbackDelta: 0
    property real seekFeedbackTargetMs: 0
    property real seekFeedbackDurationMs: 0

    Timer {
        id: seekFeedbackHideTimer
        interval: 1000
        repeat: false
        onTriggered: window.seekFeedbackVisible = false
    }

    Timer {
        id: playbackGestureFeedbackTimer
        interval: 700
        repeat: false
        onTriggered: window.playbackGestureFeedbackVisible = false
    }

    // Top-level feedback remains visible above the native mpv video window.
    Window {
        id: holdSpeedIndicatorWindow
        width: (window.holdSpeedActive || playerController.holdSpeedActive) ? 148
             : (window.seekFeedbackVisible ? 180 : 72)
        height: (window.holdSpeedActive || playerController.holdSpeedActive) ? 84
              : (window.seekFeedbackVisible ? 72 : 64)
        x: window.x + (sidebar.visible ? sidebar.width : 0)
            + (window.width - (sidebar.visible ? sidebar.width : 0) - width) / 2
        y: window.y + (window.height - height) / 2
        // 仅在点击反馈、长按倍速、快进快退或鼠标停留在反馈按钮上时可见。
        visible: window.visible && window.playerPageActive
            && !window.videoBrowserVisible && playerController.currentFile.length > 0
            && (window.holdSpeedActive || playerController.holdSpeedActive
                || window.seekFeedbackVisible
                || window.playbackGestureFeedbackVisible)
        // 暂停/播放提示需要能被点击，因此不能使用 WindowTransparentForInput。
        flags: Qt.Tool | Qt.FramelessWindowHint
        transientParent: window
        color: "transparent"
        onVisibleChanged: if (visible) raise()

        Rectangle {
            id: playbackFeedbackBackground
            anchors.fill: parent
            radius: 12
            color: (window.holdSpeedActive || playerController.holdSpeedActive || window.seekFeedbackVisible)
                ? "#e60f172a"
                : (playbackFeedbackMouse.containsMouse ? "#4dffffff" : "transparent")
            border.color: (window.holdSpeedActive || playerController.holdSpeedActive || window.seekFeedbackVisible)
                ? "#3338bdf8"
                : "transparent"
            border.width: (window.holdSpeedActive || playerController.holdSpeedActive || window.seekFeedbackVisible) ? 1.5 : 0

            MouseArea {
                id: playbackFeedbackMouse
                anchors.fill: parent
                enabled: !window.holdSpeedActive && !playerController.holdSpeedActive && !window.seekFeedbackVisible
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: window.togglePlayPause()
            }

            // 1. Hold Speed Feedback (长按加速)
            Column {
                anchors.centerIn: parent
                spacing: 3
                visible: window.holdSpeedActive || playerController.holdSpeedActive

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "3.0×"
                    color: appTheme.accentColor || "#38bdf8"
                    font.family: appTheme.fontFamily
                    font.pixelSize: 28
                    font.bold: true
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "倍速播放中 >>>"
                    color: "#f1f5f9"
                    font.family: appTheme.fontFamily
                    font.pixelSize: 12
                    font.bold: true
                }
            }

            // 2. Seek Feedback (快进 / 快退)
            Column {
                anchors.centerIn: parent
                spacing: 4
                visible: !window.holdSpeedActive && !playerController.holdSpeedActive && window.seekFeedbackVisible

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 6

                    Text {
                        text: window.seekFeedbackDelta > 0 ? "▶▶" : "◀◀"
                        color: appTheme.accentColor || "#38bdf8"
                        font.pixelSize: 16
                        font.bold: true
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: (window.seekFeedbackDelta > 0 ? "快进 +" : "快退 -")
                            + Math.round(Math.abs(window.seekFeedbackDelta) / 1000) + "秒"
                        color: "#ffffff"
                        font.family: appTheme.fontFamily
                        font.pixelSize: 15
                        font.bold: true
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: window.formatSeekTime(window.seekFeedbackTargetMs) + " / " + window.formatSeekTime(window.seekFeedbackDurationMs)
                    color: "#94a3b8"
                    font.family: appTheme.fontFamily
                    font.pixelSize: 11
                }
            }

            // 3. Play/Pause feedback
            Column {
                anchors.centerIn: parent
                spacing: 5
                visible: !window.holdSpeedActive && !playerController.holdSpeedActive && !window.seekFeedbackVisible && window.playbackGestureFeedbackVisible

                Item {
                    width: 28
                    height: 34
                    anchors.horizontalCenter: parent.horizontalCenter

                    Rectangle {
                        x: 3
                        y: 2
                        width: 8
                        height: parent.height - 4
                        radius: 1
                        color: "#e6ffffff"
                    }

                    Rectangle {
                        x: 17
                        y: 2
                        width: 8
                        height: parent.height - 4
                        radius: 1
                        color: "#e6ffffff"
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: window.playbackGestureFeedbackText
                    color: "#d9ffffff"
                    font.family: appTheme.fontFamily
                    font.pixelSize: 12
                }
            }
        }
    }

    property real volumeFeedbackVolume: -1

    Connections {
        target: playerController
        function onVolumeChanged() {
            window.volumeFeedbackVolume = playerController.volume
            window.volumeFeedbackVisible = true
            volumeFeedbackTimer.restart()
        }
        function onSeekFeedbackRequested(deltaMs, targetMs, durationMs) {
            window.seekFeedbackDelta = deltaMs
            window.seekFeedbackTargetMs = targetMs
            window.seekFeedbackDurationMs = durationMs
            window.seekFeedbackVisible = true
            seekFeedbackHideTimer.restart()
            holdSpeedIndicatorWindow.raise()
        }
        function onHoldSpeedActiveChanged() {
            if (playerController.holdSpeedActive) {
                holdSpeedIndicatorWindow.raise()
            }
        }
    }

    Timer {
        id: volumeFeedbackTimer
        interval: 1200
        repeat: false
        onTriggered: window.volumeFeedbackVisible = false
    }

    // 键盘调音量时的右上角半透明提示，档次与倍速/播放提示一致但不拦截输入。
    Window {
        id: volumeFeedbackWindow
        width: 168
        height: 44
        x: window.x + window.width - width - (window.immersiveMode ? 12 : appTheme.edgePadding + 4)
        y: window.y + (window.immersiveMode ? 12 : appTheme.edgePadding + 4)
        // 不限于播放页：只要按了音量键就显示
        visible: window.visible && window.volumeFeedbackVisible
            && window.volumeFeedbackVolume >= 0
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowTransparentForInput
        transientParent: window
        color: "transparent"
        onVisibleChanged: if (visible) raise()

        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "#d9191b1f"
            border.color: "#55ffffff"
            border.width: 1

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    // 音量图标随静音/有声音切换
                    text: playerController.volume <= 0 ? "\uE74F" : "\uE767"
                    color: "#ffffff"
                    font.family: appTheme.iconFontFamily
                    font.pixelSize: 16
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 72
                    height: 4
                    radius: 2
                    color: "#33ffffff"

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, playerController.volume))
                        height: parent.height
                        radius: 2
                        color: "#ffffff"
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Math.round(Math.max(0, Math.min(1, playerController.volume)) * 100) + "%"
                    color: "#d9ffffff"
                    font.family: appTheme.fontFamily
                    font.pixelSize: 11
                }
            }
        }
    }

    // VC++ 运行库缺失提示: libmpv 加载失败且回退 FFmpeg 管线时弹出,
    // 支持一键下载官方安装包并静默安装 (UAC 提权), 装完无需重启应用。
    Dialog {
        id: vcRuntimeDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, window.width - 48)
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 16
        property bool installRunning: false
        background: Rectangle {
            radius: appTheme.panelRadius
            color: appTheme.panelRaisedColor
            border.color: appTheme.borderColor
            border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: "缺少 VC++ 运行库"
                color: appTheme.textPrimaryColor
                font.family: appTheme.fontFamily
                font.pixelSize: appTheme.sectionTitleSize
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: "未检测到 Microsoft VC++ 2015-2022 运行库，高性能播放器 (mpv) 无法加载，已回退到兼容模式播放。是否自动下载并安装官方运行库？"
                color: appTheme.textSecondaryColor
                font.family: appTheme.fontFamily
                font.pixelSize: appTheme.bodySize
                wrapMode: Text.WordWrap
            }
            Text {
                id: vcInstallStatusLabel
                Layout.fillWidth: true
                visible: text.length > 0
                text: ""
                color: appTheme.accentColor
                font.family: appTheme.fontFamily
                font.pixelSize: appTheme.captionSize
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Button {
                    id: vcInstallButton
                    text: vcRuntimeDialog.installRunning ? "安装中..." : "立即安装"
                    enabled: !vcRuntimeDialog.installRunning
                    onClicked: {
                        vcRuntimeDialog.installRunning = true
                        vcInstallStatusLabel.text = ""
                        playerController.installVcRuntime()
                    }
                }
                Button {
                    text: "使用本地安装包"
                    enabled: !vcRuntimeDialog.installRunning
                    onClicked: playerController.installVcRuntimeFromFile()
                }
                Button {
                    text: vcRuntimeDialog.installRunning ? "后台运行" : "稍后"
                    onClicked: vcRuntimeDialog.close()
                }
            }
        }
    }

    Connections {
        target: playerController
        function onVcRuntimeMissing() {
            if (!vcRuntimeDialog.visible)
                vcRuntimeDialog.open()
        }
        function onVcInstallStateChanged(message) {
            vcInstallStatusLabel.text = message
            if (message.indexOf("完成") >= 0 || message.indexOf("失败") >= 0
                    || message.indexOf("超时") >= 0 || message.indexOf("无法") >= 0
                    || message.indexOf("已安装") >= 0)
                vcRuntimeDialog.installRunning = false
        }
    }

    ShortVideoOverlay {
        id: shortVideoOverlayWindow
        hostWindow: window
        controller: playerController
        theme: appTheme
        pageActive: sidebar.currentIndex === 6
        playbackActive: window.shortVideoPlaybackActive
        sidebarVisible: sidebar.visible
        sidebarWidth: sidebar.width
        sourceTitle: window.currentShortVideoSourceTitle()
        errorMessage: window.shortVideoErrorMessage
        prefetchCount: shortVideoPrefetchPool.count
        onTogglePlayPauseRequested: window.togglePlayPause()
        onWheelScrolled: function(delta) { window.handleShortVideoWheel(delta) }
        onNextRequested: window.playNextShortVideoByMode()
        onRetryRequested: window.retryShortVideo()
    }

    Window {
        id: normalControlWindow
        width: Math.max(1, window.width - (sidebar.visible ? sidebar.width : 0))
        height: window.playerControlHotZoneHeight
        x: window.x + (sidebar.visible ? sidebar.width : 0)
        y: window.y + window.height - height
        visible: window.visible && sidebar.currentIndex === 1 && !window.immersiveMode
            && !window.videoBrowserVisible
        flags: Qt.Tool | Qt.FramelessWindowHint
        transientParent: window
        color: "transparent"
        onVisibleChanged: {
            if (visible)
                raise()
            else
                window.bottomControlReveal = false
        }

        MouseArea {
            id: normalControlHotZoneMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            onEntered: {
                window.revealPlayerControls()
                normalControlWindow.raise()
            }
            onExited: window.hidePlayerControlsImmediately()
        }

        PlaybackControlBar {
            id: controlBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: appTheme.edgePadding
            anchors.rightMargin: appTheme.edgePadding
            anchors.bottomMargin: 0
            height: parent.height
            visible: window.playerControlsVisible
            opacity: visible ? 1.0 : 0.0
            z: 2
            theme: appTheme
            positionText: controlBar.msToClock(controlBar.seeking ? controlBar.seekTarget : (window.qtNetworkMode ? qtPlayer.position : playerController.positionMs))
            durationText: controlBar.msToClock(playerController.durationMs)
            progressValue: playerController.positionMs
            progressTo: Math.max(playerController.durationMs, 1)
            bufferProgress: playerController.bufferProgress
            backendSeeking: playerController.seeking
            volumeValue: playerController.volume * 100
            emptyState: playerController.currentFile.length === 0
            isPlaying: playerController.isPlaying
            decodedFrames: playerController.totalFrames
            playbackRate: playerController.playbackRate
            repeatMode: playerController.repeatMode
            fullScreenMode: window.visibility === Window.FullScreen
            drawerOpen: mediaDrawer.isOpen && mediaDrawer.drawerMode === "info"
            playlistOpen: mediaDrawer.isOpen && mediaDrawer.drawerMode === "playlist"
            commentsAvailable: window.animePlaybackActive
            commentsOpen: window.animeCommentsDrawerOpen
            danmakuAvailable: window.animePlaybackActive || sidebar.currentIndex === 1 || playerController.videoDanmaku.length > 0
            danmakuCount: window.animePlaybackActive
                ? services.dmghgAnimeModel.danmaku.length
                : playerController.videoDanmaku.length
            danmakuEnabled: window.danmakuEnabled
            danmakuOpacityValue: window.danmakuOpacity
            danmakuDisplayModeValue: window.danmakuDisplayMode
            danmakuFontScaleValue: window.danmakuFontScale
            canPlayPrevious: playerController.canPlayPrevious
            canPlayNext: playerController.canPlayNext
            previousEpisodeTitle: playerController.previousEpisodeTitle
            nextEpisodeTitle: playerController.nextEpisodeTitle
            onPreviousRequested: window.playPreviousEpisode()
            onNextRequested: window.playNextEpisode()
            onTogglePlayPause: window.togglePlayPause()
            onSeekRequested: function(value) { playerController.seek(value) }
            onVolumeRequested: function(value) { playerController.setVolume(value / 100.0) }
            onPlaylistRequested: {
                logDrawer.close()
                if (mediaDrawer.isOpen && mediaDrawer.drawerMode === "playlist") {
                    mediaDrawer.close()
                } else {
                    mediaDrawer.drawerMode = "playlist"
                    mediaDrawer.open()
                    mediaDrawerWindow.raise()
                }
            }
            onCommentsRequested: window.toggleAnimeCommentsDrawer()
            onDanmakuRequested: window.danmakuEnabled = !window.danmakuEnabled
            onDanmakuSettingsRequested: controlBar.openDanmakuSettings(controlBar.mapToItem(null, 0, 0))
            onDanmakuOpacityRequested: function(value) { window.danmakuOpacity = value }
            onDanmakuDisplayModeRequested: function(mode) { window.danmakuDisplayMode = mode }
            onDanmakuFontScaleRequested: function(value) { window.danmakuFontScale = value }
            onDownloadRequested: {
                playerController.saveCurrentPlaylistM3u8()
            }
            onPlaybackRateChangeRequested: function(rate) { playerController.setPlaybackRate(rate) }
            onRepeatModeChangeRequested: function(mode) { playerController.setRepeatMode(mode) }
            skipIntroMs: playerController.skipIntroMs
            skipOutroMs: playerController.skipOutroMs
            onSkipIntroRequested: function(ms) { playerController.setSkipIntroMs(ms) }
            onSkipOutroRequested: function(ms) { playerController.setSkipOutroMs(ms) }
            onFullScreenToggleRequested: window.toggleFullScreen()
            thumbnailImage: playerController.thumbnailPreview
            thumbnailPos: playerController.thumbnailPosition
            thumbnailReady: playerController.thumbnailReady
            onThumbnailRequested: function(positionMs) { playerController.requestThumbnail(positionMs) }
            onHoveredChanged: {
                if (hovered) {
                    window.revealPlayerControls()
                } else {
                    window.hidePlayerControlsImmediately()
                }
            }
            onVisibleChanged: Qt.callLater(playerPage.syncMpvVideoGeometry)
        }
    }

    // Floating bottom controls for immersive/fullscreen mode. This is a top-level
    // transparent window so the native mpv video child window cannot cover it.
    Window {
        id: immersiveControlWindow
        width: Math.max(1, window.width)
        height: appTheme.controlBarHeight
        x: window.x
        y: window.y + window.height - height
        visible: window.visible && window.immersiveMode && !window.videoBrowserVisible
        flags: Qt.Tool | Qt.FramelessWindowHint
        transientParent: window
        color: "transparent"
        onVisibleChanged: {
            if (visible) {
                raise()
            } else {
                window.bottomControlReveal = false
            }
        }

        MouseArea {
            id: immersiveHotZoneMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            onEntered: {
                window.revealPlayerControls()
                immersiveControlWindow.raise()
            }
            onExited: window.hidePlayerControlsImmediately()
        }

        PlaybackControlBar {
            id: floatingControlBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 0
            anchors.rightMargin: 0
            anchors.bottomMargin: 0
            height: appTheme.controlBarHeight
            visible: window.playerControlsVisible
            opacity: visible ? 1.0 : 0.0
            z: 2
            theme: appTheme
            positionText: floatingControlBar.msToClock(floatingControlBar.seeking ? floatingControlBar.seekTarget : playerController.positionMs)
            durationText: floatingControlBar.msToClock(playerController.durationMs)
            progressValue: playerController.positionMs
            progressTo: Math.max(playerController.durationMs, 1)
            bufferProgress: playerController.bufferProgress
            backendSeeking: playerController.seeking
            volumeValue: playerController.volume * 100
            emptyState: playerController.currentFile.length === 0
            isPlaying: playerController.isPlaying
            decodedFrames: playerController.totalFrames
            playbackRate: playerController.playbackRate
            repeatMode: playerController.repeatMode
            fullScreenMode: window.visibility === Window.FullScreen
            drawerOpen: mediaDrawer.isOpen && mediaDrawer.drawerMode === "info"
            playlistOpen: mediaDrawer.isOpen && mediaDrawer.drawerMode === "playlist"
            commentsAvailable: window.animePlaybackActive
            commentsOpen: window.animeCommentsDrawerOpen
            danmakuAvailable: window.animePlaybackActive || sidebar.currentIndex === 1 || playerController.videoDanmaku.length > 0
            danmakuCount: window.animePlaybackActive
                ? services.dmghgAnimeModel.danmaku.length
                : playerController.videoDanmaku.length
            danmakuEnabled: window.danmakuEnabled
            danmakuOpacityValue: window.danmakuOpacity
            danmakuDisplayModeValue: window.danmakuDisplayMode
            danmakuFontScaleValue: window.danmakuFontScale
            canPlayPrevious: playerController.canPlayPrevious
            canPlayNext: playerController.canPlayNext
            previousEpisodeTitle: playerController.previousEpisodeTitle
            nextEpisodeTitle: playerController.nextEpisodeTitle
            onPreviousRequested: window.playPreviousEpisode()
            onNextRequested: window.playNextEpisode()
            onTogglePlayPause: window.togglePlayPause()
            onSeekRequested: function(value) { playerController.seek(value) }
            onVolumeRequested: function(value) { playerController.setVolume(value / 100.0) }
            onPlaylistRequested: {
                logDrawer.close()
                if (mediaDrawer.isOpen && mediaDrawer.drawerMode === "playlist") {
                    mediaDrawer.close()
                } else {
                    mediaDrawer.drawerMode = "playlist"
                    mediaDrawer.open()
                    mediaDrawerWindow.raise()
                }
            }
            onCommentsRequested: window.toggleAnimeCommentsDrawer()
            onDanmakuRequested: window.danmakuEnabled = !window.danmakuEnabled
            onDanmakuSettingsRequested: floatingControlBar.openDanmakuSettings(floatingControlBar.mapToItem(null, 0, 0))
            onDanmakuOpacityRequested: function(value) { window.danmakuOpacity = value }
            onDanmakuDisplayModeRequested: function(mode) { window.danmakuDisplayMode = mode }
            onDanmakuFontScaleRequested: function(value) { window.danmakuFontScale = value }
            onDownloadRequested: {
                playerController.saveCurrentPlaylistM3u8()
            }
            onPlaybackRateChangeRequested: function(rate) { playerController.setPlaybackRate(rate) }
            onRepeatModeChangeRequested: function(mode) { playerController.setRepeatMode(mode) }
            skipIntroMs: playerController.skipIntroMs
            skipOutroMs: playerController.skipOutroMs
            onSkipIntroRequested: function(ms) { playerController.setSkipIntroMs(ms) }
            onSkipOutroRequested: function(ms) { playerController.setSkipOutroMs(ms) }
            onFullScreenToggleRequested: window.toggleFullScreen()
            thumbnailImage: playerController.thumbnailPreview
            thumbnailPos: playerController.thumbnailPosition
            thumbnailReady: playerController.thumbnailReady
            onThumbnailRequested: function(positionMs) { playerController.requestThumbnail(positionMs) }
            onHoveredChanged: {
                if (hovered) {
                    window.revealPlayerControls()
                } else {
                    window.hidePlayerControlsImmediately()
                }
            }
        }
    }

    Window {
        id: animeCommentsDrawerWindow
        width: 380
        height: window.height - appTheme.edgePadding * 2 - appTheme.controlBarHeight
        x: window.x + window.width - width - appTheme.edgePadding
        y: window.y + appTheme.edgePadding
        visible: window.visible && window.animeCommentsDrawerOpen && window.animePlaybackActive
        flags: Qt.Tool | Qt.FramelessWindowHint
        transientParent: window
        color: "transparent"
        onVisibleChanged: if (visible) raise()

        Rectangle {
            anchors.fill: parent
            radius: 10
            color: appTheme.panelColor
            border.color: appTheme.borderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "评论"
                        color: appTheme.textPrimaryColor
                        font.family: appTheme.fontFamily
                        font.pixelSize: 17
                        font.bold: true
                    }
                    Text {
                        text: services.dmghgAnimeModel.commentsLoading
                            ? "加载中..." : (services.dmghgAnimeModel.comments.length + " 条")
                        color: appTheme.textMutedColor
                        font.family: appTheme.fontFamily
                        font.pixelSize: 11
                        Layout.leftMargin: 6
                    }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        contentItem: Text {
                            text: "\uE72C"
                            font.family: appTheme.iconFontFamily
                            font.pixelSize: 12
                            color: appTheme.textSecondaryColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "刷新评论"
                        onClicked: services.dmghgAnimeModel.loadComments(window.animePlaybackVodId, 1)
                    }
                    ToolButton {
                        contentItem: Text {
                            text: "\uE711"
                            font.family: appTheme.iconFontFamily
                            font.pixelSize: 12
                            color: appTheme.textSecondaryColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "关闭评论"
                        onClicked: {
                            window.animeCommentsDrawerOpen = false
                            animeCommentsDrawerWindow.close()
                        }
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: services.dmghgAnimeModel.comments
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: drawerCommentColumn.implicitHeight + 18
                        radius: 6
                        color: appTheme.surfaceColor
                        border.color: appTheme.subtleBorderColor
                        border.width: 1

                        ColumnLayout {
                            id: drawerCommentColumn
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: modelData.uname || "匿名用户"
                                    color: appTheme.accentColor
                                    font.family: appTheme.fontFamily
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                                Text {
                                    visible: modelData.isTop
                                    text: "置顶"
                                    color: appTheme.warningColor
                                    font.family: appTheme.fontFamily
                                    font.pixelSize: 10
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: modelData.createdAt || ""
                                    color: appTheme.textMutedColor
                                    font.family: appTheme.fontFamily
                                    font.pixelSize: 10
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.comments || ""
                                color: appTheme.textSecondaryColor
                                font.family: appTheme.fontFamily
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: !services.dmghgAnimeModel.commentsLoading
                        && services.dmghgAnimeModel.comments.length === 0
                    text: "暂无评论"
                    color: appTheme.textMutedColor
                    horizontalAlignment: Text.AlignHCenter
                    font.family: appTheme.fontFamily
                    font.pixelSize: 12
                }
            }
        }
    }

    // Media drawer (right side, top-level so mpv native video cannot cover it)
    Window {
        id: mediaDrawerWindow
        width: mediaDrawer.width
        height: window.height - appTheme.edgePadding * 2 - appTheme.gap - appTheme.controlBarHeight
        x: window.x + window.width - width
        y: window.y + appTheme.edgePadding
        visible: window.visible && mediaDrawer.isOpen
        flags: Qt.Tool | Qt.FramelessWindowHint
        transientParent: window
        color: "transparent"

        MediaDrawer {
            id: mediaDrawer
            height: parent.height
            theme: appTheme
            currentFileTitle: playerController.currentFile
            currentFileSubtitle: playerController.currentFile.length > 0 ? "Playing" : ""
            infoModel: playerController.mediaInfoModel
            emptyState: playerController.currentFile.length === 0
            playlistModel: playerController.playlistModel
            currentIndex: playerController.currentIndex
            apiSiteModel: services.apiSiteModel
            sourceSearchModel: services.videoCatalog.sourceSearchModel
            currentVodName: playerController.currentVodName
            audioTracks: playerController.audioTracks
            currentAudioTrack: playerController.currentAudioTrack
            subtitleTracks: playerController.subtitleTracks
            currentSubtitleTrack: playerController.currentSubtitleTrack
            subtitlesEnabled: playerController.subtitlesEnabled
            onPlaylistItemRequested: function(index, filePath, title) {
                if (window.animePlaybackActive && String(filePath || "").indexOf("dmghg://") === 0) {
                    window.prepareAnimePlayback(title, true)
                    return
                }
                playerController.playFromPlaylist(index)
            }
            onIsOpenChanged: if (isOpen) mediaDrawerWindow.raise()
        }
    }

    // Log drawer (right side, top-level so mpv native video cannot cover it)
    Window {
        id: logDrawerWindow
        width: logDrawer.width
        height: window.height - appTheme.edgePadding * 2 - appTheme.gap - appTheme.controlBarHeight
        x: window.x + window.width - width
        y: window.y + appTheme.edgePadding
        visible: window.visible && logDrawer.isOpen
        flags: Qt.Tool | Qt.FramelessWindowHint
        transientParent: window
        color: "transparent"

        LogDrawer {
            id: logDrawer
            height: parent.height
            theme: appTheme
            badgeText: "controller"
            statusText: playerController.isPlaying ? "Playing" : (playerController.isPaused ? "Paused" : "Idle")
            logModel: playerController.runtimeLogModel
            onIsOpenChanged: if (isOpen) logDrawerWindow.raise()
        }
    }

    // API manager dialog
    ApiManagerDialog {
        id: apiManagerDialog
        theme: appTheme
    }

    // User Profile dialog
    UserProfileDialog {
        id: userProfileDialog
        theme: appTheme
        authController: services.authController
    }

    // URL input dialog (Ctrl+U)
    Dialog {
        id: urlDialog
        title: "Open Network Stream"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            if (urlInput.text.length > 0) {
                playerController.openUrl(urlInput.text)
            }
        }
        onOpened: urlInput.forceActiveFocus()

        ColumnLayout {
            spacing: 8
            Label {
                text: "Enter stream URL (HTTP, RTSP, RTMP):"
                color: "#cccccc"
            }
            TextField {
                id: urlInput
                Layout.preferredWidth: 400
                placeholderText: "http://example.com/stream.mp4"
                color: "#ffffff"
                background: Rectangle {
                    color: "#2a2a2a"
                    radius: 4
                    border.color: urlInput.activeFocus ? "#4a90d9" : "#555555"
                }
                onAccepted: urlDialog.accept()
            }
        }
    }

    // Drag & drop files to open
    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]

        onEntered: function(drag) {
            dropOverlay.visible = true
        }
        onExited: {
            dropOverlay.visible = false
        }
        onDropped: function(drop) {
            dropOverlay.visible = false
            var urls = drop.urls
            if (urls.length === 0) return

            // Filter supported file extensions
            var supportedExts = [".mp4", ".mkv", ".mov", ".avi", ".webm", ".flv", ".ts", ".m3u8", ".m3u", ".mp3", ".wav", ".flac", ".aac", ".ogg"]
            var filePaths = []
            for (var i = 0; i < urls.length; i++) {
                var path = urls[i]
                // Remove file:/// prefix
                if (path.startsWith("file:///")) {
                    path = path.substring(8)
                } else if (path.startsWith("file://")) {
                    path = path.substring(7)
                }
                // On Windows, paths like /C:/ need leading slash removed
                if (Qt.platform.os === "windows" && path.length > 2 && path[0] === '/' && path[2] === ':') {
                    path = path.substring(1)
                }
                // Check extension
                var lowerPath = path.toLowerCase()
                var supported = false
                for (var j = 0; j < supportedExts.length; j++) {
                    if (lowerPath.endsWith(supportedExts[j])) {
                        supported = true
                        break
                    }
                }
                if (supported) {
                    filePaths.push(path)
                }
            }

            if (filePaths.length === 0) return

            if (filePaths.length === 1) {
                playerController.openFileAtPath(filePaths[0], true)
            } else {
                // Multiple files: add all to playlist, play first
                for (var k = 0; k < filePaths.length; k++) {
                    playerController.playlistModel.addFile(filePaths[k])
                }
                playerController.playFromPlaylist(0)
            }
        }
    }

    // Drop overlay indicator
    Rectangle {
        id: dropOverlay
        anchors.fill: parent
        color: "#80000000"
        visible: false
        z: 100

        Rectangle {
            anchors.centerIn: parent
            width: 280
            height: 120
            radius: 16
            color: "#cc1a1a1a"
            border.color: appTheme.accentColor
            border.width: 2

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    color: appTheme.accentColor
                    text: appText.playIcon
                    font.pixelSize: 32
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    color: appTheme.textPrimaryColor
                    text: "Drop files to play"
                    font.family: appTheme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                }
            }
        }
    }

    Window {
        id: animeEpisodesWindow
        width: Math.min(520, Math.max(340, window.width - 120))
        height: Math.min(560, Math.max(300, window.height - 160))
        x: window.x + (window.width - width) / 2
        y: window.y + (window.height - height) / 2
        // 选集统一由播放控制栏的“选集”按钮打开。
        visible: false
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        transientParent: window
        color: appTheme.windowColor
        onVisibleChanged: if (visible) { raise(); requestActivate() }

        Rectangle {
            anchors.fill: parent
            color: appTheme.panelColor
            border.color: appTheme.subtleBorderColor
            border.width: 1
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: animePage.model.detail.vodName || "选集"
                        color: appTheme.textPrimaryColor
                        font.family: appTheme.fontFamily
                        font.pixelSize: 16
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        text: "×"
                        ToolTip.visible: hovered
                        ToolTip.text: "关闭选集"
                        onClicked: {
                            window.animeEpisodesOpen = false
                            animeEpisodesWindow.close()
                        }
                    }
                }

                Text {
                    text: "选择要播放的集数"
                    color: appTheme.textMutedColor
                    font.family: appTheme.fontFamily
                    font.pixelSize: 12
                }

                ListView {
                    id: animeEpisodesGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    model: animePage.model && animePage.model.episodes ? animePage.model.episodes : []

                    delegate: Rectangle {
                        id: episodeDel
                        width: ListView.view.width
                        height: 40
                        radius: appTheme.controlRadius
                        // ListView.index 是 delegate 根项的附加属性,嵌套子项无法直接以
                        // 裸 `index` 访问 (Qt6 会抛 "index is not defined",日志刷几万行)。
                        // 在根项上显式导出,子项用 episodeDel.epIndex 引用。
                        // delegate 挂到 view 之前 ListView.index 还是 undefined，直接绑给
                        // int 属性会刷一行 "Unable to assign [undefined] to int"。
                        property int epIndex: ListView.index === undefined ? -1 : ListView.index
                        property bool currentEpisode: String(modelData) === window.animeCurrentEpisode
                        color: currentEpisode
                            ? appTheme.accentMutedColor
                            : (animeEpisodeMouse.containsMouse ? appTheme.panelRaisedColor : "transparent")
                        border.color: currentEpisode
                            ? appTheme.accentColor
                            : (animeEpisodeMouse.containsMouse ? appTheme.borderColor : "transparent")
                        border.width: currentEpisode ? 1 : 0

                        Rectangle {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24
                            height: 24
                            radius: 6
                            color: parent.currentEpisode ? appTheme.accentColor : appTheme.panelRaisedColor

                            Text {
                                anchors.centerIn: parent
                                text: episodeDel.epIndex + 1
                                color: parent.parent.currentEpisode ? "#ffffff" : appTheme.textMutedColor
                                font.family: appTheme.fontFamily
                                font.pixelSize: 10
                                font.bold: parent.parent.currentEpisode
                            }
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 44
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            color: parent.currentEpisode ? appTheme.textPrimaryColor : appTheme.textSecondaryColor
                            font.family: appTheme.fontFamily
                            font.pixelSize: 13
                            font.bold: parent.currentEpisode
                            elide: Text.ElideRight
                        }

                        MouseArea {
                            id: animeEpisodeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            preventStealing: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                window.animeCurrentEpisode = String(modelData)
                                window.animeEpisodesOpen = false
                                animeEpisodesWindow.close()
                                animePage.episodeRequested(modelData)
                                animePage.model.playEpisode(window.animePlaybackVodId, modelData)
                            }
                        }
                    }
                }
            }
        }
    }

    // 监听退出登录：停止播放并重置状态
    Connections {
        target: services.authController
        function onLoginStatusChanged() {
            if (!services.authController.isLoggedIn) {
                playerController.stop()
                if (window.visibility === Window.FullScreen) {
                    window.showNormal()
                }
            } else {
                // 「我的」页在登录前就随主界面一起建好了，那一趟取数只能拿到"请先登录"。
                // 会话一起来就补一次，正好停在页上的人不用退出再进。
                myPage.refreshOnEntry()
            }
        }
    }

    // 登录界面：未登录时覆盖主界面
    LoginView {
        id: loginView
        anchors.fill: parent
        z: 9999
        visible: !services.authController.isLoggedIn
        theme: appTheme
        authController: services.authController
    }
}
