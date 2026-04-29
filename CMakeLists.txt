cmake_minimum_required(VERSION 3.24)

project(miniPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(MINIPLAYER_ENABLE_FFMPEG "Enable FFmpeg-backed media integration" OFF)
set(MINIPLAYER_FFMPEG_PROVIDER "auto" CACHE STRING "FFmpeg discovery strategy: auto or custom-cache")
set_property(CACHE MINIPLAYER_FFMPEG_PROVIDER PROPERTY STRINGS auto custom-cache)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Multimedia)

qt_standard_project_setup()

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(miniPlayer
    src/app/main.cpp
    src/app/AppBootstrap.cpp
)

qt_add_qml_module(miniPlayer
    URI MiniPlayer
    VERSION 1.0
    QML_FILES
        ui/qml/Main.qml
        ui/qml/themes/Theme.qml
        ui/qml/components/TopBar.qml
        ui/qml/components/PlaybackControlBar.qml
        ui/qml/components/VideoSurfacePane.qml
        ui/qml/panels/CurrentMediaPanel.qml
        ui/qml/panels/MediaInfoPanel.qml
        ui/qml/panels/RuntimeLogPanel.qml
)

target_include_directories(miniPlayer PRIVATE
    src
)

target_link_libraries(miniPlayer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Multimedia
)

if(MINIPLAYER_ENABLE_FFMPEG)
    if(MINIPLAYER_FFMPEG_PROVIDER STREQUAL "custom-cache")
        list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
        find_package(FFmpeg QUIET COMPONENTS avformat avcodec avutil swresample swscale)
    else()
        find_package(FFmpeg QUIET CONFIG COMPONENTS avformat avcodec avutil swresample swscale)
        if(NOT FFmpeg_FOUND)
            find_package(FFMPEG QUIET)
        endif()
    endif()

    if(FFmpeg_FOUND OR FFMPEG_FOUND)
        message(STATUS "FFmpeg was discovered for future media tasks. Bootstrap stage keeps the app target free of FFmpeg link dependencies.")
    else()
        message(STATUS "FFmpeg discovery was requested but no package was found. Bootstrap stage continues because FFmpeg is not linked yet.")
    endif()
else()
    message(STATUS "FFmpeg integration is disabled for the bootstrap skeleton. Enable MINIPLAYER_ENABLE_FFMPEG once media pipeline work begins.")
endif()
