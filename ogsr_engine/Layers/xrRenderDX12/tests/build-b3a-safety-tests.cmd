@echo off
setlocal EnableExtensions

rem F4.0-B3-A CPU safety tests for the GPU lifetime / command-list /
rem validation policy seam. No D3D12 device is created here.

set "ROOT=%~dp0..\..\..\.."
set "TESTS=%~dp0"
set "TESTS=%TESTS:~0,-1%"
set "SOURCE=%TESTS%\b3a_offscreen_safety_tests.cpp"

if "%B3A_SAFETY_BUILD_DIR%"=="" set "B3A_SAFETY_BUILD_DIR=%TEMP%\ogsr-b3a-safety"
if not exist "%B3A_SAFETY_BUILD_DIR%" mkdir "%B3A_SAFETY_BUILD_DIR%"
pushd "%B3A_SAFETY_BUILD_DIR%"

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

set "EXE=%B3A_SAFETY_BUILD_DIR%\b3a_safety_tests.exe"
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /utf-8 /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX ^
    /I"%TESTS%" "%SOURCE%" /Fe:"%EXE%" /link /SUBSYSTEM:CONSOLE
if errorlevel 1 (
    popd
    exit /b 1
)

echo B3A_SAFETY_EXE=%EXE%
popd
exit /b 0
