cmake_minimum_required(VERSION 3.24)

project(miniPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(MINIPLAYER_ENABLE_FFMPEG "Enable FFmpeg-backed media integration" OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Multimedia)

qt_standard_project_setup()

qt_add_executable(miniPlayer
    src/app/main.cpp
    src/app/AppBootstrap.cpp
)

qt_add_qml_module(miniPlayer
    URI MiniPlayer
    VERSION 1.0
    QML_FILES
        ui/qml/Main.qml
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
    find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swresample swscale)

    target_link_libraries(miniPlayer PRIVATE
        FFmpeg::avformat
        FFmpeg::avcodec
        FFmpeg::avutil
        FFmpeg::swresample
        FFmpeg::swscale
    )
else()
    message(STATUS "FFmpeg integration is disabled for the bootstrap skeleton. Enable MINIPLAYER_ENABLE_FFMPEG once media pipeline work begins.")
endif()
