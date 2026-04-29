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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Multimedia Test)

qt_standard_project_setup()
enable_testing()

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(miniPlayer
    src/app/main.cpp
    src/app/AppBootstrap.cpp
    src/controller/PlayerController.cpp
    src/models/RuntimeLogModel.cpp
    src/models/MediaInfoModel.cpp
    src/infrastructure/Logger.cpp
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

add_executable(miniPlayerCoreTests
    src/common/constant.h
    tests/unit/models/ModelTests.cpp
    src/controller/PlayerController.cpp
    src/models/RuntimeLogModel.cpp
    src/models/MediaInfoModel.cpp
    src/infrastructure/Logger.cpp

)

target_include_directories(miniPlayerCoreTests PRIVATE
    src
)

target_link_libraries(miniPlayerCoreTests PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME miniPlayerCoreTests COMMAND $<TARGET_FILE:miniPlayerCoreTests>)

if(MINIPLAYER_ENABLE_FFMPEG)
    if(MINIPLAYER_FFMPEG_PROVIDER STREQUAL "custom-cache")
        list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
        find_package(FFmpeg QUIET COMPONENTS avformat avcodec avutil)
    else()
        find_package(FFMPEG QUIET)
    endif()

    set(miniPlayerFfmpegTargets
        FFmpeg::avformat
        FFmpeg::avcodec
        FFmpeg::avutil
    )

    set(miniPlayerHasFfmpegTargets TRUE)
    foreach(miniPlayerFfmpegTarget IN LISTS miniPlayerFfmpegTargets)
        if(NOT TARGET ${miniPlayerFfmpegTarget})
            set(miniPlayerHasFfmpegTargets FALSE)
        endif()
    endforeach()

    if((FFmpeg_FOUND OR FFMPEG_FOUND) AND miniPlayerHasFfmpegTargets)
        message(STATUS "FFmpeg was discovered for media task 5 and imported targets are available.")

        target_sources(miniPlayer PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core/MediaSession.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/MediaInfoExtractor.cpp
        )

        target_link_libraries(miniPlayer PRIVATE
            ${miniPlayerFfmpegTargets}
        )

        add_executable(miniPlayerMediaTests
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/media/MediaSessionTests.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core/MediaSession.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/MediaInfoExtractor.cpp
        )

        target_include_directories(miniPlayerMediaTests PRIVATE
            src
        )

        target_link_libraries(miniPlayerMediaTests PRIVATE
            Qt6::Core
            Qt6::Test
            ${miniPlayerFfmpegTargets}
        )

        add_test(NAME miniPlayerMediaTests COMMAND $<TARGET_FILE:miniPlayerMediaTests>)
    else()
        message(STATUS "FFmpeg discovery was requested but imported targets are unavailable. Media task 5 targets are skipped.")
    endif()
else()
    message(STATUS "FFmpeg integration is disabled for the bootstrap skeleton. Enable MINIPLAYER_ENABLE_FFMPEG once media pipeline work begins.")
endif()


if (MSVC)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms

            COMMAND ${CMAKE_COMMAND} -E copy
            #            "${_VCPKG_INSTALLED_DIR}/x64-windows/tools/Qt6/plugins/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
            "${_VCPKG_INSTALLED_DIR}/x64-windows/$<$<CONFIG:Debug>:debug/>/Qt6/plugins/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms"
    )
endif()