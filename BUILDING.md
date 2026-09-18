# Building Stock Checker

## Requirements

- Visual Studio 2022 with C++ desktop development tools
- CMake 3.19 or newer
- Git
- vcpkg
- CommonLibSSE-NG
- Skyrim Menu Framework consumer header (`SKSEMenuFramework.h`)

The project is configured for C++23 and Skyrim SE/AE. VR is disabled.

## Repository layout

```text
Stock-Checker/
├── CMakeLists.txt
├── vcpkg.json
├── extern/
│   ├── CommonLibSSE-NG/
│   └── SKSEMenuFramework.h
├── include/
│   └── StockChecker/
│       └── PCH.h
├── package/
│   └── SKSE/
│       └── Plugins/
└── src/
    └── main.cpp
```

The CommonLibSSE-NG subdirectory and SMF consumer header are external dependencies and are not vendored in this repository.

## Clone dependencies

From the repository root:

```powershell
git clone https://github.com/alandtse/CommonLibSSE-NG.git ".\extern\CommonLibSSE-NG"
```

Place the current `SKSEMenuFramework.h` consumer header at:

```text
extern/SKSEMenuFramework.h
```

## Configure

Example using vcpkg at `C:\vcpkg`:

```powershell
cmake -S . -B build `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
```

## Build

```powershell
cmake --build build --config Release
```

The DLL is emitted under:

```text
package/SKSE/Plugins/Release/StockChecker.dll
```
