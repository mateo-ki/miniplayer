# MeloBox

MeloBox 是一个基于 Qt 6 / QML / C++ 的桌面多媒体工具箱，当前包含本地播放、网络视频播放、资源站搜索、图片刷新、短视频、语音、音乐、热讯、下载和站点管理等功能。

## 功能概览

- 本地视频和网络视频播放
- 视频资源站搜索、详情、选集播放和播放列表
- 图片站点随机图片、同源刷新、换源刷新、1/3/5 秒自动刷新
- 短视频源选择和播放
- 随机语音播放、下载和复制
- 音乐搜索、歌单加载、歌词滚动、下载、打开下载目录和音量调节
- 热讯聚合
- API 站点管理、检测、筛选、去重和加密分享
- GitHub Releases 自动更新检查和安装包下载

## 免责声明

本项目仅作为桌面播放器和多媒体工具箱示例使用，软件本身不内置、不存储、不分发任何影视、音乐、图片、语音或其他第三方内容。

用户自行添加、导入或使用的站点 URL、接口、媒体链接及其返回内容，均由对应第三方站点或用户自行负责。请确保你拥有访问、播放、下载、保存或分享相关内容的合法权利，并遵守所在地法律法规、版权规则和第三方服务条款。

开发者不对第三方站点的可用性、安全性、合法性、内容准确性、广告、跳转、失效、侵权风险或数据损失承担责任。若你认为某个站点或内容存在权利问题，请停止使用并联系对应内容提供方处理。

## 站点 URL 说明

站点在软件的“站点”模块中维护，主要字段是：

- 名称：显示用名称，例如“示例视频站”
- URL：接口基础地址或直链接口地址
- 类型：`视频`、`图片`、`短视频`

### 视频站点

视频站点通常填写资源站 API 的基础地址。软件会在此基础上请求列表、搜索和详情接口。

常见形式：

```text
https://example.com/api.php/provide/vod
https://example.com/api.php
```

不同资源站接口格式可能不一致。如果列表能打开但详情或播放失败，通常是该站点字段、播放源或返回格式不兼容。

### 图片站点

图片站点用于随机图片模块。URL 可以是：

- 直接返回图片的接口
- 返回 302 跳转到图片的接口
- 返回 JSON / 文本并包含图片 URL 的接口

常见形式：

```text
https://example.com/random-image
https://example.com/api.php?type=image
```

图片模块支持两种刷新模式：

- 同源刷新：一直请求当前图片站点
- 换源刷新：每次刷新自动切到下一个图片站点

自动刷新可选关闭、1 秒、3 秒、5 秒。慢接口会等上一张加载结束后再继续，不会并发请求。

### 短视频站点

短视频站点通常填写直接返回视频 URL、302 跳转视频 URL，或返回可解析视频链接的接口。

常见形式：

```text
https://example.com/video.php
https://example.com/video.php?type=video
```

短视频播放依赖系统网络、接口稳定性和播放器后端能力。若出现只有声音没有画面，通常和源站返回、视频编码或播放器窗口切换有关。

### 站点可用性

站点可能随时失效、限流、变更接口格式、加入防盗链或返回广告内容。MeloBox 只负责发起请求和尝试解析，不保证任意站点长期可用。

## 自动更新

MeloBox 支持通过 GitHub Releases 检查更新。发布流程建议：

1. 在 GitHub 仓库创建 Release
2. Tag 使用版本号，例如：

```text
v1.0
```

3. 上传安装包资产，例如：

```text
MeloBox-Setup-1.0.exe
```

4. 编译时配置更新源：

```powershell
cmake -S . -B build `
  -DMELOBOX_UPDATE_OWNER="你的GitHub用户名" `
  -DMELOBOX_UPDATE_REPO="你的仓库名"
```

软件会请求：

```text
https://api.github.com/repos/你的GitHub用户名/你的仓库名/releases/latest
```

如果发现更高版本，并且 Release Assets 中存在 `MeloBox` 名称的 `.exe` 安装包，就可以下载并启动安装包。

## 构建

推荐 Windows + MSVC + vcpkg：

```powershell
$prefix = "D:/vcpkg/installed/x64-windows"
cmake -S . -B build `
  -DMELOBOX_ENABLE_FFMPEG=ON `
  -DCMAKE_PREFIX_PATH="$prefix" `
  -DFFMPEG_INCLUDE_DIR="$prefix/include" `
  -DFFMPEG_AVFORMAT_LIBRARY="$prefix/debug/lib/avformat.lib" `
  -DFFMPEG_AVCODEC_LIBRARY="$prefix/debug/lib/avcodec.lib" `
  -DFFMPEG_AVUTIL_LIBRARY="$prefix/debug/lib/avutil.lib" `
  -DFFMPEG_SWRESAMPLE_LIBRARY="$prefix/debug/lib/swresample.lib" `
  -DFFMPEG_SWSCALE_LIBRARY="$prefix/debug/lib/swscale.lib" `
  -DFFMPEG_ENABLE_DISCOVERY=OFF

cmake --build build --config Release --target MeloBox
```

## 打包

项目使用 Inno Setup 生成安装包：

```powershell
cmake --build build --config Release --target package_installer
```

如果 Inno Setup 不在默认路径，可在配置时指定：

```powershell
cmake -S . -B build -DMELOBOX_INNO_SETUP_COMPILER="D:/software/InnoSetup6/ISCC.exe"
```

默认安装包会内置 `libmpv-2.dll`，以提高网络视频、短视频和复杂流媒体的播放兼容性。若需要生成更小的轻量包，可在配置时关闭：

```powershell
cmake -S . -B build -DMELOBOX_BUNDLE_MPV=OFF
```

输出目录：

```text
dist/installer/
```

安装包示例：

```text
MeloBox-Setup-1.0.exe
```

## 测试

```powershell
ctest --test-dir build -C Debug --output-on-failure
```
