@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "WT=D:\OGSR_J2B_M1_Source"
set "RENDER=%WT%\ogsr_engine\Layers\xrRenderDX12"
set "XR3DA=%WT%\ogsr_engine\xr_3da"
cl /nologo /std:c++20 /EHsc /MT /O2 /fp:precise /W4 /WX ^
  /I "%RENDER%" ^
  /I "%XR3DA%" ^
  "%~dp0j2b_m23_plan_cpu_main.cpp" ^
  /Fe:"%~dp0j2b_m23_plan_cpu.exe"
exit /b %ERRORLEVEL%
