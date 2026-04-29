# miniPlayer Developer Setup

## Requirements

- CMake 3.24+
- Qt 6 with Quick/QML and Multimedia
- MSVC or another C++20-capable compiler

## Configure from the worktree root

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S . -B build
cmake --build build
```

## Point CMake at Qt6

If CMake cannot find Qt6 automatically, provide either `CMAKE_PREFIX_PATH` or `Qt6_DIR`.

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64"
```

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S . -B build -DQt6_DIR="C:/Qt/6.8.0/msvc2022_64/lib/cmake/Qt6"
```

On this machine, Qt6 is available through `vcpkg` at `E:/vcpkg/installed/x64-windows/share/Qt6/Qt6Config.cmake`. A working configure command is:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="E:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET="x64-windows" `
  -DCMAKE_PREFIX_PATH="E:/vcpkg/installed/x64-windows"
```

## FFmpeg bootstrap note

Task 1 keeps FFmpeg integration optional so the Qt6/QML shell can be configured independently. For later media tasks, prefer the standard/vcpkg FFmpeg package discovery path first. The bootstrap CMake flow now does that automatically when `MINIPLAYER_ENABLE_FFMPEG=ON`.

Use the custom cache-based fallback only when a package manager cannot provide FFmpeg:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S . -B build `
  -DMINIPLAYER_ENABLE_FFMPEG=ON `
  -DMINIPLAYER_FFMPEG_PROVIDER="custom-cache" `
  -DFFMPEG_ROOT="C:/ffmpeg"
```

`cmake/FindFFmpeg.cmake` is now only an explicit fallback. It accepts cache entries for the include directory and each library path, and auto-discovery stays off unless `FFMPEG_ENABLE_DISCOVERY=ON` is set.

## Recorded configure result

Baseline command run for this bootstrap task:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap -B E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap/build
```

Observed baseline result on this machine:

- Configure stops at `find_package(Qt6 ...)`.
- At the time of that command, CMake was not given the local Qt6 package path, so the default search path did not resolve `Qt6Config.cmake`.
- This does not indicate missing bootstrap source files or a broken project skeleton.

Validated vcpkg command on this machine:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap -B E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap/build-vcpkg -DCMAKE_TOOLCHAIN_FILE=E:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_PREFIX_PATH=E:/vcpkg/installed/x64-windows
```

Observed vcpkg result on this machine:

- Configure succeeds.
- CMake writes generated files to `E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap/build-vcpkg`.
- Qt6 is resolved from the `E:/vcpkg/installed/x64-windows` prefix when the toolchain file and triplet are provided.
