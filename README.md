# MeloBox

## Release 构建与打包

在项目根目录执行以下命令：

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="D:/vcpkg/scripts/buildsystems/vcpkg.cmake" -DMELOBOX_ENABLE_RUNTIME_DEPLOYMENT=ON
cmake --build build --config Release --target MeloBox --parallel
cmake --build build --config Release --target package_installer --parallel
```

构建完成后，可执行文件位于 `build/Release/`，安装包位于 `dist/installer/`。
