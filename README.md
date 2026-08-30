# MeloBox

MeloBox 是一个基于 Qt 6 / QML / C++ 的 Windows 桌面多媒体工具箱，包含本地与网络视频播放、资源站搜索与选集播放、图片刷新、短视频、语音、音乐、热讯、下载和站点管理等功能，并支持 GitHub Releases 自动更新。

## 目录结构

```
src/           C++ 源码（app / core / media / mpv / models / render / controller / infrastructure）
ui/qml/        QML 界面（Main、components、panels、themes）
docs/          需求、设计与用户文档
tests/         单元测试与集成检查
cmake/         CMake 模块（FFmpeg 查找、安装包打包）
resources/     图标、qt.conf 等资源
third_party/   libmpv 开发库
installer/     Inno Setup 安装脚本
download/      运行时下载输出目录（已忽略）
```

## Release 构建与打包

在项目根目录执行以下命令：

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="D:/vcpkg/scripts/buildsystems/vcpkg.cmake" -DMELOBOX_ENABLE_RUNTIME_DEPLOYMENT=ON
cmake --build build --config Release --target MeloBox --parallel
cmake --build build --config Release --target package_installer --parallel
```

构建完成后，可执行文件位于 `build/Release/`，安装包位于 `dist/installer/`。

FFmpeg 源码构建、轻量包（不内置 mpv）、更新源配置和测试运行等完整说明见 [docs/README.md](docs/README.md)。

## 文档

- [docs/README.md](docs/README.md)：功能说明、站点 URL 配置、构建/打包/测试完整手册
- [docs/legado-book-source.md](docs/legado-book-source.md)：书源（Legado 阅读）模块与规则引擎说明
- [docs/site-requirements.md](docs/site-requirements.md) / [site-design.md](docs/site-design.md) / [site-tasks.md](docs/site-tasks.md)：站点模块（含蜜蜂接入）的需求、设计与任务
- [docs/music-module-design.md](docs/music-module-design.md) 等音乐模块文档、[docs/playback-flow-design.md](docs/playback-flow-design.md) 播放流程设计
- [docs/testing/](docs/testing/)：手动验证清单与集成检查
- [docs/superpowers/](docs/superpowers/)：历史实现计划与设计存档

## 免责声明

本项目仅作为桌面播放器和多媒体工具箱示例使用，不内置、不存储、不分发任何第三方内容；用户自行添加的站点及其内容由用户自行负责。完整免责声明见 [docs/README.md](docs/README.md)。
