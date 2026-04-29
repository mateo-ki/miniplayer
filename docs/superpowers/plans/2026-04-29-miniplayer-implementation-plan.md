# miniPlayer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Qt6 + FFmpeg local desktop player with a QML UI, VLC-inspired visual direction, and release-1 audio-mastered A/V synchronization.

**Architecture:** The app is split into a QML UI layer, a `PlayerController` bridge layer, a C++ playback core, and infrastructure helpers. Playback correctness is centered on an audio master clock, while QML remains a presentation surface for controls, media metadata, and runtime logs.

**Tech Stack:** CMake, C++20, Qt6 Quick/QML, Qt6 Multimedia, FFmpeg, GoogleTest or Qt Test, Windows desktop toolchain

---

## File Structure

Planned file layout and responsibility split:

- `CMakeLists.txt`
  - Root build entry point
- `cmake/FindFFmpeg.cmake`
  - FFmpeg library discovery
- `src/app/main.cpp`
  - Application startup and QML bootstrap
- `src/app/AppBootstrap.h`
- `src/app/AppBootstrap.cpp`
  - Registers controller and models with QML
- `src/controller/PlayerController.h`
- `src/controller/PlayerController.cpp`
  - QML bridge and UI-facing properties/invokables
- `src/core/PlayerEngine.h`
- `src/core/PlayerEngine.cpp`
  - High-level playback orchestration
- `src/core/MediaSession.h`
- `src/core/MediaSession.cpp`
  - Per-media open/close lifecycle
- `src/media/PacketQueue.h`
- `src/media/PacketQueue.cpp`
  - Thread-safe packet queue
- `src/media/FrameQueue.h`
- `src/media/FrameQueue.cpp`
  - Thread-safe decoded-frame queue
- `src/media/DemuxWorker.h`
- `src/media/DemuxWorker.cpp`
  - Packet reading and stream dispatch
- `src/media/AudioDecoder.h`
- `src/media/AudioDecoder.cpp`
  - Audio decoding
- `src/media/VideoDecoder.h`
- `src/media/VideoDecoder.cpp`
  - Video decoding
- `src/media/AudioClock.h`
- `src/media/AudioClock.cpp`
  - Audio-master timing source
- `src/media/VideoSyncScheduler.h`
- `src/media/VideoSyncScheduler.cpp`
  - Video timing decisions against audio clock
- `src/media/SeekCoordinator.h`
- `src/media/SeekCoordinator.cpp`
  - Flush/seek/restart coordination
- `src/media/MediaInfoExtractor.h`
- `src/media/MediaInfoExtractor.cpp`
  - Metadata extraction
- `src/render/VideoFrameBridge.h`
- `src/render/VideoFrameBridge.cpp`
  - C++ to QML video frame delivery
- `src/models/RuntimeLogModel.h`
- `src/models/RuntimeLogModel.cpp`
  - Log list model
- `src/models/MediaInfoModel.h`
- `src/models/MediaInfoModel.cpp`
  - Media metadata list model
- `src/infrastructure/Logger.h`
- `src/infrastructure/Logger.cpp`
  - Logging entry point
- `src/infrastructure/FfmpegWrappers.h`
  - RAII wrappers for FFmpeg handles
- `src/infrastructure/Error.h`
  - Error/result types
- `ui/qml/Main.qml`
  - Main application shell
- `ui/qml/components/*.qml`
  - Reusable QML controls
- `ui/qml/panels/*.qml`
  - Left/media panel, info panel, log panel
- `ui/qml/themes/Theme.qml`
  - Theme constants
- `tests/unit/...`
  - Queue, clock, controller, scheduler tests
- `tests/integration/...`
  - Integration-level smoke tests or harness notes
- `docs/README.md`
  - Developer setup instructions

## Task 1: Initialize repository skeleton and build foundation

**Files:**
- Create: `.gitignore`
- Create: `CMakeLists.txt`
- Create: `cmake/FindFFmpeg.cmake`
- Create: `src/app/main.cpp`
- Create: `src/app/AppBootstrap.h`
- Create: `src/app/AppBootstrap.cpp`
- Create: `ui/qml/Main.qml`
- Create: `docs/README.md`

- [ ] **Step 1: Initialize the Git repository**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git init
```

Expected: Git creates `.git` under `E:\project\cpp\miniPlayer`.

- [ ] **Step 2: Create the initial `.gitignore`**

```gitignore
build/
.vs/
.vscode/
.idea/
*.user
*.obj
*.pdb
*.ilk
*.exe
*.dll
*.lib
*.exp
*.log
.superpowers/
```

- [ ] **Step 3: Write the root `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.24)
project(miniPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Multimedia)
find_package(FFmpeg REQUIRED)

qt_standard_project_setup()

qt_add_executable(miniPlayer
    src/app/main.cpp
    src/app/AppBootstrap.cpp
    src/controller/PlayerController.cpp
    src/core/PlayerEngine.cpp
    src/core/MediaSession.cpp
    src/media/PacketQueue.cpp
    src/media/FrameQueue.cpp
    src/media/DemuxWorker.cpp
    src/media/AudioDecoder.cpp
    src/media/VideoDecoder.cpp
    src/media/AudioClock.cpp
    src/media/VideoSyncScheduler.cpp
    src/media/SeekCoordinator.cpp
    src/media/MediaInfoExtractor.cpp
    src/render/VideoFrameBridge.cpp
    src/models/RuntimeLogModel.cpp
    src/models/MediaInfoModel.cpp
    src/infrastructure/Logger.cpp
)

qt_add_qml_module(miniPlayer
    URI MiniPlayer
    VERSION 1.0
    QML_FILES
        ui/qml/Main.qml
)

target_include_directories(miniPlayer PRIVATE src)
target_link_libraries(miniPlayer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Multimedia
    FFmpeg::avformat
    FFmpeg::avcodec
    FFmpeg::avutil
    FFmpeg::swresample
    FFmpeg::swscale
)
```

- [ ] **Step 4: Write a minimal `FindFFmpeg.cmake`**

```cmake
add_library(FFmpeg::avformat UNKNOWN IMPORTED)
add_library(FFmpeg::avcodec UNKNOWN IMPORTED)
add_library(FFmpeg::avutil UNKNOWN IMPORTED)
add_library(FFmpeg::swresample UNKNOWN IMPORTED)
add_library(FFmpeg::swscale UNKNOWN IMPORTED)

set(FFmpeg_FOUND TRUE)
message(STATUS "Stub FindFFmpeg.cmake added. Replace imported library paths during environment setup.")
```

- [ ] **Step 5: Write a minimal application bootstrap**

`src/app/main.cpp`

```cpp
#include <QGuiApplication>

#include "app/AppBootstrap.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    AppBootstrap bootstrap;
    bootstrap.initialize();
    return app.exec();
}
```

`src/app/AppBootstrap.h`

```cpp
#pragma once

class AppBootstrap {
public:
    void initialize();
};
```

`src/app/AppBootstrap.cpp`

```cpp
#include "app/AppBootstrap.h"

void AppBootstrap::initialize() {}
```

- [ ] **Step 6: Write a minimal `ui/qml/Main.qml`**

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    width: 1440
    height: 900
    visible: true
    title: "miniPlayer"

    Rectangle {
        anchors.fill: parent
        color: "#141414"
    }
}
```

- [ ] **Step 7: Add setup notes to `docs/README.md`**

```md
# miniPlayer Developer Setup

## Requirements

- CMake 3.24+
- Qt 6 with Quick/QML and Multimedia
- FFmpeg development libraries
- MSVC or another C++20-capable compiler

## First configure

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S . -B build
cmake --build build
```
```

- [ ] **Step 8: Configure the project**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S . -B build
```

Expected: CMake configures or fails specifically on not-yet-created source files or temporary FFmpeg imported-target setup, which is acceptable at this stage.

- [ ] **Step 9: Commit the bootstrap**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add .
git commit -m "chore: bootstrap miniplayer project skeleton"
```

## Task 2: Build the QML shell and theme system

**Files:**
- Modify: `ui/qml/Main.qml`
- Create: `ui/qml/themes/Theme.qml`
- Create: `ui/qml/components/TopBar.qml`
- Create: `ui/qml/components/PlaybackControlBar.qml`
- Create: `ui/qml/components/VideoSurfacePane.qml`
- Create: `ui/qml/panels/CurrentMediaPanel.qml`
- Create: `ui/qml/panels/MediaInfoPanel.qml`
- Create: `ui/qml/panels/RuntimeLogPanel.qml`

- [ ] **Step 1: Write the initial UI smoke checklist**

Create `tests/integration/ui-shell-checklist.md`:

```md
# UI Shell Checklist

- Application launches to a dark window
- Top bar is visible
- Left panel, center panel, right panel, control bar, and log panel are visible
- Layout remains readable at 1280x720
- No panel uses default bright styling
```

- [ ] **Step 2: Create the QML theme constants**

`ui/qml/themes/Theme.qml`

```qml
pragma Singleton
import QtQuick

QtObject {
    readonly property color appBackground: "#141414"
    readonly property color panelBackground: "#1d1d1d"
    readonly property color panelAltBackground: "#232323"
    readonly property color borderColor: "#343434"
    readonly property color textPrimary: "#f2f2f2"
    readonly property color textMuted: "#b7b7b7"
    readonly property color accent: "#ff8a1c"
    readonly property color accentStrong: "#ff6a00"
    readonly property int radiusM: 12
    readonly property int radiusL: 18
}
```

- [ ] **Step 3: Create the top bar component**

`ui/qml/components/TopBar.qml`

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

Rectangle {
    color: "#121212"
    border.color: Theme.borderColor
    implicitHeight: 52

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18

        Label { text: "miniPlayer"; color: Theme.textPrimary; font.pixelSize: 18; font.bold: true }
        Item { Layout.fillWidth: true }
        Label { text: "Open"; color: Theme.textMuted }
        Label { text: "Recent"; color: Theme.textMuted }
        Label { text: "Tools"; color: Theme.textMuted }
    }
}
```

- [ ] **Step 4: Create the main visual panels**

Use the following minimal skeletons:

```qml
// ui/qml/components/VideoSurfacePane.qml
import QtQuick
import "../themes"

Rectangle {
    color: "#111111"
    radius: Theme.radiusL
    border.color: Theme.borderColor
}
```

```qml
// ui/qml/panels/CurrentMediaPanel.qml
import QtQuick
import "../themes"

Rectangle {
    color: Theme.panelBackground
    border.color: Theme.borderColor
}
```

```qml
// ui/qml/panels/MediaInfoPanel.qml
import QtQuick
import "../themes"

Rectangle {
    color: Theme.panelBackground
    border.color: Theme.borderColor
}
```

```qml
// ui/qml/panels/RuntimeLogPanel.qml
import QtQuick
import "../themes"

Rectangle {
    color: "#111111"
    border.color: Theme.borderColor
}
```

- [ ] **Step 5: Create the bottom playback control bar**

`ui/qml/components/PlaybackControlBar.qml`

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

Rectangle {
    color: "#141414"
    border.color: Theme.borderColor
    implicitHeight: 96

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 16

        Button { text: "Open" }
        Button { text: "Play" }
        Button { text: "Pause" }
        Button { text: "Stop" }

        Slider { Layout.fillWidth: true; from: 0; to: 100; value: 0 }

        Label { text: "00:00 / 00:00"; color: Theme.textMuted }
        Label { text: "1.0x"; color: Theme.textMuted }
    }
}
```

- [ ] **Step 6: Compose the full `Main.qml` layout**

Replace `ui/qml/Main.qml` with:

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"
import "panels"
import "themes"

ApplicationWindow {
    width: 1440
    height: 900
    visible: true
    color: Theme.appBackground
    title: "miniPlayer"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar { Layout.fillWidth: true }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            CurrentMediaPanel { Layout.preferredWidth: 260; Layout.fillHeight: true }
            VideoSurfacePane { Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 18 }
            MediaInfoPanel { Layout.preferredWidth: 340; Layout.fillHeight: true }
        }

        PlaybackControlBar { Layout.fillWidth: true }
        RuntimeLogPanel { Layout.fillWidth: true; Layout.preferredHeight: 160 }
    }
}
```

- [ ] **Step 7: Build and inspect the shell**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build
```

Expected: Build succeeds or reports only upstream playback-core source files not yet implemented.

- [ ] **Step 8: Commit the QML shell**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add ui/qml tests/integration/ui-shell-checklist.md
git commit -m "feat: add qml shell and theme foundation"
```

## Task 3: Add logging and media info models

**Files:**
- Create: `src/models/RuntimeLogModel.h`
- Create: `src/models/RuntimeLogModel.cpp`
- Create: `src/models/MediaInfoModel.h`
- Create: `src/models/MediaInfoModel.cpp`
- Create: `src/infrastructure/Logger.h`
- Create: `src/infrastructure/Logger.cpp`
- Create: `tests/unit/models/RuntimeLogModelTests.cpp`
- Create: `tests/unit/models/MediaInfoModelTests.cpp`

- [ ] **Step 1: Write the failing `RuntimeLogModel` test**

```cpp
#include <gtest/gtest.h>

#include "models/RuntimeLogModel.h"

TEST(RuntimeLogModelTests, AppendsVisibleLogEntries) {
    RuntimeLogModel model;
    model.append("info", "ffmpeg initialized");
    EXPECT_EQ(model.rowCount(), 1);
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target RuntimeLogModelTests
ctest --test-dir build -R RuntimeLogModelTests --output-on-failure
```

Expected: Fail because model files and test target are not implemented yet.

- [ ] **Step 3: Implement `RuntimeLogModel`**

`src/models/RuntimeLogModel.h`

```cpp
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct RuntimeLogEntry {
    QString level;
    QString message;
};

class RuntimeLogModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles { LevelRole = Qt::UserRole + 1, MessageRole };

    explicit RuntimeLogModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void clear();
    void append(const QString &level, const QString &message);

private:
    QVector<RuntimeLogEntry> entries_;
};
```

`src/models/RuntimeLogModel.cpp`

```cpp
#include "models/RuntimeLogModel.h"

RuntimeLogModel::RuntimeLogModel(QObject *parent) : QAbstractListModel(parent) {}

int RuntimeLogModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant RuntimeLogModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto &entry = entries_[index.row()];
    switch (role) {
    case LevelRole: return entry.level;
    case MessageRole: return entry.message;
    default: return {};
    }
}

QHash<int, QByteArray> RuntimeLogModel::roleNames() const {
    return { {LevelRole, "level"}, {MessageRole, "message"} };
}

void RuntimeLogModel::clear() {
    beginResetModel();
    entries_.clear();
    endResetModel();
}

void RuntimeLogModel::append(const QString &level, const QString &message) {
    beginInsertRows({}, rowCount(), rowCount());
    entries_.push_back({level, message});
    endInsertRows();
}
```

- [ ] **Step 4: Implement `MediaInfoModel`**

```cpp
// src/models/MediaInfoModel.h
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct MediaInfoItem {
    QString key;
    QString value;
};

class MediaInfoModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles { KeyRole = Qt::UserRole + 1, ValueRole };

    explicit MediaInfoModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void replaceAll(const QVector<MediaInfoItem> &items);

private:
    QVector<MediaInfoItem> items_;
};
```

```cpp
// src/models/MediaInfoModel.cpp
#include "models/MediaInfoModel.h"

MediaInfoModel::MediaInfoModel(QObject *parent) : QAbstractListModel(parent) {}

int MediaInfoModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

QVariant MediaInfoModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto &item = items_[index.row()];
    switch (role) {
    case KeyRole: return item.key;
    case ValueRole: return item.value;
    default: return {};
    }
}

QHash<int, QByteArray> MediaInfoModel::roleNames() const {
    return { {KeyRole, "label"}, {ValueRole, "value"} };
}

void MediaInfoModel::replaceAll(const QVector<MediaInfoItem> &items) {
    beginResetModel();
    items_ = items;
    endResetModel();
}
```

- [ ] **Step 5: Implement the logger facade**

```cpp
// src/infrastructure/Logger.h
#pragma once

#include <functional>
#include <QString>

class Logger {
public:
    using Sink = std::function<void(const QString &, const QString &)>;

    static Logger &instance();
    void setSink(Sink sink);
    void info(const QString &message) const;
    void warn(const QString &message) const;
    void error(const QString &message) const;

private:
    Sink sink_;
};
```

```cpp
// src/infrastructure/Logger.cpp
#include "infrastructure/Logger.h"

Logger &Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setSink(Sink sink) { sink_ = std::move(sink); }
void Logger::info(const QString &message) const { if (sink_) sink_("info", message); }
void Logger::warn(const QString &message) const { if (sink_) sink_("warn", message); }
void Logger::error(const QString &message) const { if (sink_) sink_("error", message); }
```

- [ ] **Step 6: Run tests and verify they pass**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target RuntimeLogModelTests MediaInfoModelTests
ctest --test-dir build -R "RuntimeLogModelTests|MediaInfoModelTests" --output-on-failure
```

Expected: Both model test targets pass.

- [ ] **Step 7: Commit the models and logger**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add src/models src/infrastructure tests/unit/models
git commit -m "feat: add runtime log and media info models"
```

## Task 4: Implement `PlayerController` and QML binding surface

**Files:**
- Create: `src/controller/PlayerController.h`
- Create: `src/controller/PlayerController.cpp`
- Modify: `src/app/AppBootstrap.cpp`
- Modify: `ui/qml/Main.qml`
- Modify: `ui/qml/components/PlaybackControlBar.qml`
- Create: `tests/unit/controller/PlayerControllerTests.cpp`

- [ ] **Step 1: Write the failing controller state test**

```cpp
#include <gtest/gtest.h>

#include "controller/PlayerController.h"

TEST(PlayerControllerTests, ExposesInitialIdleState) {
    PlayerController controller;
    EXPECT_FALSE(controller.isPlaying());
    EXPECT_EQ(controller.durationMs(), 0);
}
```

- [ ] **Step 2: Run the controller test and verify it fails**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target PlayerControllerTests
ctest --test-dir build -R PlayerControllerTests --output-on-failure
```

Expected: Fail because `PlayerController` does not exist yet.

- [ ] **Step 3: Implement the controller header**

```cpp
#pragma once

#include <QObject>
#include <QString>

#include "models/MediaInfoModel.h"
#include "models/RuntimeLogModel.h"

class PlayerEngine;

class PlayerController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStateChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY playbackStateChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY timelineChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY timelineChanged)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(MediaInfoModel* mediaInfoModel READ mediaInfoModel CONSTANT)
    Q_PROPERTY(RuntimeLogModel* runtimeLogModel READ runtimeLogModel CONSTANT)

public:
    explicit PlayerController(QObject *parent = nullptr);
    bool isPlaying() const;
    bool isPaused() const;
    qint64 durationMs() const;
    qint64 positionMs() const;
    float volume() const;
    bool muted() const;
    QString currentFile() const;
    MediaInfoModel *mediaInfoModel();
    RuntimeLogModel *runtimeLogModel();

    Q_INVOKABLE void openFile();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);

    void setVolume(float volume);
    void setMuted(bool muted);

signals:
    void playbackStateChanged();
    void timelineChanged();
    void volumeChanged();
    void mutedChanged();
    void currentFileChanged();

private:
    MediaInfoModel mediaInfoModel_;
    RuntimeLogModel runtimeLogModel_;
    bool isPlaying_ = false;
    bool isPaused_ = false;
    qint64 durationMs_ = 0;
    qint64 positionMs_ = 0;
    float volume_ = 1.0f;
    bool muted_ = false;
    QString currentFile_;
};
```

- [ ] **Step 4: Implement the minimal controller behavior**

```cpp
#include "controller/PlayerController.h"

#include "infrastructure/Logger.h"

PlayerController::PlayerController(QObject *parent) : QObject(parent) {
    Logger::instance().setSink([this](const QString &level, const QString &message) {
        runtimeLogModel_.append(level, message);
    });
}

bool PlayerController::isPlaying() const { return isPlaying_; }
bool PlayerController::isPaused() const { return isPaused_; }
qint64 PlayerController::durationMs() const { return durationMs_; }
qint64 PlayerController::positionMs() const { return positionMs_; }
float PlayerController::volume() const { return volume_; }
bool PlayerController::muted() const { return muted_; }
QString PlayerController::currentFile() const { return currentFile_; }
MediaInfoModel *PlayerController::mediaInfoModel() { return &mediaInfoModel_; }
RuntimeLogModel *PlayerController::runtimeLogModel() { return &runtimeLogModel_; }

void PlayerController::openFile() { Logger::instance().info("openFile requested"); }
void PlayerController::play() { isPlaying_ = true; isPaused_ = false; emit playbackStateChanged(); }
void PlayerController::pause() { isPaused_ = true; emit playbackStateChanged(); }
void PlayerController::stop() { isPlaying_ = false; isPaused_ = false; positionMs_ = 0; emit playbackStateChanged(); emit timelineChanged(); }
void PlayerController::seek(qint64 positionMs) { positionMs_ = positionMs; emit timelineChanged(); }
void PlayerController::setVolume(float volume) { volume_ = volume; emit volumeChanged(); }
void PlayerController::setMuted(bool muted) { muted_ = muted; emit mutedChanged(); }
```

- [ ] **Step 5: Register the controller in `AppBootstrap`**

```cpp
#include "app/AppBootstrap.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "controller/PlayerController.h"

void AppBootstrap::initialize() {
    static QQmlApplicationEngine engine;
    static PlayerController controller;
    engine.rootContext()->setContextProperty("playerController", &controller);
    engine.load(QUrl("qrc:/qt/qml/MiniPlayer/ui/qml/Main.qml"));
}
```

- [ ] **Step 6: Bind the control bar to `playerController`**

Use these bindings in `ui/qml/components/PlaybackControlBar.qml`:

```qml
Button { text: "Open"; onClicked: playerController.openFile() }
Button { text: "Play"; onClicked: playerController.play() }
Button { text: "Pause"; onClicked: playerController.pause() }
Button { text: "Stop"; onClicked: playerController.stop() }
Slider {
    Layout.fillWidth: true
    from: 0
    to: Math.max(playerController.durationMs, 1)
    value: playerController.positionMs
    onMoved: playerController.seek(value)
}
Label {
    text: playerController.positionMs + " / " + playerController.durationMs
    color: Theme.textMuted
}
```

- [ ] **Step 7: Run the controller test and a manual launch**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target PlayerControllerTests miniPlayer
ctest --test-dir build -R PlayerControllerTests --output-on-failure
.\build\miniPlayer.exe
```

Expected: Controller tests pass and the UI launches with wired buttons.

- [ ] **Step 8: Commit the controller bridge**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add src/controller src/app ui/qml/components/PlaybackControlBar.qml ui/qml/Main.qml tests/unit/controller
git commit -m "feat: add player controller and qml bindings"
```

## Task 5: Add FFmpeg wrappers, media session, and metadata extraction

**Files:**
- Create: `src/infrastructure/FfmpegWrappers.h`
- Create: `src/infrastructure/Error.h`
- Create: `src/core/MediaSession.h`
- Create: `src/core/MediaSession.cpp`
- Create: `src/media/MediaInfoExtractor.h`
- Create: `src/media/MediaInfoExtractor.cpp`
- Create: `tests/unit/core/MediaSessionTests.cpp`

- [ ] **Step 1: Write the failing media session test**

```cpp
#include <gtest/gtest.h>

#include "core/MediaSession.h"

TEST(MediaSessionTests, RejectsMissingFile) {
    MediaSession session;
    auto result = session.open("Z:/missing-file.mp4");
    EXPECT_FALSE(result.ok);
}
```

- [ ] **Step 2: Run the media session test and verify it fails**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target MediaSessionTests
ctest --test-dir build -R MediaSessionTests --output-on-failure
```

Expected: Fail because `MediaSession` and `Error` are not implemented.

- [ ] **Step 3: Implement error and RAII wrapper types**

```cpp
// src/infrastructure/Error.h
#pragma once

#include <QString>

struct Error {
    bool ok = true;
    QString message;

    static Error success() { return {}; }
    static Error failure(const QString &message) { return {false, message}; }
};
```

```cpp
// src/infrastructure/FfmpegWrappers.h
#pragma once

extern "C" {
#include <libavformat/avformat.h>
}

#include <memory>

struct AvFormatContextDeleter {
    void operator()(AVFormatContext *ctx) const {
        if (ctx) {
            avformat_close_input(&ctx);
        }
    }
};

using UniqueAvFormatContext = std::unique_ptr<AVFormatContext, AvFormatContextDeleter>;
```

- [ ] **Step 4: Implement `MediaSession`**

```cpp
// src/core/MediaSession.h
#pragma once

#include <QString>

#include "infrastructure/Error.h"
#include "infrastructure/FfmpegWrappers.h"

class MediaSession {
public:
    Error open(const QString &path);
    void close();
    bool isOpen() const;
    AVFormatContext *formatContext() const;

private:
    UniqueAvFormatContext formatContext_;
};
```

```cpp
// src/core/MediaSession.cpp
#include "core/MediaSession.h"

#include <QFileInfo>

Error MediaSession::open(const QString &path) {
    if (!QFileInfo::exists(path)) {
        return Error::failure("file does not exist");
    }
    AVFormatContext *raw = nullptr;
    if (avformat_open_input(&raw, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        return Error::failure("avformat_open_input failed");
    }
    formatContext_.reset(raw);
    if (avformat_find_stream_info(formatContext_.get(), nullptr) < 0) {
        close();
        return Error::failure("avformat_find_stream_info failed");
    }
    return Error::success();
}

void MediaSession::close() { formatContext_.reset(); }
bool MediaSession::isOpen() const { return formatContext_ != nullptr; }
AVFormatContext *MediaSession::formatContext() const { return formatContext_.get(); }
```

- [ ] **Step 5: Implement `MediaInfoExtractor`**

```cpp
// src/media/MediaInfoExtractor.h
#pragma once

#include <QVector>

#include "models/MediaInfoModel.h"

struct AVFormatContext;

class MediaInfoExtractor {
public:
    QVector<MediaInfoItem> extract(const QString &path, const AVFormatContext *context) const;
};
```

```cpp
// src/media/MediaInfoExtractor.cpp
#include "media/MediaInfoExtractor.h"

extern "C" {
#include <libavformat/avformat.h>
}

QVector<MediaInfoItem> MediaInfoExtractor::extract(const QString &path, const AVFormatContext *context) const {
    QVector<MediaInfoItem> items;
    items.push_back({"File", path});
    if (!context) {
        return items;
    }
    items.push_back({"Container", context->iformat ? context->iformat->long_name : "unknown"});
    items.push_back({"Duration", QString::number(context->duration)});
    items.push_back({"Bitrate", QString::number(context->bit_rate)});
    return items;
}
```

- [ ] **Step 6: Run the tests**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target MediaSessionTests
ctest --test-dir build -R MediaSessionTests --output-on-failure
```

Expected: Missing-file test passes.

- [ ] **Step 7: Commit the media session foundation**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add src/core/MediaSession.* src/media/MediaInfoExtractor.* src/infrastructure/FfmpegWrappers.h src/infrastructure/Error.h tests/unit/core
git commit -m "feat: add ffmpeg session and media metadata extraction"
```

## Task 6: Implement queues and timing primitives

**Files:**
- Create: `src/media/PacketQueue.h`
- Create: `src/media/PacketQueue.cpp`
- Create: `src/media/FrameQueue.h`
- Create: `src/media/FrameQueue.cpp`
- Create: `src/media/AudioClock.h`
- Create: `src/media/AudioClock.cpp`
- Create: `tests/unit/media/PacketQueueTests.cpp`
- Create: `tests/unit/media/AudioClockTests.cpp`

- [ ] **Step 1: Write the failing queue and clock tests**

```cpp
TEST(PacketQueueTests, PushPopPreservesOrder) {
    PacketQueue queue;
    queue.push({1});
    queue.push({2});
    EXPECT_EQ(queue.pop().streamIndex, 1);
}
```

```cpp
TEST(AudioClockTests, ReportsLatestPlaybackPosition) {
    AudioClock clock;
    clock.update(1200);
    EXPECT_EQ(clock.currentPositionMs(), 1200);
}
```

- [ ] **Step 2: Run the tests and verify they fail**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target PacketQueueTests AudioClockTests
ctest --test-dir build -R "PacketQueueTests|AudioClockTests" --output-on-failure
```

Expected: Fail because queue and clock classes do not exist.

- [ ] **Step 3: Implement the basic queue and clock types**

```cpp
// src/media/PacketQueue.h
#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

struct PacketEnvelope {
    int streamIndex = -1;
};

class PacketQueue {
public:
    void push(const PacketEnvelope &packet);
    PacketEnvelope pop();
    void clear();

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<PacketEnvelope> queue_;
};
```

```cpp
// src/media/PacketQueue.cpp
#include "media/PacketQueue.h"

void PacketQueue::push(const PacketEnvelope &packet) {
    std::scoped_lock lock(mutex_);
    queue_.push(packet);
    cv_.notify_one();
}

PacketEnvelope PacketQueue::pop() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty(); });
    auto packet = queue_.front();
    queue_.pop();
    return packet;
}

void PacketQueue::clear() {
    std::scoped_lock lock(mutex_);
    queue_ = {};
}
```

```cpp
// src/media/AudioClock.h
#pragma once

#include <atomic>

class AudioClock {
public:
    void update(qint64 positionMs);
    qint64 currentPositionMs() const;

private:
    std::atomic<qint64> positionMs_ = 0;
};
```

```cpp
// src/media/AudioClock.cpp
#include "media/AudioClock.h"

void AudioClock::update(qint64 positionMs) { positionMs_.store(positionMs); }
qint64 AudioClock::currentPositionMs() const { return positionMs_.load(); }
```

- [ ] **Step 4: Add `FrameQueue` now so later decoder tasks stay small**

```cpp
// src/media/FrameQueue.h
#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

struct VideoFrameEnvelope {
    qint64 ptsMs = 0;
};

class FrameQueue {
public:
    void push(const VideoFrameEnvelope &frame);
    VideoFrameEnvelope pop();
    void clear();

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<VideoFrameEnvelope> queue_;
};
```

- [ ] **Step 5: Run the tests**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target PacketQueueTests AudioClockTests
ctest --test-dir build -R "PacketQueueTests|AudioClockTests" --output-on-failure
```

Expected: Queue and clock tests pass.

- [ ] **Step 6: Commit the timing primitives**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add src/media/PacketQueue.* src/media/FrameQueue.* src/media/AudioClock.* tests/unit/media
git commit -m "feat: add packet queue frame queue and audio clock"
```

## Task 7: Add demuxing, audio decode, and audio output path

**Files:**
- Create: `src/media/DemuxWorker.h`
- Create: `src/media/DemuxWorker.cpp`
- Create: `src/media/AudioDecoder.h`
- Create: `src/media/AudioDecoder.cpp`
- Create: `src/media/AudioOutput.h`
- Create: `src/media/AudioOutput.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/integration/audio-pipeline-checklist.md`

- [ ] **Step 1: Write the integration checklist for audio pipeline**

```md
# Audio Pipeline Checklist

- Open a valid MP4 file
- Detect at least one audio stream
- Decode audio frames without crashing
- Submit PCM to Qt audio output
- Update audio clock while audio is playing
```

- [ ] **Step 2: Implement `DemuxWorker` skeleton**

```cpp
#pragma once

#include <QObject>

class MediaSession;
class PacketQueue;

class DemuxWorker final : public QObject {
    Q_OBJECT
public:
    void configure(MediaSession *session, PacketQueue *audioQueue, PacketQueue *videoQueue);
public slots:
    void process();
signals:
    void finished();
private:
    MediaSession *session_ = nullptr;
    PacketQueue *audioQueue_ = nullptr;
    PacketQueue *videoQueue_ = nullptr;
};
```

- [ ] **Step 3: Implement minimal compile-safe `AudioDecoder` and `AudioOutput` classes**

```cpp
// AudioDecoder.h
#pragma once

class AudioDecoder {
public:
    bool initialize();
};
```

```cpp
// AudioOutput.h
#pragma once

#include <QtMultimedia/QAudioSink>

class AudioOutput {
public:
    bool initialize();
};
```

- [ ] **Step 4: Expand `CMakeLists.txt` to include `src/media/AudioOutput.cpp`**

Add:

```cmake
src/media/AudioOutput.cpp
```

to the `qt_add_executable(miniPlayer ...)` source list.

- [ ] **Step 5: Build the audio path**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S . -B build
cmake --build build
```

Expected: Project compiles with the minimal audio path scaffolding in place.

- [ ] **Step 6: Commit the audio-path scaffolding**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add CMakeLists.txt src/media/DemuxWorker.* src/media/AudioDecoder.* src/media/AudioOutput.* tests/integration/audio-pipeline-checklist.md
git commit -m "feat: scaffold demux and audio pipeline"
```

## Task 8: Add video decode path, render bridge, and sync scheduler

**Files:**
- Create: `src/media/VideoDecoder.h`
- Create: `src/media/VideoDecoder.cpp`
- Create: `src/media/VideoSyncScheduler.h`
- Create: `src/media/VideoSyncScheduler.cpp`
- Create: `src/render/VideoFrameBridge.h`
- Create: `src/render/VideoFrameBridge.cpp`
- Create: `tests/unit/media/VideoSyncSchedulerTests.cpp`

- [ ] **Step 1: Write the failing scheduler test**

```cpp
TEST(VideoSyncSchedulerTests, DropsLateFramesBeyondTolerance) {
    VideoSyncScheduler scheduler;
    EXPECT_TRUE(scheduler.shouldDropFrame(1000, 1100, 50));
}
```

- [ ] **Step 2: Run the scheduler test and verify it fails**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target VideoSyncSchedulerTests
ctest --test-dir build -R VideoSyncSchedulerTests --output-on-failure
```

Expected: Fail because scheduler does not exist.

- [ ] **Step 3: Implement the scheduler**

```cpp
// src/media/VideoSyncScheduler.h
#pragma once

class VideoSyncScheduler {
public:
    bool shouldDropFrame(qint64 framePtsMs, qint64 audioClockMs, qint64 toleranceMs) const;
    qint64 delayBeforePresent(qint64 framePtsMs, qint64 audioClockMs) const;
};
```

```cpp
// src/media/VideoSyncScheduler.cpp
#include "media/VideoSyncScheduler.h"

bool VideoSyncScheduler::shouldDropFrame(qint64 framePtsMs, qint64 audioClockMs, qint64 toleranceMs) const {
    return framePtsMs + toleranceMs < audioClockMs;
}

qint64 VideoSyncScheduler::delayBeforePresent(qint64 framePtsMs, qint64 audioClockMs) const {
    return framePtsMs > audioClockMs ? framePtsMs - audioClockMs : 0;
}
```

- [ ] **Step 4: Implement `VideoFrameBridge`**

```cpp
#pragma once

#include <QObject>
#include <QImage>

class VideoFrameBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QImage currentFrame READ currentFrame NOTIFY frameChanged)
public:
    explicit VideoFrameBridge(QObject *parent = nullptr);
    QImage currentFrame() const;
    void present(const QImage &frame);
signals:
    void frameChanged();
private:
    QImage currentFrame_;
};
```

- [ ] **Step 5: Implement `VideoDecoder` as the matching minimal compile-safe class**

```cpp
#pragma once

class VideoDecoder {
public:
    bool initialize();
};
```

- [ ] **Step 6: Run the scheduler test**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target VideoSyncSchedulerTests
ctest --test-dir build -R VideoSyncSchedulerTests --output-on-failure
```

Expected: Scheduler test passes.

- [ ] **Step 7: Commit the render/sync foundation**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add src/media/VideoDecoder.* src/media/VideoSyncScheduler.* src/render/VideoFrameBridge.* tests/unit/media/VideoSyncSchedulerTests.cpp
git commit -m "feat: add video sync scheduler and frame bridge"
```

## Task 9: Implement `PlayerEngine` and wire open/play/pause/stop

**Files:**
- Create: `src/core/PlayerEngine.h`
- Create: `src/core/PlayerEngine.cpp`
- Modify: `src/controller/PlayerController.h`
- Modify: `src/controller/PlayerController.cpp`
- Create: `tests/unit/core/PlayerEngineTests.cpp`

- [ ] **Step 1: Write the failing engine test**

```cpp
TEST(PlayerEngineTests, OpenMissingFileReturnsFailure) {
    PlayerEngine engine;
    auto result = engine.open("Z:/missing-file.mp4");
    EXPECT_FALSE(result.ok);
}
```

- [ ] **Step 2: Run the engine test and verify it fails**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target PlayerEngineTests
ctest --test-dir build -R PlayerEngineTests --output-on-failure
```

Expected: Fail because engine is not implemented.

- [ ] **Step 3: Implement `PlayerEngine` as the orchestration boundary**

```cpp
// src/core/PlayerEngine.h
#pragma once

#include <QString>

#include "infrastructure/Error.h"

class MediaInfoModel;
class RuntimeLogModel;

class PlayerEngine {
public:
    Error open(const QString &path);
    void play();
    void pause();
    void stop();
    void seek(qint64 positionMs);
};
```

- [ ] **Step 4: Update `PlayerController` to delegate**

Add these members:

```cpp
#include "core/PlayerEngine.h"

private:
    PlayerEngine engine_;
```

And change methods:

```cpp
void PlayerController::play() {
    engine_.play();
    isPlaying_ = true;
    isPaused_ = false;
    emit playbackStateChanged();
}
```

- [ ] **Step 5: Add first real open-file bridge behavior**

Replace `openFile()` with a temporary fixed-path smoke bridge until file dialog handling is added:

```cpp
void PlayerController::openFile() {
    const QString path = "sample.mp4";
    const auto result = engine_.open(path);
    if (!result.ok) {
        runtimeLogModel_.append("error", result.message);
        return;
    }
    currentFile_ = path;
    emit currentFileChanged();
}
```

- [ ] **Step 6: Run the engine test and manual smoke launch**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build --target PlayerEngineTests miniPlayer
ctest --test-dir build -R PlayerEngineTests --output-on-failure
.\build\miniPlayer.exe
```

Expected: Engine unit test passes and manual open attempts emit visible logs.

- [ ] **Step 7: Commit engine orchestration**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add src/core/PlayerEngine.* src/controller tests/unit/core/PlayerEngineTests.cpp
git commit -m "feat: wire controller to playback engine"
```

## Task 10: Implement file picker, metadata presentation, and panel bindings

**Files:**
- Modify: `src/controller/PlayerController.cpp`
- Modify: `ui/qml/panels/CurrentMediaPanel.qml`
- Modify: `ui/qml/panels/MediaInfoPanel.qml`
- Modify: `ui/qml/panels/RuntimeLogPanel.qml`
- Modify: `ui/qml/components/VideoSurfacePane.qml`

- [ ] **Step 1: Add native file selection in the controller**

Use Qt file dialog integration:

```cpp
#include <QFileDialog>

void PlayerController::openFile() {
    const QString path = QFileDialog::getOpenFileName(
        nullptr,
        "Open Media",
        QString(),
        "Video Files (*.mp4 *.mkv *.mov *.avi);;All Files (*.*)");
    if (path.isEmpty()) {
        runtimeLogModel_.append("info", "open file cancelled");
        return;
    }
    const auto result = engine_.open(path);
    if (!result.ok) {
        runtimeLogModel_.append("error", result.message);
        return;
    }
    currentFile_ = path;
    emit currentFileChanged();
}
```

- [ ] **Step 2: Bind the current-media panel to `playerController.currentFile`**

Use:

```qml
Label {
    text: playerController.currentFile.length > 0 ? playerController.currentFile : "No file loaded"
    color: Theme.textPrimary
    wrapMode: Text.WrapAnywhere
}
```

- [ ] **Step 3: Bind `MediaInfoPanel` to `mediaInfoModel`**

```qml
ListView {
    anchors.fill: parent
    model: playerController.mediaInfoModel
    delegate: Column {
        width: ListView.view.width
        Label { text: label; color: Theme.textMuted }
        Label { text: value; color: Theme.textPrimary }
    }
}
```

- [ ] **Step 4: Bind `RuntimeLogPanel` to `runtimeLogModel`**

```qml
ListView {
    anchors.fill: parent
    model: playerController.runtimeLogModel
    delegate: Label {
        width: ListView.view.width
        text: "[" + level + "] " + message
        color: level === "error" ? "#ff8b8b" : (level === "warn" ? "#ffd166" : "#d3d3d3")
        font.family: "Consolas"
    }
}
```

- [ ] **Step 5: Add empty-state overlay text in the video panel**

```qml
Label {
    anchors.centerIn: parent
    visible: playerController.currentFile.length === 0
    text: "Open a local video file to begin"
    color: Theme.textMuted
}
```

- [ ] **Step 6: Build and manually verify metadata/log presentation**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build
.\build\miniPlayer.exe
```

Expected: File picker works, current file path updates, and log panel shows open/cancel/error messages.

- [ ] **Step 7: Commit the presentation wiring**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add src/controller/PlayerController.cpp ui/qml/panels ui/qml/components/VideoSurfacePane.qml
git commit -m "feat: bind media panels and runtime logs"
```

## Task 11: Implement synchronized playback, seek, and stop correctness

**Files:**
- Modify: `src/core/PlayerEngine.cpp`
- Modify: `src/media/DemuxWorker.cpp`
- Modify: `src/media/AudioDecoder.cpp`
- Modify: `src/media/AudioOutput.cpp`
- Modify: `src/media/VideoDecoder.cpp`
- Modify: `src/media/VideoSyncScheduler.cpp`
- Create: `src/media/SeekCoordinator.h`
- Create: `src/media/SeekCoordinator.cpp`
- Create: `tests/integration/sync-regression-checklist.md`

- [ ] **Step 1: Write the synchronization regression checklist**

```md
# Sync Regression Checklist

- Start playback from the beginning
- Confirm audio is audible
- Confirm video is visible
- Watch for at least 30 seconds and confirm no visible drift
- Pause and resume twice
- Seek to 25 percent, 50 percent, and 80 percent
- Confirm playback resumes with audio and video aligned
- Stop and reopen another file
```

- [ ] **Step 2: Implement `SeekCoordinator`**

```cpp
#pragma once

class PacketQueue;
class FrameQueue;

class SeekCoordinator {
public:
    void flush(PacketQueue &audioPackets, PacketQueue &videoPackets, FrameQueue &videoFrames);
};
```

```cpp
#include "media/SeekCoordinator.h"

#include "media/FrameQueue.h"
#include "media/PacketQueue.h"

void SeekCoordinator::flush(PacketQueue &audioPackets, PacketQueue &videoPackets, FrameQueue &videoFrames) {
    audioPackets.clear();
    videoPackets.clear();
    videoFrames.clear();
}
```

- [ ] **Step 3: Replace bootstrap playback methods with real pipeline control**

Implement this minimum behavior inside `PlayerEngine`:

```cpp
Error PlayerEngine::open(const QString &path) {
    session_.close();
    audioPackets_.clear();
    videoPackets_.clear();
    videoFrames_.clear();
    auto result = session_.open(path);
    if (!result.ok) {
        return result;
    }
    currentPath_ = path;
    currentMediaInfo_ = mediaInfoExtractor_.extract(path, session_.formatContext());
    Logger::instance().info("media opened: " + path);
    return Error::success();
}

void PlayerEngine::seek(qint64 positionMs) {
    if (!session_.isOpen()) {
        return;
    }
    seekCoordinator_.flush(audioPackets_, videoPackets_, videoFrames_);
    av_seek_frame(session_.formatContext(), -1, positionMs * 1000, AVSEEK_FLAG_BACKWARD);
    audioClock_.update(positionMs);
    Logger::instance().info("seek completed to " + QString::number(positionMs) + " ms");
}
```

- [ ] **Step 4: Update controller timeline state from playback callbacks**

Add engine-to-controller callbacks or signals so:

```cpp
positionMs_ = audioClockValue;
emit timelineChanged();
```

is driven by actual playback progression rather than UI-only writes.

- [ ] **Step 5: Build and manually execute the sync checklist**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build
.\build\miniPlayer.exe
```

Expected: Manual verification completes the checklist without obvious A/V drift or broken seek recovery.

- [ ] **Step 6: Commit synchronized playback**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add src/core/PlayerEngine.cpp src/media/DemuxWorker.cpp src/media/AudioDecoder.cpp src/media/AudioOutput.cpp src/media/VideoDecoder.cpp src/media/VideoSyncScheduler.cpp src/media/SeekCoordinator.* tests/integration/sync-regression-checklist.md
git commit -m "feat: implement synchronized playback seek and stop flows"
```

## Task 12: Final polish, validation, and documentation

**Files:**
- Modify: `docs/README.md`
- Modify: `docs/superpowers/specs/2026-04-29-miniplayer-design.md`
- Create: `docs/testing/manual-validation.md`

- [ ] **Step 1: Write the manual validation document**

```md
# Manual Validation

## Playback
- Open a supported local video
- Confirm audio output starts
- Confirm video appears
- Pause and resume
- Stop and reopen

## Synchronization
- Observe 30 seconds of playback
- Seek to multiple positions
- Confirm audio remains the master timeline

## UI
- Verify dark theme and panel hierarchy
- Verify log readability
- Verify media info readability
```

- [ ] **Step 2: Update `docs/README.md` with actual dependency setup and run instructions**

Include:

```md
## Run

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake -S . -B build
cmake --build build
.\build\miniPlayer.exe
```
```

- [ ] **Step 3: Run the final verification commands**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: Automated tests pass and remaining validation is documented as manual playback verification.

- [ ] **Step 4: Commit the docs and validation artifacts**

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git add docs
git commit -m "docs: add validation and runbook for miniplayer"
```

- [ ] **Step 5: Tag release checkpoint locally**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
git tag release-0.1.0
```

Expected: A local release checkpoint exists for the first synchronized-player milestone.
