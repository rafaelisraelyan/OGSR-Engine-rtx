[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [switch]$Sequential,
    [switch]$DescriptorTestsOnly,
    [switch]$CsmWarpOnly,
    [ValidateSet('banked-parity','scalable','adaptive-runtime','shadow-cache','clustered-lighting','page-table','continuous-1','continuous-2','continuous-3','continuous-banked-1','legacy-diagnostic','adaptive-runtime-continuous','shadow-cache-continuous','clustered-lighting-continuous','point-face-continuous','detail-casters','animated-adaptive-cache')][string]$WarpCase,
    [string]$JobLibrary
)

$ErrorActionPreference = 'Stop'
if ($DescriptorTestsOnly -and $CsmWarpOnly) { throw 'Выберите один режим тестирования.' }
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
# Компилируем те же объявления C++ и HLSL, которые использует игровой проход.
$scene = Get-Content -LiteralPath "$engine/GameDx12StaticScene.cpp" -Raw
$patterns = @(
    '(?s)constexpr uint32_t kLocalShadowLayers = .*?static_assert\(kFrameConstantsSize == \d+\);',
    '(?s)constexpr const char\* kFrameShaderConstants = R"\(.*?\)";',
    '(?s)constexpr const char\* kAuxVertexShader = R"\(.*?\)";',
    '(?s)const std::string kScenePixelShader = std::string\(R"\(.*?\)";'
)
$declarations = foreach ($pattern in $patterns) {
    $match = [regex]::Match($scene, $pattern)
    if (!$match.Success) { throw "Не найдено объявление для проверки GPU: $pattern" }
    $match.Value
}
("#include `"GameDx12ShadowFilterHlsl.h`"" + [Environment]::NewLine + ($declarations -join [Environment]::NewLine)) | Set-Content -LiteralPath "$($output.FullName)/production-shadow.inc" -Encoding utf8
if (!$JobLibrary) { $JobLibrary = "$gr/x64/ReleaseDX12/gr_job_system.lib" }
$common += "/I$($output.FullName)"
$warpArgs = @()
if ($Sequential) { $warpArgs = @('--sequential') }
if ($WarpCase) { $warpArgs = @("--$WarpCase") }
Push-Location -LiteralPath $output.FullName
try {
    if ($DescriptorTestsOnly) {
        & $cl @common "$PSScriptRoot/test-f43-shadow-descriptors.cpp" /Fe:shadow-descriptors.exe 2>&1 |
            Tee-Object -FilePath shadow-descriptors-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./shadow-descriptors.exe 2>&1 | Tee-Object -FilePath shadow-descriptors.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        $warpArgs = @('--page-table')
    }
    elseif (!$CsmWarpOnly -and !$WarpCase) {
        & $cl @common "$PSScriptRoot/test-f43-csm-math.cpp" /Fe:csm-math.exe 2>&1 | Tee-Object -FilePath math-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./csm-math.exe 2>&1 | Tee-Object -FilePath math.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f43-local-shadow.cpp" /Fe:local-shadow.exe 2>&1 | Tee-Object -FilePath local-shadow-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./local-shadow.exe 2>&1 | Tee-Object -FilePath local-shadow.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f43-csm-sampling-reference.cpp" /Fe:csm-reference.exe 2>&1 | Tee-Object -FilePath csm-reference-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./csm-reference.exe 2>&1 | Tee-Object -FilePath csm-reference.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f44-particle-packets.cpp" /Fe:particle-packets.exe 2>&1 | Tee-Object -FilePath particle-packets-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./particle-packets.exe 2>&1 | Tee-Object -FilePath particle-packets.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f44-particle-draw-plan.cpp" /Fe:particle-draw-plan.exe 2>&1 | Tee-Object -FilePath particle-draw-plan-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./particle-draw-plan.exe 2>&1 | Tee-Object -FilePath particle-draw-plan.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f44-particle-materials.cpp" /Fe:particle-materials.exe 2>&1 | Tee-Object -FilePath particle-materials-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./particle-materials.exe 2>&1 | Tee-Object -FilePath particle-materials.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f45-ui-commands.cpp" /Fe:ui-commands.exe 2>&1 | Tee-Object -FilePath ui-commands-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./ui-commands.exe 2>&1 | Tee-Object -FilePath ui-commands.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f43-pcf-pairs.cpp" /Fe:pcf-pairs.exe 2>&1 | Tee-Object -FilePath pcf-pairs-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./pcf-pairs.exe 2>&1 | Tee-Object -FilePath pcf-pairs.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f43-adaptive-shadow.cpp" /Fe:adaptive-shadow.exe 2>&1 | Tee-Object -FilePath adaptive-shadow-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./adaptive-shadow.exe 2>&1 | Tee-Object -FilePath adaptive-shadow.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f43-shadow-pages.cpp" /Fe:shadow-pages.exe 2>&1 | Tee-Object -FilePath shadow-pages-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./shadow-pages.exe 2>&1 | Tee-Object -FilePath shadow-pages.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f43-shadow-page-resources-warp.cpp" /Fe:shadow-page-resources-warp.exe /link d3d12.lib dxgi.lib dxguid.lib 2>&1 | Tee-Object -FilePath shadow-page-resources-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./shadow-page-resources-warp.exe 2>&1 | Tee-Object -FilePath shadow-page-resources-warp.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f43-local-light-capture.cpp" /Fe:local-light-capture.exe 2>&1 | Tee-Object -FilePath local-light-capture-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./local-light-capture.exe 2>&1 | Tee-Object -FilePath local-light-capture.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $cl @common "$PSScriptRoot/test-f43-shadow-address.cpp" /Fe:shadow-address.exe 2>&1 | Tee-Object -FilePath shadow-address-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./shadow-address.exe 2>&1 | Tee-Object -FilePath shadow-address.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    }
    if ($WarpCase -eq 'scalable') {
        & $cl @common "$PSScriptRoot/test-f43-shadow-schedule.cpp" /Fe:shadow-schedule.exe 2>&1 |
            Tee-Object -FilePath shadow-schedule-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./shadow-schedule.exe 2>&1 | Tee-Object -FilePath shadow-schedule.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    }
    if ($WarpCase -eq 'adaptive-runtime') {
        & $cl @common "$PSScriptRoot/test-f43-adaptive-runtime.cpp" /Fe:adaptive-runtime.exe 2>&1 |
            Tee-Object -FilePath adaptive-runtime-build.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & ./adaptive-runtime.exe 2>&1 | Tee-Object -FilePath adaptive-runtime.log
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
    }
    $argsForWarp = '/DOGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE',"/I$gr/include",
        "$PSScriptRoot/test-f43-csm-warp.cpp",$nativePath,'/Fe:csm-warp.exe','/link',
        $JobLibrary,'d3d12.lib','dxgi.lib','d3dcompiler.lib','dxguid.lib','user32.lib'
    & $cl @common @argsForWarp 2>&1 | Tee-Object -FilePath warp-build.log
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    & ./csm-warp.exe @warpArgs 2>&1 | Tee-Object -FilePath warp.log
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
