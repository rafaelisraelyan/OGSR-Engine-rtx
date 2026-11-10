@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /std:c++20 /EHsc /W4 /WX /I ".." j2a_hierarchy_cpu_main.cpp ..\dx12levelhierarchy.cpp /Fe:j2a_hierarchy_cpu.exe
if errorlevel 1 exit /b 1
j2a_hierarchy_cpu.exe
exit /b %ERRORLEVEL%
