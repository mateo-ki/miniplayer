# MPV-First Playback Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make episode clicks enter loading immediately, keep short-video playback on page 6, and route video playback through libmpv by default with the existing FFmpeg pipeline as fallback.

**Architecture:** `PlayerController::playVideoUrl()` remains the single mpv-first video dispatch path. QML selects the active video surface and reports its geometry, while ordinary VOD and short-video navigation retain separate page state.

**Tech Stack:** C++20, Qt 6, QML, Qt Test, libmpv, CMake

---

### Task 1: Regression Contract

**Files:**
- Modify: `tests/unit/models/ModelTests.cpp`
- Create: `tests/integration/playback-routing-check.ps1`

- [ ] **Step 1: Add a failing controller test**

Add a test that calls `playVodUrl()` with a URL while the controller is idle and asserts that the target URL and loading state are published synchronously. This fails while `playVodUrl()` waits for the FFmpeg pipeline before updating `currentFile`.

- [ ] **Step 2: Add a failing QML routing check**

The script reads `VideoDetailPanel.qml` and `Main.qml`, then fails when episode playback is wrapped in `Qt.callLater`, when short-video navigation assigns page 1, or when page 6 has no playback surface.

- [ ] **Step 3: Run tests and verify RED**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
powershell -ExecutionPolicy Bypass -File tests/integration/playback-routing-check.ps1
```

Expected: the new controller assertion and routing check fail for the current behavior.

### Task 2: MPV-First VOD Dispatch

**Files:**
- Modify: `src/controller/PlayerController.cpp`
- Modify: `src/controller/PlayerController.h`
- Modify: `ui/qml/components/VideoDetailPanel.qml`

- [ ] **Step 1: Route VOD through the shared backend path**

Make `playVodUrl()` validate and normalize its argument, then call `playVideoUrl()` so libmpv is attempted first and the existing fallback remains available.

- [ ] **Step 2: Remove deferred episode playback**

After `setPlaylistEpisodes(...)`, switch to the ordinary player page, emit `playbackRequested()`, and call `playerController.playVodUrl(url)` synchronously.

- [ ] **Step 3: Run focused tests and verify GREEN**

Run the Debug unit tests and routing check. Expected: the immediate VOD assertions pass, with no existing test regression.

### Task 3: Dedicated Short-Video Surface

**Files:**
- Modify: `ui/qml/Main.qml`
- Modify: `ui/qml/components/VideoSurfacePane.qml`
- Modify: `tests/integration/playback-routing-check.ps1`

- [ ] **Step 1: Add active-surface geometry selection**

Keep the ordinary surface geometry update active only on page 1. Add a short-video surface on page 6 and report its geometry only while page 6 is active.

- [ ] **Step 2: Preserve page 6 during playback**

Keep `playShortVideoSite()` on page 6, expose loading and the current short-video URL there, and refresh the native mpv window against the short-video surface.

- [ ] **Step 3: Make loading visible before the first frame**

Allow `VideoSurfacePane` to show its loading overlay while playback is starting even when the previously rendered file state is empty.

- [ ] **Step 4: Run focused checks**

Run the routing script and QML application startup check. Expected: page 6 owns an active surface and episode/short-video routes remain separate.

### Task 4: Full Verification

**Files:**
- Verify: `src/controller/PlayerController.cpp`
- Verify: `ui/qml/Main.qml`
- Verify: `ui/qml/components/VideoDetailPanel.qml`
- Verify: `ui/qml/components/VideoSurfacePane.qml`

- [ ] **Step 1: Build and test**

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
powershell -ExecutionPolicy Bypass -File tests/integration/playback-routing-check.ps1
```

Expected: all commands exit with code 0.

- [ ] **Step 2: Run the application**

Launch the Debug executable, select an ordinary episode, then select a short-video source. Confirm immediate loading, mpv playback, page 6 retention for short video, controls, and absence of QML/runtime errors.

- [ ] **Step 3: Inspect the final diff**

Confirm only task-related files changed and no pre-existing user edits were reverted.
