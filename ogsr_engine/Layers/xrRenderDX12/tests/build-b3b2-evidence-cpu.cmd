@echo off
setlocal EnableExtensions
rem F4.0-B3-B2B P1-P3 portable strict verifier contract tests (MSVC).
set "HERE=%~dp0"
if "%~1"=="" (set "OUT=%HERE%build-evidence-cpu") else (set "OUT=%~1")
if not exist "%OUT%" mkdir "%OUT%" || exit /b 1
where cl >nul 2>nul
if errorlevel 1 (
    echo MSVC cl.exe not on PATH. Run from x64 Native Tools Command Prompt.
    exit /b 1
)
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /Fe:"%OUT%\b3b2_evidence_validation_cpu.exe" "%HERE%b3b2_evidence_validation_cpu.cpp"
if errorlevel 1 exit /b 1
"%OUT%\b3b2_evidence_validation_cpu.exe"
exit /b %ERRORLEVEL%
