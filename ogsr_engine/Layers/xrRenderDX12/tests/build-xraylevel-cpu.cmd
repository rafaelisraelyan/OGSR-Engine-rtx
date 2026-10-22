@echo off
setlocal EnableExtensions

rem F4.0-B1: изолированная сборка CPU-теста.
rem
rem Использование:
rem   build-xraylevel-cpu.cmd
rem   build-xraylevel-cpu.cmd <level> <level.geom>
rem   build-xraylevel-cpu.cmd asan
rem   build-xraylevel-cpu.cmd asan <level> <level.geom>
rem
rem Один путь без второго — ошибка аргументов и ненулевой exit code.
rem Без путей real-data проверка внутри test executable выдаёт SKIP.
rem Game project, Engine.sln, XR_3DA.vcxproj и renderer targets не меняются.

set "ROOT=%~dp0..\..\..\.."
set "RENDER=%ROOT%\ogsr_engine\Layers\xrRenderDX12"
set "TEST_SOURCE=%RENDER%\tests\xraylevel_cpu_main.cpp"
set "READER_SOURCE=%RENDER%\dx12xraylevelgeometry.cpp"

if "%XRAYLEVEL_BUILD_DIR%"=="" set "XRAYLEVEL_BUILD_DIR=%TEMP%\ogsr-f4b1-cpu"
if not exist "%XRAYLEVEL_BUILD_DIR%" mkdir "%XRAYLEVEL_BUILD_DIR%"
pushd "%XRAYLEVEL_BUILD_DIR%"

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
    echo ERROR: MSVC cl.exe was not found. Run from a Developer Command Prompt.
    popd
    exit /b 2
)

set "MODE=%~1"
set "ASAN="
set "OOM_DEFINE=/DXRAYLEVEL_OOM_TEST"
set "LEVEL="
set "GEOM="
set "EXTRA="
if /I "%MODE%"=="asan" (
    set "ASAN=/fsanitize=address /Zi /DEBUG"
    set "OOM_DEFINE="
    set "LEVEL=%~2"
    set "GEOM=%~3"
    set "EXTRA=%~4"
) else (
    set "LEVEL=%~1"
    set "GEOM=%~2"
    set "EXTRA=%~3"
)

if defined EXTRA goto bad_args
if defined LEVEL goto have_level
if defined GEOM goto bad_args
goto compile

:have_level
if not defined GEOM goto bad_args
goto compile

:bad_args
echo ERROR: expected no paths, two paths, asan, or asan plus two paths.
popd
exit /b 2

:compile
set "EXE=%XRAYLEVEL_BUILD_DIR%\xraylevel_cpu_tests.exe"
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /fp:precise /utf-8 /DNDEBUG /DWIN32 /D_CRT_SECURE_NO_WARNINGS %OOM_DEFINE% ^
    /I"%RENDER%" %ASAN% ^
    "%TEST_SOURCE%" "%READER_SOURCE%" ^
    /Fe:"%EXE%" /link /DEBUG
if errorlevel 1 (
    popd
    exit /b 1
)

if defined LEVEL goto run_with_paths
goto run_no_paths

:run_with_paths
"%EXE%" "%LEVEL%" "%GEOM%"
goto after_run

:run_no_paths
"%EXE%"

:after_run
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
