@echo off
setlocal EnableExtensions

rem F4.0-B2.1/B2.2: isolated CPU-only game camera tests.
rem
rem Usage:
rem   build-gamecamera-cpu.cmd        (Release + OOM fault injection)
rem   build-gamecamera-cpu.cmd asan   (Windows ASan, no custom-new OOM)
rem   build-gamecamera-cpu.cmd stl    (Release + MSVC STL hardening + OOM)
rem
rem This target compiles only CPU camera code and the existing CPU
rem renderprep translation unit. No game executable, DX11/R4, D3D12 device,
rem VFS, archive or game project is touched.

set "ROOT=%~dp0..\..\..\.."
set "RENDER=%ROOT%\ogsr_engine\Layers\xrRenderDX12"
set "JOBS=%ROOT%\ogsr_engine\Layers\JobSystem"
set "TEST_SOURCE=%RENDER%\tests\gamecamera_cpu_main.cpp"

if "%GAMECAMERA_BUILD_DIR%"=="" set "GAMECAMERA_BUILD_DIR=%TEMP%\ogsr-f4b2-gamecamera"
if not exist "%GAMECAMERA_BUILD_DIR%" mkdir "%GAMECAMERA_BUILD_DIR%"
pushd "%GAMECAMERA_BUILD_DIR%"

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
set "OOM_DEFINE=/DGAMECAMERA_OOM_TEST"
set "TEST_DEFINE=/DGAMECAMERA_TESTING"
if /I "%MODE%"=="asan" (
    set "ASAN=/fsanitize=address /Zi /DEBUG"
    set "OOM_DEFINE="
)
if /I "%MODE%"=="stl" (
    set "TEST_DEFINE=/DGAMECAMERA_TESTING /DGAMECAMERA_STL_HARDENING_TEST /D_MSVC_STL_HARDENING=1"
)
if defined MODE if /I not "%MODE%"=="asan" if /I not "%MODE%"=="stl" (
    echo ERROR: expected no arguments, asan, or stl.
    popd
    exit /b 2
)

set "EXE=%GAMECAMERA_BUILD_DIR%\gamecamera_cpu_tests.exe"
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /fp:precise /utf-8 /DNDEBUG /DWIN32 /D_CRT_SECURE_NO_WARNINGS %OOM_DEFINE% %TEST_DEFINE% %ASAN% ^
    /I"%RENDER%" /I"%JOBS%\include" /I"%JOBS%\thirdparty\enkiTS\src" ^
    "%TEST_SOURCE%" "%RENDER%\dx12gamecamera.cpp" "%RENDER%\dx12renderprep.cpp" ^
    "%RENDER%\dx12packetplan.cpp" "%RENDER%\dx12log.cpp" ^
    "%JOBS%\src\JobSystem.cpp" "%JOBS%\src\JobGroup.cpp" "%JOBS%\src\EnkiBackend.cpp" ^
    "%JOBS%\src\CpuTopology.cpp" "%JOBS%\thirdparty\enkiTS\src\TaskScheduler.cpp" ^
    /Fe:"%EXE%" /link /DEBUG
if errorlevel 1 (
    popd
    exit /b 1
)

"%EXE%"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
