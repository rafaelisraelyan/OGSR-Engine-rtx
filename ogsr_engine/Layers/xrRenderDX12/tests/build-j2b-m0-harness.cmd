@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "WT=D:\OGSR_J2B_M1_Source"
set "RENDER=%WT%\ogsr_engine\Layers\xrRenderDX12"
set "OUT=%~dp0"
cl /nologo /std:c++20 /EHsc /MT /O2 /fp:precise /W4 /WX ^
  /I "%RENDER%" ^
  "%~dp0j2b_m0_real_visuals_main.cpp" ^
  "%RENDER%\dx12xraylevelgeometry.cpp" ^
  "%RENDER%\dx12xraylevelasset.cpp" ^
  /Fe:"%~dp0j2b_m0_real_visuals.exe" ^
  /link bcrypt.lib
exit /b %ERRORLEVEL%
