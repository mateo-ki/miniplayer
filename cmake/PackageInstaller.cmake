if(NOT DEFINED MELOBOX_BUILD_CONFIG)
    message(FATAL_ERROR "MELOBOX_BUILD_CONFIG is required.")
endif()

if(NOT MELOBOX_BUILD_CONFIG STREQUAL "Release")
    message(STATUS "Skipping installer packaging for ${MELOBOX_BUILD_CONFIG} configuration.")
    return()
endif()

if(NOT DEFINED MELOBOX_BUILD_OUTPUT_DIR OR NOT IS_DIRECTORY "${MELOBOX_BUILD_OUTPUT_DIR}")
    message(FATAL_ERROR "MELOBOX_BUILD_OUTPUT_DIR is missing or is not a directory.")
endif()

if(NOT DEFINED MELOBOX_INSTALLER_SCRIPT)
    message(FATAL_ERROR "MELOBOX_INSTALLER_SCRIPT is required.")
endif()

if(NOT DEFINED MELOBOX_PACKAGE_DIR OR MELOBOX_PACKAGE_DIR STREQUAL "")
    message(FATAL_ERROR "MELOBOX_PACKAGE_DIR is required.")
endif()

if(NOT DEFINED MELOBOX_APP_VERSION OR MELOBOX_APP_VERSION STREQUAL "")
    message(FATAL_ERROR "MELOBOX_APP_VERSION is required.")
endif()

if(NOT DEFINED MELOBOX_INNO_SETUP_COMPILER OR NOT EXISTS "${MELOBOX_INNO_SETUP_COMPILER}")
    message(FATAL_ERROR "ISCC.exe was not found. Set MELOBOX_INNO_SETUP_COMPILER to the Inno Setup compiler path.")
endif()

file(REMOVE_RECURSE "${MELOBOX_PACKAGE_DIR}")
file(MAKE_DIRECTORY "${MELOBOX_PACKAGE_DIR}")
file(COPY "${MELOBOX_BUILD_OUTPUT_DIR}/"
    DESTINATION "${MELOBOX_PACKAGE_DIR}"
    PATTERN "*.log" EXCLUDE
)

if(DEFINED MELOBOX_BUNDLE_MPV AND NOT MELOBOX_BUNDLE_MPV)
    file(REMOVE
        "${MELOBOX_PACKAGE_DIR}/libmpv-2.dll"
        "${MELOBOX_PACKAGE_DIR}/mpv-2.dll"
    )
endif()

file(GLOB meloBoxPackageArtifacts
    "${MELOBOX_PACKAGE_DIR}/*.lib"
    "${MELOBOX_PACKAGE_DIR}/*.exp"
    "${MELOBOX_PACKAGE_DIR}/*.pdb"
    "${MELOBOX_PACKAGE_DIR}/*Tests.exe"
    "${MELOBOX_PACKAGE_DIR}/MeloBox-orange-check.exe"
    "${MELOBOX_PACKAGE_DIR}/*.log"
    "${MELOBOX_PACKAGE_DIR}/*test*.txt"
    "${MELOBOX_PACKAGE_DIR}/*smoke*.txt"
)
if(meloBoxPackageArtifacts)
    file(REMOVE ${meloBoxPackageArtifacts})
endif()

file(REMOVE_RECURSE
    "${MELOBOX_PACKAGE_DIR}/cache"
    "${MELOBOX_PACKAGE_DIR}/download"
    "${MELOBOX_PACKAGE_DIR}/logs"
)

set(meloBoxRequiredFiles
    "MeloBox.exe"
    "dmghg_key.pem"
    "libmpv-2.dll"
    "Qt6Core.dll"
    "Qt6Gui.dll"
    "Qt6Network.dll"
    "Qt6Qml.dll"
    "Qt6Quick.dll"
    "Qt6QuickControls2.dll"
    "Qt6QuickTemplates2.dll"
    "Qt6QuickControls2Basic.dll"
    "Qt6QuickControls2BasicStyleImpl.dll"
    "Qt6QuickControls2Impl.dll"
    "Qt6QuickLayouts.dll"
    "Qt6QuickEffects.dll"
    "Qt6LabsQmlModels.dll"
    "Qt6Multimedia.dll"
    "Qt6Widgets.dll"
    "platforms/qwindows.dll"
    "imageformats/qjpeg.dll"
    "imageformats/qwebp.dll"
    "jpeg62.dll"
    "libwebp.dll"
    "libwebpdemux.dll"
    "libwebpmux.dll"
    "libsharpyuv.dll"
    "tls/qschannelbackend.dll"
    "tls/qopensslbackend.dll"
    "libcrypto-3-x64.dll"
    "libssl-3-x64.dll"
    "networkinformation/qnetworklistmanager.dll"
    "multimedia/windowsmediaplugin.dll"
    "qml/QtQuick/qmldir"
    "qml/QtQuick/Controls/qmldir"
    "qml/QtQuick/Layouts/qmldir"
    "qml/QtQuick/Effects/qmldir"
)
foreach(meloBoxRequiredFile IN LISTS meloBoxRequiredFiles)
    if(NOT EXISTS "${MELOBOX_PACKAGE_DIR}/${meloBoxRequiredFile}")
        message(FATAL_ERROR "Packaged runtime is incomplete: missing ${meloBoxRequiredFile}")
    endif()
endforeach()

execute_process(
    COMMAND "${MELOBOX_INNO_SETUP_COMPILER}"
        "/DAppVersion=${MELOBOX_APP_VERSION}"
        "${MELOBOX_INSTALLER_SCRIPT}"
    RESULT_VARIABLE meloBoxInstallerResult
)

if(NOT meloBoxInstallerResult EQUAL 0)
    message(FATAL_ERROR "Installer compilation failed.")
endif()
