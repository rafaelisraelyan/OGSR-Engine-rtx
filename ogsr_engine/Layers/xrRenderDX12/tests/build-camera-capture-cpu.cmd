@echo off
setlocal EnableExtensions

rem F4.0-B3-B1 CPU tests for the camera capture wire format, export gate and
rem transactional writer. No D3D12 device is created here.

set "TESTS=%~dp0"
set "TESTS=%TESTS:~0,-1%"
set "ROOT=%TESTS%\..\..\..\.."
set "RENDER=%ROOT%\ogsr_engine\Layers\xrRenderDX12"
set "JOBS=%ROOT%\ogsr_engine\Layers\JobSystem"
set "DXMATH=%ROOT%\3rd_party\Src\DirectXMath\DirectXMath\Inc"
set "SOURCE=%TESTS%\cameracapturefile_cpu_main.cpp"

if "%B3B1_CAMERA_BUILD_DIR%"=="" set "B3B1_CAMERA_BUILD_DIR=%TEMP%\ogsr-b3b1-camera-cpu"
if not exist "%B3B1_CAMERA_BUILD_DIR%" mkdir "%B3B1_CAMERA_BUILD_DIR%"
pushd "%B3B1_CAMERA_BUILD_DIR%"

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

set "EXE=%B3B1_CAMERA_BUILD_DIR%\cameracapturefile_cpu_tests.exe"
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /fp:precise /utf-8 /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DOGSR_CAMERACAPTURE_TEST_HOOKS ^
    /I"%RENDER%" /I"%JOBS%\include" /I"%JOBS%\thirdparty\enkiTS\src" /I"%DXMATH%" ^
    "%SOURCE%" "%RENDER%\dx12cameracapturefile.cpp" "%RENDER%\dx12gamecamera.cpp" ^
    "%RENDER%\dx12renderprep.cpp" "%RENDER%\dx12packetplan.cpp" "%RENDER%\dx12log.cpp" ^
    "%JOBS%\src\JobSystem.cpp" "%JOBS%\src\JobGroup.cpp" "%JOBS%\src\EnkiBackend.cpp" ^
    "%JOBS%\src\CpuTopology.cpp" "%JOBS%\thirdparty\enkiTS\src\TaskScheduler.cpp" ^
    /Fe:"%EXE%" /link /SUBSYSTEM:CONSOLE
if errorlevel 1 (
    popd
    exit /b 1
)

echo B3B1_CAMERA_EXE=%EXE%
popd
exit /b 0
