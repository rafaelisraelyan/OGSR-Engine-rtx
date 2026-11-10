@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"

set "SRC=%~1"
if "%SRC%"=="" set "SRC=..\..\..\xr_3da\GameDx12J2APreparation.cpp"
set "TAG=%~2"
if "%TAG%"=="" set "TAG=r7"
set "HDR=%~3"
if "%HDR%"=="" set "HDR=%~dp0..\..\..\xr_3da"
set "EXTRA=%~4"
set "MODE=%~5"

set "BUILD=%~dp0j2a_no_throw_cpu_build"
if exist "%BUILD%" rmdir /s /q "%BUILD%"
mkdir "%BUILD%"
copy /y "%SRC%" "%BUILD%\GameDx12J2APreparation.cpp" >nul
copy /y "%~dp0j2a_target_cpu_shims\*.*" "%BUILD%\" >nul

cl /nologo /std:c++20 /EHsc /W3 /O2 /fp:precise ^
  /DOGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE ^
  /DOGSR_J2A_CPU_FAULT_INJECTION ^
  %EXTRA% ^
  /I "%~dp0.." ^
  /I "%BUILD%" ^
  /I "%~dp0." ^
  /I "%HDR%" ^
  /I "%~dp0..\..\..\xr_3da" ^
  /I "%~dp0..\..\JobSystem\include" ^
  /I "%~dp0..\..\JobSystem\thirdparty\enkiTS\src" ^
  "%~dp0j2a_no_throw_cpu_main.cpp" ^
  "%BUILD%\GameDx12J2APreparation.cpp" ^
  "%~dp0..\dx12levelhierarchy.cpp" ^
  "%~dp0..\dx12renderprep.cpp" ^
  "%~dp0..\dx12packetplan.cpp" ^
  "%~dp0..\..\JobSystem\src\JobSystem.cpp" ^
  "%~dp0..\..\JobSystem\src\JobGroup.cpp" ^
  "%~dp0..\..\JobSystem\src\EnkiBackend.cpp" ^
  "%~dp0..\..\JobSystem\src\CpuTopology.cpp" ^
  "%~dp0..\..\JobSystem\thirdparty\enkiTS\src\TaskScheduler.cpp" ^
  /Fe:"%~dp0j2a_no_throw_cpu_%TAG%.exe"
if errorlevel 1 exit /b 1

"%~dp0j2a_no_throw_cpu_%TAG%.exe" %MODE%
exit /b %ERRORLEVEL%
