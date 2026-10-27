@echo off
setlocal EnableExtensions

rem F4.0-B3-B2A standalone CPU-only named-pipe receiver. No Engine link.

set "TESTS=%~dp0"
set "TESTS=%TESTS:~0,-1%"
set "ROOT=%TESTS%\..\..\..\.."
set "RENDER=%ROOT%\ogsr_engine\Layers\xrRenderDX12"
set "JOBS=%ROOT%\ogsr_engine\Layers\JobSystem"
set "DXMATH=%ROOT%\3rd_party\Src\DirectXMath\DirectXMath\Inc"
set "SOURCE=%TESTS%\b3b2_camera_receiver.cpp"

if "%B3B2_RECEIVER_BUILD_DIR%"=="" set "B3B2_RECEIVER_BUILD_DIR=%TEMP%\ogsr-b3b2-receiver"
if not exist "%B3B2_RECEIVER_BUILD_DIR%" mkdir "%B3B2_RECEIVER_BUILD_DIR%"
pushd "%B3B2_RECEIVER_BUILD_DIR%"

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

set "EXE=%B3B2_RECEIVER_BUILD_DIR%\b3b2_camera_receiver.exe"
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /fp:precise /utf-8 /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX ^
    /I"%RENDER%" /I"%JOBS%\include" /I"%JOBS%\thirdparty\enkiTS\src" /I"%DXMATH%" ^
    "%SOURCE%" "%RENDER%\b3b2_camera_stream.cpp" ^
    "%RENDER%\dx12cameracapturefile.cpp" "%RENDER%\dx12gamecamera.cpp" ^
    "%RENDER%\dx12renderprep.cpp" "%RENDER%\dx12packetplan.cpp" "%RENDER%\dx12log.cpp" ^
    "%JOBS%\src\JobSystem.cpp" "%JOBS%\src\JobGroup.cpp" "%JOBS%\src\EnkiBackend.cpp" ^
    "%JOBS%\src\CpuTopology.cpp" "%JOBS%\thirdparty\enkiTS\src\TaskScheduler.cpp" ^
    /Fe:"%EXE%" /link /SUBSYSTEM:CONSOLE advapi32.lib
if errorlevel 1 (
    popd
    exit /b 1
)

echo B3B2_RECEIVER_EXE=%EXE%
popd
exit /b 0
