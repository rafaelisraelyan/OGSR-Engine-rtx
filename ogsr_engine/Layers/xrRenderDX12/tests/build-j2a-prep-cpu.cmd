@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /std:c++20 /EHsc /W3 /O2 /fp:precise ^
  /I ".." ^
  /I "..\..\JobSystem\include" ^
  /I "..\..\JobSystem\thirdparty\enkiTS\src" ^
  j2a_prep_cpu_main.cpp ..\dx12renderprep.cpp ..\dx12packetplan.cpp ^
  ..\..\JobSystem\src\JobSystem.cpp ..\..\JobSystem\src\JobGroup.cpp ^
  ..\..\JobSystem\src\EnkiBackend.cpp ..\..\JobSystem\src\CpuTopology.cpp ^
  ..\..\JobSystem\thirdparty\enkiTS\src\TaskScheduler.cpp ^
  /Fe:j2a_prep_cpu.exe
if errorlevel 1 exit /b 1
j2a_prep_cpu.exe
exit /b %ERRORLEVEL%
