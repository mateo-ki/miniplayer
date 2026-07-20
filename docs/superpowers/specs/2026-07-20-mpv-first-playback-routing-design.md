# MPV-First Playback Routing Design

## Goal

Make episode selection react immediately, keep short-video playback on its dedicated page, and use libmpv as the default backend for video playback while retaining the existing FFmpeg pipeline as a fallback when libmpv is unavailable.

## Scope

- Ordinary VOD episode playback.
- Short-video playback.
- Loading-state presentation for newly selected media.
- MPV native-window geometry and visibility on both video playback surfaces.

Local audio, voice, music, downloads, and unrelated navigation remain unchanged.

## Playback Routing

All network-video entry points route through one mpv-first controller path. The path validates and normalizes the URL, publishes the selected file and loading state synchronously, then asks `MpvBackend` to load and play it. If libmpv cannot initialize, the existing FFmpeg pipeline remains the fallback.

Episode selection invokes this path directly after updating the playlist and selected index. It does not defer the playback request with `Qt.callLater`, and it does not explicitly disable mpv before opening the URL.

Short-video source selection uses the same backend path but preserves short-video context and page index. It never navigates to the ordinary video page.

## UI State And Surfaces

The ordinary video page and short-video page each own playback-surface geometry. QML reports the geometry of the currently active surface to `PlayerController`; the inactive surface does not reposition or expose the mpv native window.

On episode click, the ordinary video browser closes and its player surface becomes visible immediately. The loading overlay follows the controller loading state and can render before `currentFile` has been published by the backend.

On short-video click, page 6 remains active. Its dedicated playback surface shows loading, video, and the existing playback controls without switching to page 1.

## State Flow

1. The user selects an episode or short-video source.
2. QML updates playlist or short-video metadata.
3. QML keeps the appropriate playback page active and exposes its surface.
4. The controller synchronously resets the timeline, publishes the target URL, and sets `loading=true`.
5. MPV loads and starts the URL; buffering and progress signals update loading state.
6. If MPV is unavailable, the controller opens the same URL through the existing FFmpeg pipeline.
7. Errors clear loading and remain visible through the existing runtime log/error channel.

## Testing

- Add a controller test covering the shared mpv-first VOD dispatch contract.
- Add a QML/static integration check proving episode clicks are not deferred and short-video playback keeps page 6 active.
- Build the Debug target and run the relevant unit tests.
- Launch the application and verify ordinary episode loading, short-video page retention, mpv playback, controls, console output, and network behavior.

## Non-Goals

- Removing the FFmpeg decoder implementation.
- Changing audio or music backends.
- Redesigning the source browser or playback controls.
- Persisting transient playback state.
