# 编译后运行时部署的 QML 模块步骤（由 CMakeLists 的 POST_BUILD 调用）。
#
# windeployqt 从自身位置推断 Qt 目录，永远找不到 vcpkg 的 debug DLL
# （位于 installed/<triplet>/debug/bin，而它只查 installed/<triplet>/bin），
# 因此 Debug 配置直接拷贝 debug QML 模块树；其余配置仍交给 windeployqt。

if(MELOBOX_BUILD_CONFIG STREQUAL "Debug")
    # QML 插件依赖的 Qt 模块 DLL 比 exe 直接链接的多（如 Qt6QuickLayouts），
    # applocal 只拷 exe 的直接依赖，这里把 debug Qt6 运行时全量补齐。
    file(GLOB _melobox_qt_debug_dlls "${MELOBOX_QT_PLUGIN_BASE}/debug/bin/Qt6*.dll")
    if(_melobox_qt_debug_dlls)
        file(COPY ${_melobox_qt_debug_dlls} DESTINATION "${MELOBOX_TARGET_FILE_DIR}")
    endif()
    file(COPY "${MELOBOX_QT_PLUGIN_BASE}/debug/Qt6/qml/"
        DESTINATION "${MELOBOX_TARGET_FILE_DIR}/qml")

    # Debug CRT（msvcp140d/vcruntime140d/ucrtbased），独立运行时需要；
    # 找不到时退回开发环境的 PATH，不视为错误。
    set(_melobox_crt_dirs "")
    foreach(_melobox_vs_root
            "D:/software/vs/Microsoft Visual Studio/2022"
            "C:/Program Files/Microsoft Visual Studio/2022"
            "C:/Program Files (x86)/Microsoft Visual Studio/2022")
        file(GLOB _melobox_crt_dirs
            "${_melobox_vs_root}/*/VC/Redist/MSVC/*/debug_nonredist/x64/Microsoft.VC14*.DebugCRT")
        if(_melobox_crt_dirs)
            break()
        endif()
    endforeach()
    if(NOT _melobox_crt_dirs)
        file(GLOB _melobox_crt_dirs
            "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/ucrt")
    endif()
    if(_melobox_crt_dirs)
        list(GET _melobox_crt_dirs 0 _melobox_crt_dir)
        file(GLOB _melobox_crt_dlls "${_melobox_crt_dir}/msvcp140d.dll"
            "${_melobox_crt_dir}/vcruntime140d.dll"
            "${_melobox_crt_dir}/vcruntime140_1d.dll"
            "${_melobox_crt_dir}/ucrtbased.dll")
        if(_melobox_crt_dlls)
            file(COPY ${_melobox_crt_dlls} DESTINATION "${MELOBOX_TARGET_FILE_DIR}")
        endif()
    endif()
else()
    find_program(MELOBOX_WDEPLOYQT_EXE
        NAMES windeployqt windeployqt.exe
        HINTS "${MELOBOX_QT_PLUGIN_BASE}/tools/Qt6/bin"
    )
    if(NOT MELOBOX_WDEPLOYQT_EXE)
        message(FATAL_ERROR "windeployqt not found under ${MELOBOX_QT_PLUGIN_BASE}/tools/Qt6/bin")
    endif()
    message(STATUS "DeployRuntime: windeployqt = ${MELOBOX_WDEPLOYQT_EXE}")
    execute_process(COMMAND "${MELOBOX_WDEPLOYQT_EXE}"
        --release --no-translations
        "--qmldir=${MELOBOX_SOURCE_DIR}/ui/qml"
        "${MELOBOX_TARGET}"
        OUTPUT_VARIABLE _melobox_wd_out
        ERROR_VARIABLE _melobox_wd_err
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(STATUS "${_melobox_wd_out}")
        message(STATUS "${_melobox_wd_err}")
        message(FATAL_ERROR "windeployqt failed with exit code ${rc}")
    endif()
endif()
