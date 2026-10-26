@echo off
setlocal EnableExtensions

rem F4.0-B3-A standalone offscreen D3D12 proof.
rem No Engine executable, CHW12 swapchain, DX11/R4 path or game project is
rem linked into this test.

set "ROOT=%~dp0..\..\..\.."
set "RENDER=%ROOT%\ogsr_engine\Layers\xrRenderDX12"
set "JOBS=%ROOT%\ogsr_engine\Layers\JobSystem"
set "DXMATH=%ROOT%\3rd_party\Src\DirectXMath\DirectXMath\Inc"
set "SOURCE=%RENDER%\tests\b3a_offscreen_main.cpp"

if "%B3A_BUILD_DIR%"=="" set "B3A_BUILD_DIR=%TEMP%\ogsr-b3a-offscreen"
if not exist "%B3A_BUILD_DIR%" mkdir "%B3A_BUILD_DIR%"
pushd "%B3A_BUILD_DIR%"

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

set "EXE=%B3A_BUILD_DIR%\b3a_offscreen.exe"
cl /nologo /std:c++20 /EHsc /MT /O2 /W4 /fp:precise /utf-8 /DNDEBUG /DWIN32 /D_WINDOWS /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX ^
    /I"%RENDER%" /I"%JOBS%\include" /I"%JOBS%\thirdparty\enkiTS\src" /I"%DXMATH%" ^
    "%SOURCE%" "%RENDER%\dx12xraylevelgeometry.cpp" "%RENDER%\dx12gamecamera.cpp" ^
    "%RENDER%\dx12cameracapturefile.cpp" ^
    "%RENDER%\dx12renderprep.cpp" "%RENDER%\dx12packetplan.cpp" "%RENDER%\dx12log.cpp" ^
    "%JOBS%\src\JobSystem.cpp" "%JOBS%\src\JobGroup.cpp" "%JOBS%\src\EnkiBackend.cpp" ^
    "%JOBS%\src\CpuTopology.cpp" "%JOBS%\thirdparty\enkiTS\src\TaskScheduler.cpp" ^
    /Fe:"%EXE%" /link /SUBSYSTEM:CONSOLE d3d12.lib dxgi.lib d3dcompiler.lib /DEBUG
if errorlevel 1 (
    popd
    exit /b 1
)

echo B3A_EXE=%EXE%
popd
exit /b 0
