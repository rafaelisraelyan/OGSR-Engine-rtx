@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"

set "SRC=%~1"
if "%SRC%"=="" set "SRC=..\dx12levelhierarchy.cpp"
set "TAG=%~2"
if "%TAG%"=="" set "TAG=r6"

cl /nologo /std:c++20 /EHsc /W3 /O2 /fp:precise ^
  /I "%~dp0.." ^
  "%~dp0j2a_parser_scaling_cpu_main.cpp" ^
  "%SRC%" ^
  /Fe:"%~dp0j2a_parser_scaling_%TAG%.exe"
if errorlevel 1 exit /b 1

"%~dp0j2a_parser_scaling_%TAG%.exe"
exit /b %ERRORLEVEL%
