cmake_minimum_required(VERSION 3.24)

project(MeloBox VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(MELOBOX_ENABLE_FFMPEG "Enable FFmpeg-backed media integration" ON)
option(MELOBOX_BUILD_TESTS "Build MeloBox test executables" OFF)
option(MELOBOX_BUNDLE_MPV "Bundle libmpv runtime in packaged builds" ON)
option(MELOBOX_ENABLE_RUNTIME_DEPLOYMENT "Run Qt/vcpkg runtime deployment after building" OFF)
set(MELOBOX_FFMPEG_PROVIDER "auto" CACHE STRING "FFmpeg discovery strategy: auto or custom-cache")
set_property(CACHE MELOBOX_FFMPEG_PROVIDER PROPERTY STRINGS auto custom-cache)
set(MELOBOX_APP_VERSION "1.0" CACHE STRING "MeloBox application version shown in the UI and update checks")
set(MELOBOX_UPDATE_OWNER "" CACHE STRING "GitHub owner used by the built-in updater")
set(MELOBOX_UPDATE_REPO "" CACHE STRING "GitHub repository used by the built-in updater")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Multimedia Widgets Network Test)
find_package(PNG REQUIRED)
find_package(OpenSSL REQUIRED)

qt_standard_project_setup()
if(MELOBOX_BUILD_TESTS)
    enable_testing()
endif()

if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()

add_library(miniPlayerInfrastructure STATIC
    src/infrastructure/Logger.cpp
)

target_include_directories(miniPlayerInfrastructure PUBLIC
    src
)

target_link_libraries(miniPlayerInfrastructure PUBLIC
    Qt6::Core
)

add_library(miniPlayerModels STATIC
    src/models/RuntimeLogModel.cpp
    src/models/MediaInfoModel.cpp
    src/models/PlaylistModel.cpp
    src/models/VideoSearchModel.cpp
    src/models/ApiSiteModel.cpp
    src/models/DownloadModel.cpp
    src/models/DmghgAnimeModel.cpp
    src/media/DmghgClient.cpp
    src/models/BeeVideoModel.cpp
    src/models/BeeScheduleModel.cpp
    src/media/BeeClient.cpp
)

target_include_directories(miniPlayerModels PUBLIC
    src
)

target_link_libraries(miniPlayerModels PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    OpenSSL::Crypto
    miniPlayerInfrastructure
)

add_library(miniPlayerMpv STATIC
    src/mpv/MpvBackend.cpp
)

target_include_directories(miniPlayerMpv PUBLIC
    src
)

target_link_libraries(miniPlayerMpv PUBLIC
    Qt6::Core
    miniPlayerInfrastructure
)

foreach(miniPlayerCppTarget IN ITEMS miniPlayerInfrastructure miniPlayerModels miniPlayerMpv)
    if(MSVC)
        target_compile_options(${miniPlayerCppTarget} PRIVATE /utf-8)
    endif()
endforeach()

qt_add_executable(MeloBox
    src/app/main.cpp
    src/app/AppBootstrap.cpp
    src/controller/PlayerController.cpp
    src/media/ImageCacheService.cpp
)

if(WIN32)
    set_target_properties(MeloBox PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    target_sources(MeloBox PRIVATE
        resources/windows/appicon.rc
    )

    add_custom_command(TARGET MeloBox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/appicon.ico"
            "$<TARGET_FILE_DIR:MeloBox>/appicon.ico"
        COMMENT "Deploying tray icon"
    )
endif()

qt_add_qml_module(MeloBox
    URI MiniPlayer
    VERSION 1.0
    NO_IMPORT_SCAN
    QML_FILES
        ui/qml/Main.qml
        ui/qml/themes/Theme.qml
        ui/qml/AppStrings.qml
        ui/qml/components/TopBar.qml
        ui/qml/components/PlaybackControlBar.qml
        ui/qml/components/VideoSurfacePane.qml
        ui/qml/components/MediaDrawer.qml
        ui/qml/components/LogDrawer.qml
        ui/qml/components/NavigationSidebar.qml
        ui/qml/components/SearchView.qml
        ui/qml/components/SiteManagementView.qml
        ui/qml/components/VideoDetailPanel.qml
        ui/qml/components/DownloadView.qml
        ui/qml/components/MusicView.qml
        ui/qml/components/HistoryView.qml
        ui/qml/components/MemeView.qml
        ui/qml/components/VoiceView.qml
        ui/qml/components/HotNewsView.qml
        ui/qml/components/ImageView.qml
        ui/qml/components/ShortVideoView.qml
        ui/qml/components/ShortVideoOverlay.qml
        ui/qml/components/MediaBrowserWindow.qml
        ui/qml/components/HomeView.qml
        ui/qml/components/VideoPlayerView.qml
        ui/qml/components/AnimeView.qml
        ui/qml/components/BeeView.qml
        ui/qml/components/DanmakuOverlay.qml
        ui/qml/components/ApiManagerDialog.qml
        ui/qml/panels/MediaPanel.qml
        ui/qml/panels/CurrentMediaPanel.qml
        ui/qml/panels/MediaInfoPanel.qml
        ui/qml/panels/RuntimeLogPanel.qml
    RESOURCES
        ui/qml/assets/appicon.png
)

target_include_directories(MeloBox PRIVATE
    src
)

if(MSVC)
    target_compile_options(MeloBox PRIVATE /utf-8)
endif()

target_link_libraries(MeloBox PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Multimedia
    Qt6::Widgets
    Qt6::Network
    PNG::PNG
    miniPlayerInfrastructure
    miniPlayerModels
    miniPlayerMpv
)

target_compile_definitions(MeloBox PRIVATE
    MELOBOX_APP_VERSION="${MELOBOX_APP_VERSION}"
    MELOBOX_UPDATE_OWNER="${MELOBOX_UPDATE_OWNER}"
    MELOBOX_UPDATE_REPO="${MELOBOX_UPDATE_REPO}"
)

set(MELOBOX_MPV_RUNTIME_DLL
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/extracted/libmpv-2.dll"
    CACHE FILEPATH "Optional libmpv runtime DLL copied next to MeloBox.exe"
)

set(MELOBOX_INNO_SETUP_COMPILER "" CACHE FILEPATH "Path to ISCC.exe used by the package_installer target")
if(WIN32 AND NOT MELOBOX_INNO_SETUP_COMPILER)
    foreach(meloBoxInnoSetupCandidate IN ITEMS
        "D:/software/InnoSetup6/ISCC.exe"
        "C:/Program Files (x86)/Inno Setup 6/ISCC.exe"
        "C:/Program Files/Inno Setup 6/ISCC.exe"
    )
        if(EXISTS "${meloBoxInnoSetupCandidate}")
            set(MELOBOX_INNO_SETUP_COMPILER "${meloBoxInnoSetupCandidate}" CACHE FILEPATH "Path to ISCC.exe used by the package_installer target" FORCE)
            break()
        endif()
    endforeach()
endif()

set(MELOBOX_PACKAGE_CLEAN_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/dist/MeloBox/MeloBox.log"
    "${CMAKE_CURRENT_SOURCE_DIR}/dist/MeloBox/miniPlayer.exe"
)

if(NOT MELOBOX_BUNDLE_MPV)
    list(APPEND MELOBOX_PACKAGE_CLEAN_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/dist/MeloBox/libmpv-2.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/dist/MeloBox/mpv-2.dll"
    )
endif()

add_custom_target(package_installer ALL
    COMMAND ${CMAKE_COMMAND}
        -DMELOBOX_BUILD_CONFIG=$<CONFIG>
        -DMELOBOX_BUILD_OUTPUT_DIR=$<TARGET_FILE_DIR:MeloBox>
        -DMELOBOX_INNO_SETUP_COMPILER=${MELOBOX_INNO_SETUP_COMPILER}
        -DMELOBOX_INSTALLER_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/installer/melobox.iss
        -DMELOBOX_PACKAGE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/dist/MeloBox
        -DMELOBOX_APP_VERSION=${MELOBOX_APP_VERSION}
        -DMELOBOX_BUNDLE_MPV=${MELOBOX_BUNDLE_MPV}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageInstaller.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS MeloBox
    COMMENT "Compiling MeloBox installer"
    VERBATIM
)

if(MELOBOX_BUNDLE_MPV AND EXISTS "${MELOBOX_MPV_RUNTIME_DLL}")
    add_custom_command(TARGET MeloBox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MELOBOX_MPV_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:MeloBox>/libmpv-2.dll"
        COMMENT "Deploying libmpv runtime"
    )
endif()

# OpenSSL runtime DLL required by DmghgClient (RSA/AES response decryption).
if(WIN32 AND TARGET OpenSSL::Crypto)
    add_custom_command(TARGET MeloBox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:OpenSSL::Crypto>"
            "$<TARGET_FILE_DIR:MeloBox>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:OpenSSL::SSL>"
            "$<TARGET_FILE_DIR:MeloBox>"
        COMMENT "Deploying OpenSSL runtime"
    )
endif()

# Dmghg main-gateway responses use the bundled RSA key for decryption.
add_custom_command(TARGET MeloBox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/dmghg/dmghg_key.pem"
        "$<TARGET_FILE_DIR:MeloBox>/dmghg_key.pem"
    COMMENT "Deploying Dmghg response decryption key"
)

if(MELOBOX_ENABLE_FFMPEG)
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
    if(MELOBOX_FFMPEG_PROVIDER STREQUAL "custom-cache")
        find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swresample swscale)
    else()
        # The bundled FindFFmpeg module only searches for headers/libraries when
        # discovery is explicitly enabled. This is required for vcpkg installs,
        # where FFmpeg is present under the active toolchain prefix.
        set(FFMPEG_ENABLE_DISCOVERY ON CACHE BOOL
            "Allow FindFFmpeg.cmake to search under FFMPEG_ROOT" FORCE)
        if(NOT FFMPEG_ROOT AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            set(FFMPEG_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
                CACHE PATH "Root directory of the FFmpeg installation" FORCE)
        endif()
        # vcpkg's FFmpeg port ships canonical include/lib paths but its package
        # wrapper can bypass the normal find_* search used by our module. Seed
        # the cache explicitly when those files are present.
        if(FFMPEG_ROOT AND EXISTS "${FFMPEG_ROOT}/include/libavformat/avformat.h")
            set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include" CACHE PATH
                "Directory containing FFmpeg headers" FORCE)
            foreach(_ffmpeg_component IN ITEMS avformat avcodec avutil swresample swscale)
                string(TOUPPER "${_ffmpeg_component}" _ffmpeg_component_upper)
                set(_ffmpeg_library "${FFMPEG_ROOT}/lib/${_ffmpeg_component}.lib")
                if(EXISTS "${_ffmpeg_library}")
                    set(FFMPEG_${_ffmpeg_component_upper}_LIBRARY "${_ffmpeg_library}" CACHE FILEPATH
                        "Path to the ${_ffmpeg_component} library" FORCE)
                endif()
            endforeach()
        endif()
        find_package(FFmpeg QUIET COMPONENTS avformat avcodec avutil swresample swscale)
        if(NOT FFmpeg_FOUND)
            find_package(FFMPEG QUIET)
        endif()
    endif()

    set(miniPlayerFfmpegTargets
        FFmpeg::avformat
        FFmpeg::avcodec
        FFmpeg::avutil
        FFmpeg::swresample
        FFmpeg::swscale
    )

    set(miniPlayerHasFfmpegTargets TRUE)
    foreach(miniPlayerFfmpegTarget IN LISTS miniPlayerFfmpegTargets)
        if(NOT TARGET ${miniPlayerFfmpegTarget})
            set(miniPlayerHasFfmpegTargets FALSE)
        endif()
    endforeach()

    if((FFmpeg_FOUND OR FFMPEG_FOUND) AND miniPlayerHasFfmpegTargets)
        message(STATUS "FFmpeg was discovered for media task 5 and imported targets are available.")

        add_library(miniPlayerMediaPipeline STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core/PlayerEngine.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core/MediaSession.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/MediaInfoExtractor.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/PacketQueue.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/FrameQueue.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/AudioClock.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/DemuxWorker.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/AudioDecoder.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/AudioOutput.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/VideoDecoder.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/VideoSyncScheduler.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/VideoDecodeWorker.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/ImageHlsWorker.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/PcmAudioPlayer.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/ThumbnailWorker.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/SubtitleDecoder.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/HlsPlaylistFilter.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/HlsPlaylistProxy.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/SoftwareDecoderFactory.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/HardwareDecoderFactory.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/render/VideoFrameBridge.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/render/ThumbnailImageProvider.cpp
        )

        target_include_directories(miniPlayerMediaPipeline PUBLIC
            src
        )

        if(MSVC)
            target_compile_options(miniPlayerMediaPipeline PRIVATE /utf-8)
        endif()

        target_link_libraries(miniPlayerMediaPipeline PUBLIC
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::Qml
            Qt6::Multimedia
            Qt6::Network
            PNG::PNG
            miniPlayerInfrastructure
            miniPlayerModels
            ${miniPlayerFfmpegTargets}
        )

        target_link_libraries(MeloBox PRIVATE
            miniPlayerMediaPipeline
        )

        if(MELOBOX_BUILD_TESTS)
        add_executable(miniPlayerMediaTests
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/media/MediaSessionTests.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core/MediaSession.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/HlsPlaylistFilter.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/HlsPlaylistProxy.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/MediaInfoExtractor.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/ImageHlsWorker.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/PcmAudioPlayer.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/FrameQueue.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure/Logger.cpp
        )

        target_include_directories(miniPlayerMediaTests PRIVATE
            src
        )

        target_link_libraries(miniPlayerMediaTests PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Test
            Qt6::Multimedia
            Qt6::Network
            ${miniPlayerFfmpegTargets}
            PNG::PNG
        )

        add_test(NAME miniPlayerMediaTests COMMAND $<TARGET_FILE:miniPlayerMediaTests>)

        add_executable(miniPlayerCoreTests
            src/common/constant.h
            tests/unit/models/ModelTests.cpp
            src/controller/PlayerController.cpp
            src/mpv/MpvBackend.cpp
            src/core/PlayerEngine.cpp
            src/core/MediaSession.cpp
            src/media/MediaInfoExtractor.cpp
            src/media/PacketQueue.cpp
            src/media/FrameQueue.cpp
            src/media/AudioClock.cpp
            src/media/DemuxWorker.cpp
            src/media/AudioDecoder.cpp
            src/media/AudioOutput.cpp
            src/media/VideoDecoder.cpp
            src/media/VideoSyncScheduler.cpp
            src/media/VideoDecodeWorker.cpp
            src/media/ImageHlsWorker.cpp
            src/media/PcmAudioPlayer.cpp
            src/media/ThumbnailWorker.cpp
            src/media/SubtitleDecoder.cpp
            src/media/HlsPlaylistFilter.cpp
            src/media/HlsPlaylistProxy.cpp
            src/media/ImageCacheService.cpp
            src/render/VideoFrameBridge.cpp
            src/models/RuntimeLogModel.cpp
            src/models/MediaInfoModel.cpp
            src/models/PlaylistModel.cpp
            src/models/VideoSearchModel.cpp
            src/models/ApiSiteModel.cpp
            src/models/DownloadModel.cpp
            src/models/DmghgAnimeModel.cpp
            src/media/DmghgClient.cpp
            src/models/BeeVideoModel.cpp
            src/models/BeeScheduleModel.cpp
            src/media/BeeClient.cpp
            src/infrastructure/Logger.cpp
        )

        target_include_directories(miniPlayerCoreTests PRIVATE
            src
        )

        target_link_libraries(miniPlayerCoreTests PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Quick
            Qt6::Test
            Qt6::Multimedia
            Qt6::Widgets
            Qt6::Network
            OpenSSL::Crypto
            ${miniPlayerFfmpegTargets}
            PNG::PNG
        )

        add_test(NAME miniPlayerCoreTests COMMAND $<TARGET_FILE:miniPlayerCoreTests>)

        add_executable(miniPlayerPipelineTests
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/media/PrimitivesTests.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/PacketQueue.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/FrameQueue.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/AudioClock.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure/Logger.cpp
        )

        target_include_directories(miniPlayerPipelineTests PRIVATE
            src
        )

        target_link_libraries(miniPlayerPipelineTests PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Test
            ${miniPlayerFfmpegTargets}
        )

        add_test(NAME miniPlayerPipelineTests COMMAND $<TARGET_FILE:miniPlayerPipelineTests>)

        add_executable(miniPlayerSyncTests
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/media/VideoSyncSchedulerTests.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/VideoSyncScheduler.cpp
        )

        target_include_directories(miniPlayerSyncTests PRIVATE
            src
        )

        target_link_libraries(miniPlayerSyncTests PRIVATE
            Qt6::Core
            Qt6::Test
        )

        add_test(NAME miniPlayerSyncTests COMMAND $<TARGET_FILE:miniPlayerSyncTests>)

        add_executable(miniPlayerDecoderTests
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/media/DecoderTests.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/AudioDecoder.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/media/VideoDecoder.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure/Logger.cpp
        )

        target_include_directories(miniPlayerDecoderTests PRIVATE
            src
        )

        target_link_libraries(miniPlayerDecoderTests PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Test
            ${miniPlayerFfmpegTargets}
        )

        add_test(NAME miniPlayerDecoderTests COMMAND $<TARGET_FILE:miniPlayerDecoderTests>)
        endif()
    else()
        message(STATUS "FFmpeg discovery was requested but imported targets are unavailable. Media task 5 targets are skipped.")
    endif()
else()
    message(STATUS "FFmpeg integration is disabled for the bootstrap skeleton. Enable MELOBOX_ENABLE_FFMPEG once media pipeline work begins.")
endif()


if (MSVC AND MELOBOX_ENABLE_RUNTIME_DEPLOYMENT)
    # Depending on the vcpkg toolchain version, _VCPKG_INSTALLED_DIR points at
    # either the "installed" root or the per-triplet directory. Normalize it
    # here so Qt plugins/tools are always resolved from the triplet dir.
    set(_miniPlayerQtPluginBase "")
    if(DEFINED _VCPKG_INSTALLED_DIR)
        if(_VCPKG_INSTALLED_DIR MATCHES "/${VCPKG_TARGET_TRIPLET}$" OR NOT DEFINED VCPKG_TARGET_TRIPLET)
            set(_miniPlayerQtPluginBase "${_VCPKG_INSTALLED_DIR}")
        else()
            set(_miniPlayerQtPluginBase "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        endif()
    elseif(DEFINED CMAKE_PREFIX_PATH)
        set(_miniPlayerQtPluginBase "${CMAKE_PREFIX_PATH}")
    endif()

    if(_miniPlayerQtPluginBase)
        set(_miniPlayerQtPluginDir "${_miniPlayerQtPluginBase}/$<$<CONFIG:Debug>:debug/>/Qt6/plugins")
        set(_miniPlayerQtBinDir "${_miniPlayerQtPluginBase}/$<$<CONFIG:Debug>:debug/>/bin")
        set(_miniPlayerWinDeployQt "")
        if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND EXISTS "${_miniPlayerQtPluginBase}/tools/Qt6/bin/windeployqt.debug.bat")
            set(_miniPlayerWinDeployQt "${_miniPlayerQtPluginBase}/tools/Qt6/bin/windeployqt.debug.bat")
        else()
            foreach(_miniPlayerWinDeployQtCandidate
                    "${_miniPlayerQtPluginBase}/tools/Qt6/bin/windeployqt.exe")
                if(EXISTS "${_miniPlayerWinDeployQtCandidate}")
                    set(_miniPlayerWinDeployQt "${_miniPlayerWinDeployQtCandidate}")
                    break()
                endif()
            endforeach()
        endif()
        if(NOT _miniPlayerWinDeployQt)
            find_program(_miniPlayerWinDeployQt
                NAMES windeployqt windeployqt.exe
                HINTS "${_miniPlayerQtPluginBase}/tools/Qt6/bin"
            )
        endif()
        message(STATUS "windeployqt resolved to: ${_miniPlayerWinDeployQt}")

        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                # Deploy QML import modules so the build output can run directly.
                COMMAND "${_miniPlayerWinDeployQt}"
                    "$<IF:$<CONFIG:Debug>,--debug,--release>"
                    "--no-translations"
                    "--qmldir"
                    "${CMAKE_CURRENT_SOURCE_DIR}/ui/qml"
                    "$<TARGET_FILE:${PROJECT_NAME}>"

                # Keep only the Basic Qt Quick Controls style used by the app.
                # windeployqt deploys every style by default, which adds many MB
                # of unused QML and DLLs to the installed footprint.
                COMMAND ${CMAKE_COMMAND} -E rm -rf
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/Controls/Fusion"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/Controls/Material"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/Controls/Imagine"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/Controls/Universal"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/Controls/FluentWinUI3"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/Controls/Windows"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/Dialogs"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/LocalStorage"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/NativeStyle"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/Particles"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/Shapes"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQuick/VectorImage"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qml/QtQml/XmlListModel"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qmltooling"
                COMMAND ${CMAKE_COMMAND} -E rm -f
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickControls2Fusion.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickControls2FusionStyleImpl.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickControls2Material.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickControls2MaterialStyleImpl.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickControls2Imagine.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickControls2ImagineStyleImpl.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickControls2Universal.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickControls2UniversalStyleImpl.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickControls2FluentWinUI3StyleImpl.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickControls2WindowsStyleImpl.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickDialogs2.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickDialogs2QuickImpl.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickDialogs2Utils.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickParticles.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickShapes.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickShapesDesignHelpers.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickVectorImage.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickVectorImageGenerator.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickVectorImageHelpers.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Qt6QuickTest.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/dxcompiler.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/dxil.dll"

                # platforms
                COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtPluginDir}/platforms/qwindows$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/platforms"

                # imageformats
                COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/imageformats

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtPluginDir}/imageformats/qjpeg$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/imageformats"

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtPluginDir}/imageformats/qgif$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/imageformats"

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtPluginDir}/imageformats/qwebp$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/imageformats"

                COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/imageformats

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtPluginDir}/imageformats/qwebp$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/imageformats"

                # styles
                COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/styles

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtPluginDir}/styles/qmodernwindowsstyle$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/styles"

                # networkinformation
                COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/networkinformation

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtPluginDir}/networkinformation/qnetworklistmanager$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/networkinformation"

                # tls
                COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/tls

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtPluginDir}/tls/qschannelbackend$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/tls"

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtPluginDir}/tls/qopensslbackend$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/tls"

                # multimedia - use Windows native backend only (avoid FFmpeg conflict)
                COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/multimedia

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtPluginDir}/multimedia/windowsmediaplugin$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/multimedia"

                # Remove optional plugins windeployqt may have copied but the app
                # does not use. Keep qjpeg/qgif and both TLS backends; BeeWeb
                # requires OpenSSL for its TLS renegotiation behavior.
                COMMAND ${CMAKE_COMMAND} -E rm -rf
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/generic"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/iconengines"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/styles"
                COMMAND ${CMAKE_COMMAND} -E rm -f
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/imageformats/qico$<$<CONFIG:Debug>:d>.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/imageformats/qsvg$<$<CONFIG:Debug>:d>.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/multimedia/ffmpegmediaplugin$<$<CONFIG:Debug>:d>.dll"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/tls/qcertonlybackend$<$<CONFIG:Debug>:d>.dll"

                # libpng
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtPluginBase}/$<$<CONFIG:Debug>:debug/>/bin/libpng16$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"

                # Image plugin runtime dependencies. qjpeg/qwebp are loaded at
                # runtime, so windeployqt cannot always discover these DLLs.
                # Use the configuration-specific bin directory to avoid mixing
                # Debug plugins with Release dependencies.
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtBinDir}/jpeg62.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/jpeg62.dll"

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtBinDir}/libwebp.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libwebp.dll"

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtBinDir}/libwebpdecoder.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libwebpdecoder.dll"

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtBinDir}/libwebpdemux.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libwebpdemux.dll"

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtBinDir}/libwebpmux.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libwebpmux.dll"

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtBinDir}/libsharpyuv.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libsharpyuv.dll"

                # Runtime DLLs required transitively by Qt/FFmpeg on vcpkg.
                # Keep every source configuration-specific; copying Release
                # binaries into a Debug output makes Qt plugins fail to load.
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtBinDir}/brotlicommon.dll"
                "${_miniPlayerQtBinDir}/brotlidec.dll"
                "${_miniPlayerQtBinDir}/bz2$<$<CONFIG:Debug>:d>.dll"
                "${_miniPlayerQtBinDir}/double-conversion.dll"
                "${_miniPlayerQtBinDir}/freetype$<$<CONFIG:Debug>:d>.dll"
                "${_miniPlayerQtBinDir}/harfbuzz.dll"
                "${_miniPlayerQtBinDir}/libcrypto-3-x64.dll"
                "${_miniPlayerQtBinDir}/libssl-3-x64.dll"
                "${_miniPlayerQtBinDir}/md4c.dll"
                "${_miniPlayerQtBinDir}/pcre2-16$<$<CONFIG:Debug>:d>.dll"
                "${_miniPlayerQtBinDir}/zlib$<$<CONFIG:Debug>:d>1.dll"
                "${_miniPlayerQtBinDir}/zstd.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"

                # Qt Quick Controls QML plugins may not be discovered by
                # windeployqt when imports come from the generated qrc module.
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_miniPlayerQtBinDir}/Qt6QuickControls2.dll"
                "${_miniPlayerQtBinDir}/Qt6QuickTemplates2.dll"
                "${_miniPlayerQtBinDir}/Qt6QuickControls2Basic.dll"
                "${_miniPlayerQtBinDir}/Qt6QuickControls2BasicStyleImpl.dll"
                "${_miniPlayerQtBinDir}/Qt6QuickControls2Impl.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"

                # MSVC runtime side-by-side DLLs required by the vcpkg Qt build.
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$ENV{SystemRoot}/System32/msvcp140_1.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/MSVCP140_1.dll"

                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$ENV{SystemRoot}/System32/msvcp140_2.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/MSVCP140_2.dll"

                # Keep Qt's plugin lookup self-contained when the application is
                # launched from an installed directory with a different Qt setup.
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/qt.conf"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/qt.conf"
        )
    endif()
endif()
