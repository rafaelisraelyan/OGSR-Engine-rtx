@echo off
setlocal EnableExtensions

rem F4.0-B3-B1 synthetic rasterization acceptance test (CPU rules + real GPU).
rem Uses the same PSO contract as the offscreen replay: CullMode=NONE,
rem DepthClipEnable=TRUE, DepthFunc=LESS, no blend, 64x64 target.

set "TESTS=%~dp0"
set "TESTS=%TESTS:~0,-1%"
set "SOURCE=%TESTS%\b3b1_raster_acceptance.cpp"

if "%B3B1_RASTER_BUILD_DIR%"=="" set "B3B1_RASTER_BUILD_DIR=%TEMP%\ogsr-b3b1-raster"
if not exist "%B3B1_RASTER_BUILD_DIR%" mkdir "%B3B1_RASTER_BUILD_DIR%"
pushd "%B3B1_RASTER_BUILD_DIR%"

where cl >nul 2>nul
if errorlevel 1 (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
)
where cl >nul 2>nul
if errorlevel 1 (
    echo ERROR: MSVC cl.exe was not found.
    popd
    exit /b 2
)

set "EXE=%B3B1_RASTER_BUILD_DIR%\b3b1_raster_acceptance.exe"
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /fp:precise /utf-8 /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX ^
    /I"%TESTS%" "%SOURCE%" /Fe:"%EXE%" /link /SUBSYSTEM:CONSOLE d3d12.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 (
    popd
    exit /b 1
)

echo B3B1_RASTER_EXE=%EXE%
popd
exit /b 0
