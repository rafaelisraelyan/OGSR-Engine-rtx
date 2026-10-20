@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 exit /b 2
set "ROOT=%~dp0..\..\..\.."
set "RENDER=%ROOT%\ogsr_engine\Layers\xrRenderDX12"
set "JOBS=%ROOT%\ogsr_engine\Layers\JobSystem"
if not exist "%ROOT%\x64\f352b-asan" mkdir "%ROOT%\x64\f352b-asan"
pushd "%ROOT%\x64\f352b-asan"
cl /nologo /std:c++20 /EHsc /MT /O2 /Zi /fp:precise /fsanitize=address /utf-8 /DGR_JOBS_TESTING /DNDEBUG /DWIN32 ^
 /I"%RENDER%" /I"%JOBS%\include" /I"%JOBS%\thirdparty\enkiTS\src" /I"%ROOT%\3rd_party\Src\DirectXMath\DirectXMath\Inc" ^
 "%RENDER%\tests\visibility_cpu_main.cpp" "%RENDER%\dx12renderprep.cpp" "%RENDER%\dx12renderprep_tests.cpp" "%RENDER%\dx12snapshot_tests.cpp" "%RENDER%\dx12ordering_tests.cpp" "%RENDER%\dx12packetplan.cpp" "%RENDER%\dx12packetplan_tests.cpp" "%RENDER%\dx12fencewait_tests.cpp" "%RENDER%\dx12transform_tests.cpp" "%RENDER%\dx12camera.cpp" "%RENDER%\dx12log.cpp" ^
 "%JOBS%\src\JobSystem.cpp" "%JOBS%\src\JobGroup.cpp" "%JOBS%\src\EnkiBackend.cpp" "%JOBS%\src\CpuTopology.cpp" "%JOBS%\thirdparty\enkiTS\src\TaskScheduler.cpp" ^
 /Fe:visibility_asan.exe /link /DEBUG
if errorlevel 1 (popd & exit /b 1)
visibility_asan.exe
set "RESULT=%errorlevel%"
popd
exit /b %RESULT%
