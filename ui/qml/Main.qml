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
        playerController.stop()
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
        for (var i = 0; i < playerController.apiSiteModel.count; ++i) {
            if (playerController.apiSiteModel.typeAt(i) !== "shortvideo") continue
            shortVideoSiteList.append({
                "sourceIndex": i,
                "title": playerController.apiSiteModel.nameAt(i),
                "subtitle": playerController.apiSiteModel.baseUrlAt(i)
            })
        }
        var savedShortVideoUrl = playerController.uiSetting("selectedShortVideoSiteUrl", "")
        var savedShortVideoIndex = findSiteIndexByUrl("shortvideo", savedShortVideoUrl)
        if (savedShortVideoIndex >= 0)
            selectedShortVideoSiteIndex = savedShortVideoIndex
        if (shortVideoSiteList.count > 0
                && (selectedShortVideoSiteIndex < 0
                    || playerController.apiSiteModel.typeAt(selectedShortVideoSiteIndex) !== "shortvideo")) {
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
        var savedPage = playerController.uiSetting("currentPage", sidebar.currentIndex)
        // 旧版页面 0 是首页。首页移除后，将旧设置迁移到视频页。
        if (savedPage === 0)
            savedPage = 1
        sidebar.currentIndex = Math.max(1, Math.min(10, savedPage))
        restoringUiSettings = false

        settingsRestored = true
    }

    function findSiteIndexByUrl(typeName, baseUrl) {
        if (!baseUrl || baseUrl.length === 0)
            return -1
        for (var i = 0; i < playerController.apiSiteModel.count; ++i) {
            if (playerController.apiSiteModel.typeAt(i) === typeName
                    && playerController.apiSiteModel.baseUrlAt(i) === baseUrl) {
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
        if (selectedShortVideoSiteIndex >= 0 && selectedShortVideoSiteIndex < playerController.apiSiteModel.count)
            saveUiSetting("selectedShortVideoSiteUrl", playerController.apiSiteModel.baseUrlAt(selectedShortVideoSiteIndex))
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


    function playShortVideoSite(siteIndex, recordHistory, prefetchedUrl) {
        if (siteIndex < 0 || siteIndex >= playerController.apiSiteModel.count) {
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
            + " name=" + playerController.apiSiteModel.nameAt(siteIndex)
            + " url=" + playerController.apiSiteModel.baseUrlAt(siteIndex))
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
            : playerController.apiSiteModel.baseUrlAt(siteIndex)
        playerController.playShortVideoUrl(playbackUrl,
                                           playerController.apiSiteModel.nameAt(siteIndex))
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

    function retryShortVideo() {
        if (selectedShortVideoSiteIndex >= 0)
            playShortVideoSite(selectedShortVideoSiteIndex, false)
    }

    function failoverShortVideo(message) {
        shortVideoErrorMessage = message || "当前短视频源播放失败"
        shortVideoFailedSourceCount += 1
        shortVideoAutoAdvanceGuard = true
        shortVideoFailoverTimer.restart()
    }

    Connections {
        target: playerController.apiSiteModel
        function onCountChanged() { window.rebuildShortVideoSites() }
        function onCurrentSiteChanged() { window.rebuildShortVideoSites() }
        function onDataChanged() { window.rebuildShortVideoSites() }
    }

    Connections {
        target: playerController
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
            if (window.sidebar.currentIndex === 6 && window.isCurrentShortVideo())
                window.failoverShortVideo(message)
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
            if (playerController.isPlaying && playerController.positionMs > 500)
                window.shortVideoLoadRetryCount = 0
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

    function revealPlayerControls() {
        if (!playerPageActive)
            return
        bottomControlReveal = true
        playerControlHideTimer.restart()
        if (!immersiveMode && normalControlWindow.visible)
            normalControlWindow.raise()
        if (immersiveMode && immersiveControlWindow.visible)
            immersiveControlWindow.raise()
    }

    function schedulePlayerControlsHide() {
        if (!playerPageActive
                || controlBar.hovered
                || floatingControlBar.hovered
                || normalControlHotZoneMouse.containsMouse
                || immersiveHotZoneMouse.containsMouse
                || controlBar.seeking
                || floatingControlBar.seeking
                || playerController.seeking) {
            return
        }
        playerControlHideTimer.restart()
    }

    function togglePlayPause() {
        if (playerController.isPlaying)
            playerController.pause()
        else
            playerController.play()
    }

    function beginHoldSpeed() {
        if (holdSpeedActive || playerController.currentFile.length === 0)
            return
        playbackRateBeforeHold = playerController.playbackRate
        holdSpeedActive = true
        playerController.setPlaybackRate(3.0)
        holdSpeedIndicatorWindow.raise()
    }

    function endHoldSpeed() {
        if (!holdSpeedActive)
            return
        holdSpeedActive = false
        playerController.setPlaybackRate(playbackRateBeforeHold)
    }

    function seekRelative(deltaMs) {
        seekAccum += deltaMs
        seekDebounceTimer.restart()
    }

    function adjustVolume(delta) {
        playerController.setVolume(Math.max(0.0, Math.min(1.0, playerController.volume + delta)))
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

    Shortcut { sequence: "Space"; onActivated: window.togglePlayPause() }
    Shortcut {
        sequence: "Left"
        onActivated: {
            window.seekRelative(-keyboardSeekStepMs)
        }
    }
    Shortcut {
        sequence: "Right"
        onActivated: {
            window.seekRelative(keyboardSeekStepMs)
        }
    }
    Shortcut { sequence: "Up"; onActivated: window.adjustVolume(0.05) }
    Shortcut { sequence: "Down"; onActivated: window.adjustVolume(-0.05) }
    Shortcut { sequence: "["; onActivated: window.adjustVolume(-0.05) }
    Shortcut { sequence: "]"; onActivated: window.adjustVolume(0.05) }
    Shortcut { sequence: "F"; onActivated: window.toggleFullScreen() }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (window.visibility === Window.FullScreen) {
                window.toggleFullScreen()
            } else if (window.videoFillMode) {
                window.setVideoFillMode(false)
            }
        }
    }
    Shortcut { sequence: "Ctrl+O"; onActivated: playerController.openFile() }
    Shortcut { sequence: "Ctrl+U"; onActivated: urlDialog.open() }
    Shortcut { sequence: "A"; onActivated: playerController.cycleAspectRatio() }

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
                    && !playerController.seeking) {
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
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#191919" }
            GradientStop { position: 0.32; color: "#121212" }
            GradientStop { position: 1.0; color: appTheme.windowColor }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Left sidebar navigation
        NavigationSidebar {
            id: sidebar
            Layout.fillHeight: true
            // 鐢婚潰閾烘弧鍙奖鍝嶆挱鏀惧櫒鍐呭锛屼笉搴斿悶鎺夊簲鐢ㄥ鑸€?            // 宸︿晶鑿滃崟浠呭湪鐪熸鐨勭郴缁熷叏灞忕姸鎬佷笅闅愯棌銆?            Layout.preferredWidth: window.visibility === Window.FullScreen ? 0 : 64
            visible: window.visibility !== Window.FullScreen
            theme: appTheme
            onCurrentIndexChanged: window.saveUiSetting("currentPage", currentIndex)
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
                onTogglePlayPauseRequested: window.togglePlayPause()
                onFocusHostRequested: window.contentItem.forceActiveFocus()
                onRevealControlsRequested: window.revealPlayerControls()
                onHideControlsRequested: window.schedulePlayerControlsHide()
                onBeginHoldSpeedRequested: window.beginHoldSpeed()
                onEndHoldSpeedRequested: window.endHoldSpeed()
                onVolumeAdjustmentRequested: function(delta) { window.adjustVolume(delta) }
                onManageSitesRequested: sidebar.currentIndex = 9
                onPlaybackRequested: {
                    window.videoBrowserActive = false
                    window.bottomControlReveal = false
                }
                onReturnToDetailRequested: {
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
            // Page 3: Downloads
            DownloadView {
                id: downloadPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: appTheme.edgePadding
                controller: playerController
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
                onHideControlsRequested: window.schedulePlayerControlsHide()
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
            // Page 8: Hot news
            HotNewsView {
                id: hotNewsPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: playerController
                theme: appTheme
                strings: appText
                onSaveUiSettingRequested: function(key, value) { window.saveUiSetting(key, value) }
            }
            // Page 9: API sites
            SiteManagementView {
                id: sitesPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: appTheme.edgePadding
                controller: playerController
                theme: appTheme
            }
            // Page 10: Music
            MusicView {
                id: musicPage
                Layout.fillWidth: true
                Layout.fillHeight: true
                controller: playerController
                theme: appTheme
                strings: appText
            }
        }
    }

    Window {
        id: playerSearchHotZoneWindow
        width: Math.max(window.playerSearchHotZoneMinWidth,
            Math.min(window.playerSearchHotZoneMaxWidth,
                (window.width - (sidebar.visible ? sidebar.width : 0)) * window.playerSearchHotZoneRatio))
        height: window.playerSearchHotZoneHeight
        x: window.x + (sidebar.visible ? sidebar.width : 0)
            + Math.max(0, (window.width - (sidebar.visible ? sidebar.width : 0) - width) / 2)
        y: window.y
        visible: false
        flags: Qt.Tool | Qt.FramelessWindowHint
        transientParent: window
        color: "transparent"
        onVisibleChanged: if (visible) raise()

        Rectangle {
            anchors.fill: parent
            color: "#26ffffff"
        }

        MouseArea {
            id: playerSearchHotZoneMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            onEntered: playerSearchHotZoneDelay.restart()
            onExited: playerSearchHotZoneDelay.stop()
        }

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
        onManageSitesRequested: sidebar.currentIndex = 9
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
        height: window.playerControlsVisible ? appTheme.controlBarHeight + 16 : 16
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

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 16
            color: "#22ffffff"
            z: 0
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
            onExited: window.schedulePlayerControlsHide()
        }

        PlaybackControlBar {
            id: controlBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: appTheme.edgePadding
            anchors.rightMargin: appTheme.edgePadding
            anchors.bottomMargin: 4
            height: appTheme.controlBarHeight
            visible: window.playerControlsVisible
            opacity: visible ? 1.0 : 0.0
            z: 2
            theme: appTheme
            positionText: Qt.formatTime(new Date(Math.max(0, controlBar.seeking ? controlBar.seekTarget : (window.qtNetworkMode ? qtPlayer.position : playerController.positionMs))), "mm:ss")
            durationText: Qt.formatTime(new Date(Math.max(0, playerController.durationMs)), "mm:ss")
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
                }
            }
            onDownloadRequested: {
                playerController.saveCurrentPlaylistM3u8()
                sidebar.currentIndex = 3
            }
            onPlaybackRateChangeRequested: function(rate) { playerController.setPlaybackRate(rate) }
            onRepeatModeChangeRequested: function(mode) { playerController.setRepeatMode(mode) }
            onFullScreenToggleRequested: window.toggleFullScreen()
            thumbnailImage: playerController.thumbnailPreview
            thumbnailPos: playerController.thumbnailPosition
            thumbnailReady: playerController.thumbnailReady
            onThumbnailRequested: function(positionMs) { playerController.requestThumbnail(positionMs) }
            onHoveredChanged: {
                if (hovered) {
                    window.revealPlayerControls()
                } else {
                    window.schedulePlayerControlsHide()
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
            onExited: window.schedulePlayerControlsHide()
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
            positionText: Qt.formatTime(new Date(Math.max(0, floatingControlBar.seeking ? floatingControlBar.seekTarget : playerController.positionMs)), "mm:ss")
            durationText: Qt.formatTime(new Date(Math.max(0, playerController.durationMs)), "mm:ss")
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
                }
            }
            onDownloadRequested: {
                playerController.saveCurrentPlaylistM3u8()
                sidebar.currentIndex = 3
            }
            onPlaybackRateChangeRequested: function(rate) { playerController.setPlaybackRate(rate) }
            onRepeatModeChangeRequested: function(mode) { playerController.setRepeatMode(mode) }
            onFullScreenToggleRequested: window.toggleFullScreen()
            thumbnailImage: playerController.thumbnailPreview
            thumbnailPos: playerController.thumbnailPosition
            thumbnailReady: playerController.thumbnailReady
            onThumbnailRequested: function(positionMs) { playerController.requestThumbnail(positionMs) }
            onHoveredChanged: {
                if (hovered) {
                    window.revealPlayerControls()
                } else {
                    window.schedulePlayerControlsHide()
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
            apiSiteModel: playerController.apiSiteModel
            sourceSearchModel: playerController.sourceSearchModel
            currentVodName: playerController.currentVodName
            audioTracks: playerController.audioTracks
            currentAudioTrack: playerController.currentAudioTrack
            subtitleTracks: playerController.subtitleTracks
            currentSubtitleTrack: playerController.currentSubtitleTrack
            subtitlesEnabled: playerController.subtitlesEnabled
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

    Dialog {
        id: sourceSwitchDialog
        title: "鎾斁绾胯矾杈冩參"
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: playerController.acceptSourceSwitch()
        onRejected: playerController.rejectSourceSwitch()

        contentItem: ColumnLayout {
            spacing: 10
            Label {
                Layout.preferredWidth: 360
                text: playerController.sourceSwitchReason
                wrapMode: Text.WordWrap
                color: appTheme.textPrimaryColor
            }
            Label {
                Layout.preferredWidth: 360
                text: "是否切换到“" + playerController.sourceSwitchCandidate
                    + "”？切换后会恢复当前集数和播放进度。"
                wrapMode: Text.WordWrap
                color: appTheme.textSecondaryColor
            }
        }

        Connections {
            target: playerController
            function onSourceSwitchSuggestionChanged() {
                if (playerController.sourceSwitchSuggested)
                    sourceSwitchDialog.open()
                else
                    sourceSwitchDialog.close()
            }
        }
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
}
