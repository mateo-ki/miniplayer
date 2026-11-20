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

    # Release CRT 完整集随程序分发 (app-local): libmpv-2.dll 等第三方 DLL
    # 依赖 vcruntime140/vcruntime140_1/msvcp140 全家桶。windeployqt 只部署
    # Qt 自己用到的 MSVCP140_1/2, 精简系统缺任一即报"DLL 初始化例程失败"。
    # 来源与 Debug 分支一致, 取 VS 安装目录的 Redist CRT。
    set(_melobox_crt_dirs "")
    foreach(_melobox_vs_root
            "D:/software/vs/Microsoft Visual Studio/2022"
            "C:/Program Files/Microsoft Visual Studio/2022"
            "C:/Program Files (x86)/Microsoft Visual Studio/2022")
        file(GLOB _melobox_crt_dirs
            "${_melobox_vs_root}/*/VC/Redist/MSVC/*/x64/Microsoft.VC14*.CRT")
        if(_melobox_crt_dirs)
            break()
        endif()
    endforeach()
    if(_melobox_crt_dirs)
        list(GET _melobox_crt_dirs 0 _melobox_crt_dir)
        file(GLOB _melobox_crt_dlls
            "${_melobox_crt_dir}/vcruntime140.dll"
            "${_melobox_crt_dir}/vcruntime140_1.dll"
            "${_melobox_crt_dir}/msvcp140.dll"
            "${_melobox_crt_dir}/msvcp140_1.dll"
            "${_melobox_crt_dir}/msvcp140_2.dll"
            "${_melobox_crt_dir}/concrt140.dll")
        if(_melobox_crt_dlls)
            file(COPY ${_melobox_crt_dlls} DESTINATION "${MELOBOX_TARGET_FILE_DIR}")
            list(LENGTH _melobox_crt_dlls _melobox_crt_count)
            message(STATUS "DeployRuntime: deployed ${_melobox_crt_count} app-local CRT DLLs")
        endif()
    else()
        message(WARNING "DeployRuntime: VC Redist CRT dir not found; app-local CRT not deployed")
    endif()
endif()

# windeployqt 部署的是 vcpkg Qt 自带的 d3dcompiler_47.dll (实测 10.0.26100.9168)，
# 它按新版 UCRT 编译，导入表要求 __CxxFrameHandler4；UCRT 较旧的机器上 mpv 首次
# 出画加载它就会弹"无法定位程序输入点 ... d3dcompiler_47.dll"。Windows SDK 的
# D3D redist 版 (10.0.26100.4654) 是静态 CRT，只导入 KERNEL32/ADVAPI32/RPCRT4，
# 覆盖掉它就不依赖目标机器的 UCRT 版本。redist 目录本身即可再分发授权。
find_file(MELOBOX_D3DCOMPILER_REDIST
    NAMES d3dcompiler_47.dll
    HINTS "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64"
          "C:/Program Files/Windows Kits/10/Redist/D3D/x64"
          "C:/Program Files (x86)/Windows Kits/11/Redist/D3D/x64"
    NO_DEFAULT_PATH
)
if(MELOBOX_D3DCOMPILER_REDIST)
    file(COPY "${MELOBOX_D3DCOMPILER_REDIST}" DESTINATION "${MELOBOX_TARGET_FILE_DIR}")
    message(STATUS "DeployRuntime: d3dcompiler_47.dll replaced with ${MELOBOX_D3DCOMPILER_REDIST}")
else()
    message(WARNING
        "DeployRuntime: Windows SDK D3D redist d3dcompiler_47.dll not found; "
        "shipped copy still needs a recent UCRT (old machines will show an entry-point error)")
endif()
