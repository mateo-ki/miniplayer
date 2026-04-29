# miniPlayer Design Spec

Date: 2026-04-29

## 1. Overview

`miniPlayer` is a desktop local video player built with `CMake + Qt6 + FFmpeg`.

The first release targets a real product-shaped prototype instead of a throwaway demo:

- Play local media files through a custom playback core.
- Use FFmpeg for demuxing, stream inspection, decoding, and playback timing.
- Use Qt Quick/QML for a modern desktop UI inspired by VLC's practical layout.
- Deliver full audio/video synchronization in the first release.
- Expose runtime logs and media metadata so the application feels like a professional playback tool instead of a toy sample.

This spec describes the release-1 architecture and boundaries. It is intentionally focused on a stable local-player foundation. Network streams, subtitles, hardware acceleration, and playlist management are deferred.

## 2. Goals

Release 1 must provide:

- Open and play local video files.
- Full audio playback and video playback with audio as the master clock.
- Pause, resume, stop, and seek while preserving synchronization correctness.
- Display current playback time and total duration.
- Basic volume and mute control.
- Display container, stream, codec, duration, resolution, frame rate, sample rate, channels, and bitrate information.
- Show runtime logs for media open, decode, synchronization, seek, and error events.
- Present a usable, polished QML UI that is inspired by VLC without cloning its exact interface.

## 3. Non-Goals

Release 1 will not include:

- Network stream playback such as `http`, `rtsp`, `rtmp`, or `hls`.
- Subtitle loading or rendering.
- Multiple audio-track switching.
- Hardware decoding.
- Screenshot/export tools.
- Full playlist management.
- Theme switching.
- Production-grade playback speed control.

Some of these capabilities are intentionally anticipated in the module boundaries, but they are not part of the first delivery target.

## 4. Product Direction

The application should feel like a practical desktop media tool:

- Large central video surface.
- Clear bottom control bar.
- Right-side media information panel.
- Bottom runtime log panel.
- Left-side space reserved for current media and future queue/list capabilities.

The UI should take visual cues from VLC's calm, functional, dark presentation:

- Dark neutral background.
- Small amount of orange accent for interactive state.
- Strong spacing and panel hierarchy.
- No flashy animations or decorative noise.
- Enough polish to avoid looking like a raw engineering demo.

The result should be "professional and restrained" rather than "fancy and experimental".

## 5. Architecture

The system is split into four layers.

### 5.1 QML UI Layer

Responsibilities:

- Render the main window, panels, controls, and status views.
- Bind to controller properties and invoke user actions.
- Stay free of FFmpeg details and low-level playback state.

Suggested UI technologies:

- `Qt Quick`
- `Qt Quick Controls`
- `Qt Quick Layouts`

The UI layer should contain reusable QML components for:

- Main window shell
- Video surface container
- Playback control bar
- Media info panel
- Runtime log panel
- Current media / future queue panel

### 5.2 Bridge / ViewModel Layer

This layer connects QML to the playback engine.

Primary class:

- `PlayerController : QObject`

Responsibilities:

- Expose properties for QML binding.
- Translate QML actions into engine commands.
- Convert engine signals into UI-friendly state updates.

Expected exposed properties and invokables include:

- `openFile()`
- `play()`
- `pause()`
- `stop()`
- `seek(qint64 positionMs)`
- `setVolume(float)`
- `setMuted(bool)`
- `isPlaying`
- `isPaused`
- `durationMs`
- `positionMs`
- `volume`
- `muted`
- `currentFile`
- `statusText`
- `videoInfoModel`
- `audioInfoModel`
- `runtimeLogModel`

QML interacts only with this layer and never with FFmpeg objects directly.

### 5.3 Media Core Layer

This layer owns actual playback behavior.

Core responsibilities:

- Open and close media sessions.
- Read packets from the container.
- Decode audio and video streams.
- Maintain packet queues and frame queues.
- Drive audio output.
- Maintain the master clock.
- Schedule video frame presentation against the audio clock.
- Coordinate flush and reset during seek/stop.

Suggested modules:

- `PlayerEngine`
- `MediaSession`
- `DemuxWorker`
- `PacketQueue`
- `AudioDecoder`
- `VideoDecoder`
- `FrameQueue`
- `AudioOutput`
- `AudioClock`
- `VideoSyncScheduler`
- `SeekCoordinator`
- `MediaInfoExtractor`

These modules can initially be implemented with moderate class granularity, but their responsibilities must remain separate even if some of them are grouped during the first pass.

### 5.4 Infrastructure Layer

Responsibilities:

- Logging
- Error reporting
- Configuration
- Thread helpers
- FFmpeg resource wrappers
- Common time/format conversion helpers

Important implementation rule:

- FFmpeg resource ownership must be wrapped in RAII-style C++ utilities so open/close/flush paths remain safe during repeated playback operations.

## 6. Playback Model

Release 1 requires full audio/video synchronization.

### 6.1 Master Clock Strategy

The application must use:

- Audio clock as the master clock.

Reasoning:

- Audio playback reflects actual device playback progression.
- Video presentation can be delayed or dropped relative to audio.
- This is the standard and most reliable strategy for a local player.

### 6.2 Media Flow

Playback flow:

1. User selects a local file in the UI.
2. `PlayerController` forwards the request to `PlayerEngine`.
3. `MediaSession` opens the file and discovers streams.
4. `MediaInfoExtractor` extracts metadata for the right-side panel.
5. `DemuxWorker` reads packets and distributes them to audio/video queues.
6. `AudioDecoder` and `VideoDecoder` consume packets and produce decoded frames.
7. `AudioOutput` sends PCM audio to the playback device and updates the audio clock.
8. `VideoSyncScheduler` compares decoded video frame timestamps to the audio clock and decides whether to present, delay, or drop frames.
9. Engine signals update the controller, which updates QML-bound state and models.

### 6.3 Pause / Resume

Pause must:

- Stop visible playback progression.
- Stop audio advancement.
- Freeze video presentation progression.
- Preserve enough state to resume cleanly without reopening the file.

Resume must:

- Continue from the paused timing position.
- Restore synchronized progression without an additional open cycle.

### 6.4 Seek

Seek is part of the first release scope and must work with synchronization intact.

Seek flow:

1. Request target position.
2. Pause packet consumption temporarily.
3. Flush decoder state.
4. Clear stale packet/frame queues.
5. Seek container position through FFmpeg.
6. Refill queues from the new timeline point.
7. Re-establish audio clock.
8. Resume synchronized playback.

The implementation does not need to support frame-exact editing semantics, but it must behave predictably for user playback control.

### 6.5 Stop

Stop must:

- Halt all workers.
- Clear queues.
- Release session-specific resources.
- Reset exposed UI state to a clean idle form.

Stop is not equivalent to pause. It is a state reset.

## 7. Threading Model

The UI must remain responsive at all times. No blocking IO or decode work may run on the main thread.

Suggested execution model:

- Main/UI thread
  - QML rendering
  - input handling
  - model updates

- Demux thread
  - container reads
  - packet distribution

- Audio decode/output thread
  - audio decode
  - PCM submission
  - audio clock updates

- Video decode thread
  - video decode
  - decoded-frame delivery

- Sync/presentation coordination
  - may be part of engine scheduling instead of a dedicated thread
  - must have a clearly defined ownership point

Rules:

- Cross-thread communication should use Qt signals/slots or other explicit thread-safe messaging mechanisms.
- Shared state must be minimized and isolated behind clear synchronization rules.
- Logging must not block real-time playback paths.

## 8. Rendering Strategy

The UI is QML-based, but playback and synchronization are C++-driven.

Recommended approach:

- Decode frames in C++.
- Convert frames into a Qt-consumable image/rendering form.
- Feed them into a dedicated video rendering bridge used by QML.

Acceptable release-1 rendering options:

- `QVideoSink`/Qt multimedia-compatible bridge if integration stays under engine control.
- A custom image-provider or texture-backed item if required by the selected Qt rendering path.

Selection rule:

- Choose the rendering path that keeps timing control in C++ and does not push synchronization responsibility into QML/JavaScript.

QML must remain a presentation surface, not the playback scheduler.

## 9. UI Design

### 9.1 Main Layout

Main window structure:

- Top toolbar/header
- Left panel for current media and future queue/list space
- Center video area
- Right panel for media information
- Bottom playback control bar
- Bottom runtime log panel

The center video surface should occupy the largest area.

### 9.2 Visual Direction

The first release UI should:

- Reference VLC's practical and readable desktop style.
- Use dark surfaces with disciplined orange accents.
- Prefer clean geometry, clear padding, and restrained borders.
- Avoid a raw default-control appearance.
- Avoid excessive animation.

### 9.3 UX Expectations

The first release should already feel coherent:

- Empty state should guide the user to open a file.
- Panels should not feel like placeholders even when some future features are deferred.
- Media info should be readable at a glance.
- Logs should be easy to scan and distinguish by severity.
- The app should scale reasonably across common desktop window sizes.

## 10. Data Presented to the User

### 10.1 Media Info Panel

Release 1 should show:

- file name
- full file path
- container format
- duration
- video codec
- resolution
- frame rate
- pixel format if available
- audio codec
- sample rate
- channel layout
- bitrate when available

### 10.2 Runtime Log Panel

Release 1 should emit visible logs for:

- FFmpeg initialization
- file open
- stream discovery
- playback start/pause/resume/stop
- seek begin/end
- synchronization status milestones
- warnings
- decode/output errors

Severity categories should at minimum support:

- `info`
- `warn`
- `error`

Additionally:

- `debug` logs may exist internally in release 1, but the visible UI only needs guaranteed support for `info`, `warn`, and `error`.

## 11. File and Module Layout

Suggested project layout:

```text
miniPlayer/
  CMakeLists.txt
  cmake/
  docs/
    superpowers/
      specs/
      plans/
  assets/
    icons/
    themes/
  src/
    app/
    core/
    controller/
    infrastructure/
    media/
    models/
    render/
  ui/
    qml/
      components/
      panels/
      themes/
  third_party/
  tests/
```

Suggested source grouping:

- `src/app`
  - application bootstrap
  - QML registration
  - startup wiring

- `src/controller`
  - `PlayerController`

- `src/core`
  - high-level engine and session coordination

- `src/media`
  - demux, decode, queues, clocks

- `src/render`
  - video frame delivery/render bridge

- `src/models`
  - log model
  - media info model

- `src/infrastructure`
  - logger
  - error utilities
  - thread helpers
  - FFmpeg wrappers

- `ui/qml`
  - main shell
  - components
  - themed controls
  - panel composition

## 12. Build and Dependency Strategy

Primary toolchain:

- `CMake`
- `Qt6`
- `FFmpeg`
- modern C++ compiler with C++20 support preferred

Dependency expectations:

- Qt installed with the required Quick/QML modules.
- FFmpeg development libraries available to CMake.

The build system should:

- Keep FFmpeg discovery isolated in clear CMake logic.
- Avoid hardcoding machine-specific absolute paths in project files.
- Be structured so Windows-first development does not prevent later portability improvements.

## 13. Testing Strategy

Release 1 testing focus:

- repeated open/close cycles
- pause/resume behavior
- seek behavior at multiple positions
- stop/reset behavior
- synchronization stability during normal playback
- invalid/unsupported file handling
- UI/controller state coherence

Testing layers:

- targeted unit tests for pure helpers and non-UI models where practical
- manual integration testing for playback correctness
- explicit regression checklist for synchronization scenarios

Because playback correctness is timing-sensitive, manual verification remains essential even if unit coverage exists.

## 14. Risks and Mitigations

### Risk 1: Audio/video sync drift

Mitigation:

- use audio as master clock
- keep clock ownership centralized
- log drift and correction events
- design seek/reset paths explicitly instead of treating them as side effects

### Risk 2: UI polished too early, playback core delayed

Mitigation:

- prioritize engine and synchronization milestones before visual refinement
- keep UI clean but avoid overspending effort on non-essential styling during early integration

### Risk 3: FFmpeg resource leaks and unstable stop/seek cycles

Mitigation:

- use RAII wrappers
- isolate session lifecycle
- test repeated open/stop/seek flows early

### Risk 4: QML and C++ responsibilities blur

Mitigation:

- keep QML declarative
- keep scheduling and playback decisions in C++
- expose only UI-ready properties and actions through the controller

## 15. Release 1 Acceptance Criteria

Release 1 is considered successful when:

- A local video file can be opened from the UI.
- Audio plays successfully.
- Video displays successfully.
- Audio/video synchronization is maintained during normal playback.
- Pause/resume maintains coherent state.
- Seek works and returns to synchronized playback.
- Stop resets the session cleanly.
- Media information is shown in the info panel.
- Runtime logs surface important playback events and failures.
- The interface feels intentionally designed and not like an unstyled sample app.

## 16. Future Expansion

Planned later phases may include:

- playlist management
- subtitles
- playback speed control
- hardware decoding
- multiple track selection
- screenshots
- network streams
- persistent settings and recent-files history

The release-1 architecture should make these additions incremental rather than requiring a full rewrite.
