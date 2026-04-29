# miniPlayer Developer Setup

## Requirements

- CMake 3.24+
- Qt 6 with Quick/QML and Multimedia
- FFmpeg development libraries
- MSVC or another C++20-capable compiler

## First configure

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap -B E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap/build
cmake --build E:/project/cpp/miniPlayer/.worktrees/task-1-bootstrap/build
```

## FFmpeg hints

If CMake cannot find FFmpeg automatically, pass one of the following:

- `-DFFMPEG_ROOT=E:/deps/ffmpeg`
- `-DFFMPEG_INCLUDE_DIR=...`
- `-DFFMPEG_AVFORMAT_LIBRARY=...`
- `-DFFMPEG_AVCODEC_LIBRARY=...`
- `-DFFMPEG_AVUTIL_LIBRARY=...`
- `-DFFMPEG_SWRESAMPLE_LIBRARY=...`
- `-DFFMPEG_SWSCALE_LIBRARY=...`
