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
- [ ] Default controls are visually wrapped with panel surfaces or custom backgrounds.
- [ ] All new QML files are registered in `qt_add_qml_module`.

## Manual Review Notes

- Reference direction: desktop media shell inspired by VLC, but not a copy.
- Non-goal: no controller bindings, no transport logic, no playback state machine.
- Review focus: spacing balance, panel hierarchy, contrast, and whether the shell still reads well at 1440x900.

## Verification Log

- `cmake -S E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap -B E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap/build-vcpkg -DCMAKE_TOOLCHAIN_FILE=E:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_PREFIX_PATH=E:/vcpkg/installed/x64-windows`
  - Result: success
- `cmake --build E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap/build-vcpkg`
  - Result: success after rerunning outside the sandbox because MSBuild needed Windows SDK access under `C:/Users/zql/AppData/Local/Microsoft SDKs`
- `git diff --check`
  - Result: success (only LF/CRLF normalization warnings in Git output, no diff errors)
