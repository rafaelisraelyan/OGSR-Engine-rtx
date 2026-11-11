@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "WT=D:\OGSR_J2B_M1_Source"
set "RENDER=%WT%\ogsr_engine\Layers\xrRenderDX12"
set "JS=%WT%\ogsr_engine\Layers\JobSystem"
cl /nologo /std:c++20 /EHsc /MT /O2 /fp:precise /W4 /WX ^
  /I "%RENDER%" ^
  /I "%JS%\include" ^
  /I "%JS%\thirdparty\enkiTS\src" ^
  "%~dp0j2b_m1_batch_cpu_main.cpp" ^
  "%RENDER%\dx12j2bbatch.cpp" ^
  "%RENDER%\dx12levelhierarchy.cpp" ^
  "%RENDER%\dx12xraylevelgeometry.cpp" ^
  "%RENDER%\dx12xraylevelasset.cpp" ^
  "%JS%\src\JobSystem.cpp" ^
  "%JS%\src\JobGroup.cpp" ^
  "%JS%\src\EnkiBackend.cpp" ^
  "%JS%\src\CpuTopology.cpp" ^
  "%JS%\thirdparty\enkiTS\src\TaskScheduler.cpp" ^
  /Fe:"%~dp0j2b_m1_batch_cpu.exe"
exit /b %ERRORLEVEL%
