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

If Qt6 is installed through `vcpkg`, configure through the toolchain file and make sure the Qt packages are installed in that triplet:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

## FFmpeg bootstrap note

Task 1 keeps FFmpeg integration optional so the Qt6/QML shell can be configured independently. When media pipeline work begins, turn it on explicitly:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S . -B build -DMINIPLAYER_ENABLE_FFMPEG=ON -DFFMPEG_ROOT="C:/ffmpeg"
```

`cmake/FindFFmpeg.cmake` accepts cache entries for the include directory and each library path. Auto-discovery stays off unless `FFMPEG_ENABLE_DISCOVERY=ON` is set.

## Recorded configure result

Validation command run for this bootstrap task:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap -B E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap/build
```

Observed result on this machine:

- Configure stops at `find_package(Qt6 ...)`.
- Known failure reason: this machine does not currently expose `Qt6Config.cmake` or `qt6-config.cmake` to CMake.
- This failure is caused by missing local Qt6 configuration, not by missing bootstrap source files, QML files, or the project skeleton itself.
