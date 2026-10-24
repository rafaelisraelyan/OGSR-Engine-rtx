@echo off
setlocal EnableExtensions

rem F4.0-B2.3 narrow CPU-only tests for adapter diagnostics/dump gate.
rem No Engine, Device, D3D11, D3D12 or DXGI dependency.

set "ROOT=%~dp0..\..\.."
set "XR3DA=%ROOT%\ogsr_engine\xr_3da"
set "RENDER=%ROOT%\ogsr_engine\Layers\xrRenderDX12"
set "TEST_SOURCE=%XR3DA%\tests\gamecamera_adapter_rules_main.cpp"

if "%B23_RULES_BUILD_DIR%"=="" set "B23_RULES_BUILD_DIR=%TEMP%\ogsr-b23-adapter-rules"
if not exist "%B23_RULES_BUILD_DIR%" mkdir "%B23_RULES_BUILD_DIR%"
pushd "%B23_RULES_BUILD_DIR%"

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

set "EXE=%B23_RULES_BUILD_DIR%\gamecamera_adapter_rules.exe"
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /fp:precise /utf-8 /DNDEBUG /DWIN32 /D_CRT_SECURE_NO_WARNINGS ^
    /I"%XR3DA%" /I"%RENDER%" ^
    "%TEST_SOURCE%" /Fe:"%EXE%" /link /DEBUG
if errorlevel 1 (
    popd
    exit /b 1
)

"%EXE%"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
