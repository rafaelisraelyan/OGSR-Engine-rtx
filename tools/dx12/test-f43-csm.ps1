[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [switch]$Sequential
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$output = New-Item -ItemType Directory -Path $OutputDirectory
$engine = Join-Path $root 'ogsr_engine/xr_3da'
$math = Join-Path $root '3rd_party/Src/DirectXMath/DirectXMath/Inc'
$gr = Join-Path $root 'ogsr_engine/Layers/JobSystem'
$vc = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207'
$kit = 'C:/Program Files (x86)/Windows Kits/10'
$sdk = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$kit/Include/$sdk/ucrt;$kit/Include/$sdk/shared;$kit/Include/$sdk/um;$kit/Include/$sdk/winrt"
$env:LIB = "$vc/lib/x64;$kit/Lib/$sdk/ucrt/x64;$kit/Lib/$sdk/um/x64"
$cl = "$vc/bin/HostX64/x64/cl.exe"
$common = '/nologo','/EHsc','/std:c++20','/utf-8','/MT','/O2','/DNOMINMAX',"/I$engine","/I$math"
$fixtureIncludes = "#include <windows.h>" + [Environment]::NewLine + "void Msg(const char* format, ...);"
$native = (Get-Content -LiteralPath "$engine/GameDx12NativeFrame.cpp" -Raw).Replace('#include "stdafx.h"', $fixtureIncludes)
$nativePath = Join-Path $output.FullName 'GameDx12NativeFrame.cpp'
$native | Set-Content -LiteralPath $nativePath -Encoding utf8
$warpArgs = @()
if ($Sequential) { $warpArgs = @('--sequential') }
Push-Location -LiteralPath $output.FullName
try {
    & $cl @common "$PSScriptRoot/test-f43-csm-math.cpp" /Fe:csm-math.exe 2>&1 | Tee-Object -FilePath math-build.log
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & ./csm-math.exe 2>&1 | Tee-Object -FilePath math.log
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    $argsForWarp = '/DOGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE',"/I$gr/include",
        "$PSScriptRoot/test-f43-csm-warp.cpp",$nativePath,'/Fe:csm-warp.exe','/link',
        "$gr/x64/ReleaseDX12/gr_job_system.lib",'d3d12.lib','dxgi.lib','user32.lib'
    & $cl @common @argsForWarp 2>&1 | Tee-Object -FilePath warp-build.log
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & ./csm-warp.exe @warpArgs 2>&1 | Tee-Object -FilePath warp.log
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
