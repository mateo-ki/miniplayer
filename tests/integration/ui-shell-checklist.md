# UI Shell Integration Checklist

## Scope

Task 2 builds a QML-only shell for miniPlayer without wiring `PlayerController` or any live media pipeline.

## Acceptance Checklist

- [ ] `Main.qml` renders a full application shell instead of the bootstrap placeholder.
- [ ] The shell uses a shared `Theme.qml` object for colors, spacing, radius, and typography constants.
- [ ] The main content area is a three-column layout with:
  - [ ] left `CurrentMediaPanel`
  - [ ] center `VideoSurfacePane`
  - [ ] right `MediaInfoPanel`
- [ ] A `TopBar` sits above the content area.
- [ ] A `PlaybackControlBar` sits below the content area.
- [ ] A `RuntimeLogPanel` sits at the bottom of the window.
- [ ] The visual direction is dark, layered, and uses orange as a restrained accent.
- [ ] The center video area is the largest region in the layout.
- [ ] The shell remains readable at `1280x720`.
- [ ] Default controls are visually wrapped with panel surfaces or custom backgrounds.
- [ ] The shell exposes future binding points for `PlayerController`, `MediaInfoModel`, and `RuntimeLogModel`.
- [ ] All new QML files are registered in `qt_add_qml_module`.

## Manual Review Notes

- Reference direction: desktop media shell inspired by VLC, but not a copy.
- Non-goal: no controller bindings, no transport logic, no playback state machine.
- Review focus: spacing balance, panel hierarchy, contrast, empty-state affordance, and future binding readiness.

## Verification Log

- `cmake -S E:/project/cpp/miniPlayer -B E:/project/cpp/miniPlayer/build-vcpkg -DCMAKE_TOOLCHAIN_FILE=E:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_PREFIX_PATH=E:/vcpkg/installed/x64-windows`
  - Result: pending local rerun after review fixes
- `cmake --build E:/project/cpp/miniPlayer/build-vcpkg`
  - Result: pending local rerun after review fixes
- `git diff --check`
  - Result: pending local rerun after review fixes
