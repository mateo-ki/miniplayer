$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$controller = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "src/controller/PlayerController.cpp")
$detail = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/VideoDetailPanel.qml")
$main = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/Main.qml")
$musicView = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/MusicView.qml")
$imageView = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/ImageView.qml")
$shortVideoView = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/ShortVideoView.qml")
$videoPlayerView = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/VideoPlayerView.qml")
$sidebar = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "ui/qml/components/NavigationSidebar.qml")
$cache = Get-Content -Raw -Encoding utf8 (Join-Path $projectRoot "src/media/ImageCacheService.cpp")
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
Assert-Contains $videoPlayerView "root.hostWindow.visible && root.pageActive && !root.browserVisible" `
    "the detail return button must be limited to the ordinary video page"
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
Assert-Contains $controller "stallTimer_->setInterval(8000)" `
    "source quality detection must wait for eight seconds of continuous buffering"
Assert-Contains $controller "mpvBuffering_ && isPlaying_" `
    "stall detection must use the independent mpv buffering state"
Assert-Contains $main "id: sourceSwitchDialog" `
    "source switching must require explicit user confirmation"
Assert-Contains $imageView "root.controller.imageDiskCacheCount" `
    "the image page must expose disk cache status"
Assert-Contains $imageView "root.controller.clearImageCache()" `
    "the image page must expose cache clearing"
Assert-Contains $videoPlayerView "onDoubleClicked: root.togglePlayPauseRequested()" `
    "double-clicking the video surface must toggle playback"
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
Assert-Contains $imageView "readonly property int imagePoolTargetSize: 8" `
    "the in-memory image pool must contain eight images"
Assert-Contains $imageView "readonly property int imagePrefetchConcurrency: 2" `
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
