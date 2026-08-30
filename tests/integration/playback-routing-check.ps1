$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$controller = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "src/controller/PlayerController.cpp")
$detail = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/VideoDetailPanel.qml")
$main = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/Main.qml")
$musicView = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/MusicView.qml")
$imageView = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/ImageView.qml")
$shortVideoView = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/ShortVideoView.qml")
$videoPlayerView = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/VideoPlayerView.qml")
$searchView = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/SearchView.qml")
$siteManagementView = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/SiteManagementView.qml")
$apiSiteModel = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "src/models/ApiSiteModel.cpp")
$videoSearchModel = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "src/models/VideoSearchModel.cpp")
$mpvBackend = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "src/mpv/MpvBackend.cpp")
$sidebar = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/NavigationSidebar.qml")
$cache = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "src/media/ImageCacheService.cpp")
$beeClient = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "src/media/BeeClient.cpp")
$beeModel = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "src/models/BeeVideoModel.cpp")
$beeView = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/BeeView.qml")
$cmake = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "CMakeLists.txt")
$packageInstaller = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "cmake/PackageInstaller.cmake")
$vodStart = $controller.IndexOf("void PlayerController::playVodUrl")
$vodEnd = $controller.IndexOf("void PlayerController::resolveAndPlayUrl", $vodStart)
if ($vodStart -lt 0 -or $vodEnd -le $vodStart) {
    throw "unable to locate playVodUrl implementation"
}
$playVodBody = $controller.Substring($vodStart, $vodEnd - $vodStart)

function Assert-Contains {
    param([string]$Content, [string]$Expected, [string]$Message)
    if (-not $Content.Contains($Expected)) { throw $Message }
}

function Assert-NotContains {
    param([string]$Content, [string]$Unexpected, [string]$Message)
    if ($Content.Contains($Unexpected)) { throw $Message }
}

function Assert-Matches {
    param([string]$Content, [string]$Pattern, [string]$Message)
    if ($Content -notmatch $Pattern) { throw $Message }
}

Assert-Contains $playVodBody "playVideoUrl(trimmed);" `
    "playVodUrl must dispatch through the mpv-first playVideoUrl path"
Assert-NotContains $detail "Qt.callLater(function()" `
    "episode playback must not be deferred with Qt.callLater"
Assert-Contains $main "var playerVisible = (index === 1 && !videoBrowserVisible)" `
    "page 6 must keep the mpv surface visible only during short-video playback"
Assert-Contains $main "ShortVideoView" `
    "page 6 must mount the extracted short-video view"
Assert-Contains $shortVideoView "id: shortVideoVideoPane" `
    "page 6 must own a dedicated short-video playback surface"
Assert-Contains $shortVideoView "shortVideoVideoPane.syncMpvVideoGeometry()" `
    "short-video playback must report its own mpv geometry"
Assert-Contains $main "&& bottomControlReveal" `
    "player controls must honor the auto-hide state so subtitles are not permanently covered"
Assert-Contains $main "readonly property int playerControlHotZoneHeight: appTheme.controlBarHeight" `
    "the video control reveal hot zone must match the control bar height"
Assert-Contains $main "height: window.playerControlHotZoneHeight" `
    "the normal video control window must use one shared bar and hot-zone height"
Assert-NotContains $main "appTheme.controlBarHeight + window.playerControlHotZoneHeight" `
    "the control window must not stack the bar and hot-zone heights"
Assert-Contains $main "height - window.playerControlHotZoneHeight" `
    "the video gesture layer must reveal controls only in the matching bottom hot zone"
Assert-Contains $main "onVideoBrowserVisibleChanged:" `
    "video browser visibility must update the native mpv surface immediately"
Assert-Contains $main "playerController.setMpvSurfaceVisible(!videoBrowserVisible)" `
    "returning to video detail must hide the native mpv surface immediately"
Assert-Contains $sidebar "property int currentIndex: 1" `
    "the application must start on the video page"
Assert-NotContains $sidebar 'label: "\u9996\u9875"' `
    "the removed home page must not remain in navigation"
Assert-Contains $main "if (savedPage === 0)" `
    "saved home-page settings must migrate to video"
Assert-Matches $videoPlayerView 'root\.hostWindow\.visible\s*&&\s*root\.pageActive\s*&&\s*!root\.browserVisible' `
    "the detail return button must be limited to the ordinary video page"
Assert-Contains $videoPlayerView "root.sidebar.visible ? root.sidebar.width : 0" `
    "the detail return button must always stay to the right of a visible navigation sidebar"
Assert-NotContains $videoPlayerView "root.sidebar.visible && !root.immersiveMode" `
    "fill mode must not disable navigation-sidebar avoidance outside fullscreen"
Assert-Contains $main "playerController.stop()" `
    "returning to video detail must stop playback"
Assert-Contains $main "window.visibility = Window.Windowed" `
    "returning to video detail must leave fullscreen mode"
Assert-Contains $main "property bool shortVideoPlaybackActive: sidebar.currentIndex === 6" `
    "short-video page must stay mounted while switching or retrying playback"
Assert-Contains $main "function playNextShortVideoByMode()" `
    "short-video playback must support automatic next-item switching"
Assert-Contains $main "playerController.stopShortVideo()" `
    "leaving the short-video page must stop and clear short-video playback"
Assert-Contains $main "id: shortVideoOverlayWindow" `
    "short-video playback must own a dedicated interaction overlay"
Assert-Contains $main "id: shortVideoPrefetchPool" `
    "short-video playback must maintain a five-item prefetch candidate pool"
Assert-Contains $controller "const bool isRemoteHls" `
    "all remote HLS playback must enter the playlist filter"
Assert-Contains $controller "fetchAndFilterHls(trimmed, hlsRequestSerial_)" `
    "remote HLS playback must filter the manifest before mpv load"
Assert-NotContains $controller "parsed.host().contains(QStringLiteral(\"lzcdn\")" `
    "HLS filtering must not be restricted to a single site"
Assert-Contains $detail "setPlaybackSources" `
    "video detail playback must register all source lines"
Assert-Contains $controller "firstFrameTimer_->setInterval(10000)" `
    "source quality detection must allow ten seconds for the first frame"
Assert-NotContains $controller "stallTimer_" `
    "continuous buffering must not start a source-switch timeout"
Assert-Contains $apiSiteModel "melo-box-app/raw/master/vod.txt" `
    "remote site loading must use the maintained Gitee site file"
Assert-Contains $siteManagementView "apiSiteModel.loadRemoteSites()" `
    "the site management page must expose remote site loading"
Assert-Contains $siteManagementView "apiSiteModel.remoteSitesLoading" `
    "the remote site button must reflect its loading state"
Assert-Contains $siteManagementView "apiSiteModel.moveSiteToSlot(sourceIndex, insertSlot)" `
    "the complete site list must support drag reordering"
Assert-Contains $siteManagementView "preventStealing: true" `
    "the reorder handle must retain pointer capture while dragging"
Assert-Contains $siteManagementView "root.reorderVisualOffset(siteRow.y)" `
    "drag reordering must use a visual offset instead of changing ListView delegate layout coordinates"
Assert-Contains $siteManagementView "insertSlot = hoveredIndex" `
    "drag reordering must calculate an insertion slot from the hovered row"
Assert-Contains $apiSiteModel "slot > from ? slot - 1 : slot" `
    "the model must convert insertion slots to final row indexes in one place"
Assert-Contains $siteManagementView "id: reorderScrollTimer" `
    "drag reordering must support edge-triggered automatic scrolling"
Assert-Contains $siteManagementView "siteList.contentY = Math.max(0, Math.min(maximumY" `
    "drag edge scrolling must stay within the list content bounds"
Assert-Contains $siteManagementView "var previousContentY = siteList.contentY" `
    "site list rebuilds must preserve the current scroll position"
Assert-Contains $siteManagementView "root.reorderSourceIndex = -1" `
    "drag state must be cleared before the reordered model refreshes delegates"
Assert-Contains $main "function onOrderChanged() { window.rebuildShortVideoSites() }" `
    "site reordering must refresh short-video source indexes"
Assert-Contains $siteManagementView "apiSiteModel.togglePremium(model.sourceIndex)" `
    "site rows must expose the premium marker toggle"
Assert-Contains $siteManagementView "apiSiteModel.removeSelectedSites()" `
    "the site management page must expose bulk deletion for checked sites"
Assert-Contains $apiSiteModel "QString ApiSiteModel::removeSelectedSites()" `
    "bulk site deletion must be implemented by the model"
Assert-Contains $apiSiteModel 'site["premium"] = item.premium' `
    "encrypted site sharing must preserve premium markers"
Assert-Contains $apiSiteModel "bool ApiSiteModel::enforcePremiumOrder()" `
    "premium sites must be stably grouped before standard sites"
Assert-Contains $apiSiteModel "to = qBound(0, to, premiumCount - 1)" `
    "manual reordering must keep premium sites inside the premium group"
Assert-NotContains $siteManagementView "apiSiteModel.loadJsonVideoSites()" `
    "the JSON video-site import entry must remain hidden from the site management page"
Assert-Contains $apiSiteModel "video_sources_default.json" `
    "JSON import must use the configured remote video source file"
Assert-Contains $apiSiteModel 'object.value(QStringLiteral("api"))' `
    "JSON import must parse the source API field"
Assert-Contains $apiSiteModel "enqueueSiteStatusChecks(importedUrls)" `
    "JSON-imported video sites must be queued for availability checks"
Assert-Contains $apiSiteModel "constexpr int kMaxConcurrentStatusChecks = 4" `
    "bulk site checks must limit concurrent network requests"
Assert-Contains $apiSiteModel "finishSiteStatusCheck(normalized)" `
    "site check completion must continue the throttled queue"
Assert-Contains $siteManagementView "root.syncFilteredSite(row)" `
    "site status changes must update only the affected list rows"
Assert-NotContains $main "function onDataChanged() { window.rebuildShortVideoSites() }" `
    "site status changes must not rebuild the short-video site cache"
Assert-Contains $detail 'setPlaybackSources' `
    "video detail playback must register all source lines"
Assert-Contains $mpvBackend '"cache-secs", "180"' `
    "HLS playback must keep a three-minute forward cache target"
Assert-Contains $mpvBackend '"demuxer-readahead-secs", "180"' `
    "HLS playback must request additional segments ahead"
Assert-Contains $mpvBackend '"demuxer-max-bytes", "512MiB"' `
    "HLS forward buffering must have enough byte capacity"
Assert-Contains $main "id: sourceSwitchDialog" `
    "source switching must require explicit user confirmation"
Assert-Contains $imageView "root.controller.imageDiskCacheCount" `
    "the image page must expose disk cache status"
Assert-Contains $imageView "root.controller.clearImageCache()" `
    "the image page must expose cache clearing"
Assert-Contains $videoPlayerView "onDoubleClicked: root.togglePlayPauseRequested()" `
    "double-clicking the video surface must toggle playback"
Assert-Contains $videoPlayerView "anchors.bottomMargin: 0" `
    "the video surface must fill the player instead of reserving control-bar space"
Assert-Contains $main "controlBarReservedHeight: 0" `
    "ordinary playback must not shrink the video for the control bar"
Assert-NotContains $main 'color: "#22ffffff"' `
    "the bottom control reveal hot zone must remain visually transparent"
Assert-Contains $main "function hidePlayerControlsImmediately()" `
    "the player must expose an immediate control-hide path"
Assert-Contains $main "|| normalControlHotZoneMouse.containsMouse" `
    "immediate hiding must not flicker while the pointer is still inside a control hot zone"
Assert-Contains $main "onExited: window.hidePlayerControlsImmediately()" `
    "leaving a control hot zone must hide controls immediately"
Assert-Contains $main "onHideControlsRequested: window.hidePlayerControlsImmediately()" `
    "moving above the bottom video hot zone must hide controls immediately"
Assert-Contains $searchView "id: remarksBadge" `
    "video cards must expose the episode/update badge"
Assert-Contains $searchView "anchors.bottom: parent.bottom" `
    "the episode/update badge must stay at the card bottom-right"
Assert-Contains $searchView 'color: "#ffffff"' `
    "the episode/update badge must use white text"
Assert-Contains $videoSearchModel 'category[QStringLiteral("parentTypeId")] = jsonValueToString(obj.value(QStringLiteral("type_pid")))' `
    "video categories must preserve the API parent type ID"
Assert-Contains $searchView "property string selectedParentTypeId" `
    "the video list must track the expanded parent category separately"
Assert-Contains $searchView "function selectChildCategory(typeId)" `
    "secondary video categories must have their own selection path"
Assert-Contains $searchView "id: categoryPopup" `
    "secondary video categories must open in a popup below the parent category"
Assert-Contains $searchView "popup.open()" `
    "clicking a parent category with children must open its category popup"
Assert-Contains $searchView "if (root.childCategoryTags.length > 0)" `
    "parent categories with children must use the non-query popup path"
Assert-Matches $searchView 'popup\.open\(\)\s+return' `
    "opening the secondary category popup must return before loading a video list"
Assert-Contains $searchView "? theme.accentMutedColor" `
    "selected video categories must use the softer muted accent background"
Assert-Contains $searchView 'root.selectedTypeId = typeId || ""' `
    "selecting a secondary category must use that category's own type ID"
Assert-Contains $searchView "playerController.loadVideoListByCategory(root.selectedTypeId, page)" `
    "category loading must pass the selected secondary type ID to the controller"
Assert-Contains $searchView '"sitePremium": playerController.apiSiteModel.premiumAt(i)' `
    "the video-site dropdown cache must preserve premium site state"

Assert-Contains $beeClient "film-recommend-model/select" `
    "Bee must use the provider recommendation endpoint when the search box is empty"
Assert-Contains $beeClient 'QByteArrayLiteral("text/plain")' `
    "Bee recommendation requests must send the provider-required text/plain Accept header"
Assert-Contains $beeClient "decryptB64" `
    "Bee responses must be decrypted instead of being parsed as plain JSON"
Assert-Contains $beeClient 'Connection", "close"' `
    "Bee requests must close connections to avoid gateway connection reuse failures"
Assert-Contains $beeClient "status == 503" `
    "Bee must retry transient 503 responses"
Assert-Contains $beeClient "RemoteHostClosedError" `
    "Bee must retry remote connection closes"
Assert-Contains $beeModel "loadRecommended()" `
    "Bee model must expose a dedicated recommendation loading path"
Assert-Matches $beeModel 'if \(normalizedKeyword\.isEmpty\(\)\)\s*\{\s*loadRecommended\(\);' `
    "empty Bee searches must restore recommendations instead of calling /vod/search?wd="
Assert-Contains $beeModel "detailRequestId_" `
    "Bee detail responses must ignore stale requests"
Assert-Contains $beeView "Component.onCompleted: root.ensureRecommended()" `
    "Bee page must load recommendations on first display"
Assert-Contains $beeView "VideoDetailPanel" `
    "Bee detail must reuse the ordinary video detail panel"
Assert-Contains $beeView "required property string vodPic" `
    "Bee cards must bind the poster role explicitly"
Assert-Contains $cmake '"${_miniPlayerQtBinDir}/jpeg62.dll"' `
    "vcpkg JPEG runtime must be deployed beside the Debug/Release executable"
Assert-Contains $cmake '"${_miniPlayerQtBinDir}/libwebpdecoder.dll"' `
    "vcpkg WebP decoder runtime must be deployed beside the executable"
Assert-Contains $packageInstaller '"jpeg62.dll"' `
    "installer validation must include the JPEG runtime"
Assert-Contains $packageInstaller '"libwebpdecoder.dll"' `
    "installer validation must include the WebP decoder runtime"
Assert-Contains $searchView "visible: model.sitePremium" `
    "premium video sites must display a marker in the site dropdown"
Assert-Contains $searchView "id: premiumSiteText" `
    "the premium video-site marker must include its visible label"
Assert-Contains $main "id: videoGestureWindow" `
    "ordinary mpv playback must expose a top-level gesture window above the native video surface"
Assert-Contains $main "onDoubleClicked: window.togglePlayPause()" `
    "the native video gesture layer must toggle play and pause on double-click"
Assert-Contains $main "pressAndHoldInterval: 500" `
    "the native video gesture layer must recognize long presses"
Assert-Contains $main "id: holdSpeedIndicatorWindow" `
    "ordinary video gestures must show feedback above the native video surface"
Assert-Contains $main "text: window.holdSpeedActive ?" `
    "long-press playback must show a centered 3x indicator"
Assert-Contains $main "playbackGestureFeedbackTimer.restart()" `
    "double-click play and pause feedback must disappear automatically"
Assert-Contains $main "Qt.WindowTransparentForInput" `
    "gesture feedback must not block video pointer input"
Assert-Contains $main "window.beginHoldSpeed()" `
    "long-pressing ordinary video must enable temporary three-times playback"
Assert-Contains $videoPlayerView "function raisePlaybackOverlays()" `
    "the native video gesture layer must not cover the return-to-detail overlay"
Assert-Contains $controller "suspendCurrentVideoForShortVideo();" `
    "starting short-video playback must preserve the ordinary video session"
Assert-Contains $controller "restoreSuspendedVideoAfterShortVideo();" `
    "leaving short-video playback must restore the ordinary video session"
Assert-Contains $main "onTogglePlayPauseRequested: window.togglePlayPause()" `
    "the extracted video player must route double-click playback to the window controller"
Assert-Contains $main "playerController.playFromPlaylist(nextEpisodeIndex)" `
    "ordinary video playback must automatically advance to the next episode"
Assert-Contains $main "if (shortVideoWheelDelta < 200)" `
    "short-video wheel navigation must accumulate roughly two mouse-wheel steps"
Assert-Contains $main 'sequence: "Escape"' `
    "the player must provide an Escape shortcut"
Assert-Contains $main "window.toggleFullScreen()" `
    "Escape must leave fullscreen through the synchronized fullscreen path"
Assert-NotContains $main "onScreenshotRequested:" `
    "the player page must not bind the removed screenshot action"
Assert-NotContains $main "onFillModeToggleRequested:" `
    "the player page must not bind the removed fill-mode control"
Assert-Contains $controller 'types"), QStringLiteral("lrc")' `
    "search playback must request lyrics by source and song ID"
Assert-Contains $controller "loadMusicLyricsForCurrentTrack" `
    "music playback must load lyrics through the controller lyric pipeline"
Assert-Contains $controller "musicLyricRequestSerial_" `
    "lyric requests must be invalidated when the track changes"
Assert-Contains $musicView "adjustMusicLyricOffset(-500)" `
    "music lyrics must expose a negative offset adjustment"
Assert-Contains $musicView "setMusicLyricDisplayOptions" `
    "music lyrics must expose translation and romanization toggles"
Assert-Contains $main "MusicView" `
    "Main.qml must mount the extracted music view component"
Assert-Contains $controller "setCurrentMusicLrc(lrc)" `
    "resolved lyrics must be parsed into the music lyric model"
Assert-Contains $cache "200LL * 1024 * 1024" `
    "image disk cache must be limited to 200 MB"
Assert-Contains $cache "7LL * 24 * 60 * 60 * 1000" `
    "image disk cache entries must expire after seven days"
Assert-NotContains $cache 'key + QStringLiteral(".bin")' `
    "image disk cache must store real image files instead of raw .bin blobs"
Assert-Contains $cache "extensionFromBytes(bytes, mimeType)" `
    "cached image extensions must come from file signatures"
Assert-Contains $cache "migrateLegacyBinFiles();" `
    "legacy .bin image cache files must be migrated on startup"
Assert-Contains $cache "localPath" `
    "cache entries must expose the real local image path"
Assert-Contains $controller "localImageFileUrl(cached.localPath)" `
    "prefetched cache hits must display the real cached image file"
Assert-Contains $controller "已从磁盘缓存加载图片" `
    "random image must serve from disk cache before hitting the network"
Assert-Contains $controller "localImageFileUrl(trimmed)" `
    "prefetched image display must read bytes back from the cached file"
Assert-Contains $main "raiseEpisodeOverlays()" `
    "episode picker overlays must stay above the gesture layer"
Assert-Contains $main "enabled: !mediaDrawer.isOpen && !window.animeEpisodesOpen" `
    "the video gesture layer must not steal clicks from episode pickers"
Assert-Contains $main "Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint" `
    "anime episode window must stay on top of the gesture layer"
Assert-Contains $imageView "readonly property int imagePoolTargetSize: 8" `
    "the in-memory image pool must contain eight images"
Assert-Contains $imageView "readonly property int imagePrefetchConcurrency: 4" `
    "image prefetch must not compete with foreground loading through excessive concurrency"
Assert-Contains $imageView "onDoubleClicked: imagePreviewDialog.open()" `
    "double-clicking the image must open the large preview"
$m3u8OnlyLabel = ([string][char]0x4EC5) + ([char]0x5B58) + " m3u8"
$damagedDirectorLabel = ([string][char]0x7035) + ([char]0x517C) + ([char]0x7D28)
$damagedM3u8Label = ([string][char]0x6D60) + ([char]0x5B58) + "m3u8"
$damagedRatingLabel = ([string][char]0x7338) + "?"
Assert-Contains $detail $m3u8OnlyLabel `
    "the m3u8-only source label must remain valid UTF-8 text"
Assert-NotContains $detail $damagedDirectorLabel `
    "known damaged director label must not return"
Assert-NotContains $detail $damagedM3u8Label `
    "known damaged m3u8 label must not return"
Assert-NotContains $detail $damagedRatingLabel `
    "known damaged rating label must not return"

Write-Host "Playback routing checks passed."
