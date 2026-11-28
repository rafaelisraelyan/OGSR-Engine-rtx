[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [switch]$Warp,
    [string]$Adapter = 'RTX 5070 Laptop',
    [string[]]$Cases = @('legacy/legacy','subtexel3/legacy','legacy/subtexel3',
        'subtexel5/legacy','legacy/subtexel5','subtexel5/subtexel5','subtexel7/subtexel7'),
    [string]$SceneRef,
    [switch]$ReproduceConstantLocal
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = New-Item -ItemType Directory -Path $OutputDirectory
$engine = Join-Path $root 'ogsr_engine/xr_3da'
$math = Join-Path $root '3rd_party/Src/DirectXMath/DirectXMath/Inc'
$vc = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207'
$kit = 'C:/Program Files (x86)/Windows Kits/10'
$sdk = '10.0.26100.0'
$env:INCLUDE = "$vc/include;$kit/Include/$sdk/ucrt;$kit/Include/$sdk/shared;$kit/Include/$sdk/um;$kit/Include/$sdk/winrt"
$env:LIB = "$vc/lib/x64;$kit/Lib/$sdk/ucrt/x64;$kit/Lib/$sdk/um/x64"
if ($SceneRef) {
    $scene = (git -C $root show "${SceneRef}:ogsr_engine/xr_3da/GameDx12StaticScene.cpp") -join "`n"
    if ($LASTEXITCODE) { throw 'git show failed' }
} else {
    $scene = Get-Content -LiteralPath "$engine/GameDx12StaticScene.cpp" -Raw
}
if ($ReproduceConstantLocal) {
    $from = 'return ContinuousLocalFilter(coords, gradient, address, (int)clamp(LocalFilterRadius, 1u, 3u));'
    $to = 'return ContinuousLocalFilter(coords, gradient, address, OGSR_DX12_CONTINUOUS_LOCAL);'
    if (!$scene.Contains($from)) { throw 'Не найден аргумент радиуса для отрицательной проверки' }
    $scene = $scene.Replace($from, $to)
}
# Компилируются полные объявления и тела production-функций из выбранного среза.
# Отдельная программа не линкует загрузчик уровня и игровую логику.
function Read-Production([string]$Pattern) {
    $match = [regex]::Match($scene, $Pattern)
    if (!$match.Success) { throw "Не найден production-блок $Pattern" }
    $match.Value
}
$declarations = @(
    Read-Production '(?s)constexpr uint32_t kLocalShadowLayers = .*?static_assert\(kFrameConstantsSize == \d+\);'
    foreach ($name in @('ShadowLayerRoot','LocalShadowSrvRoot','WaterSrvCount','HemiSrvRoot','TerrainSrvRoot',
        'TerrainScaleRoot','LocalLightBufferRoot','LocalProjectionBufferRoot','LocalPageShapesRoot',
        'ClusterGridRoot','ClusterIndicesRoot','TerrainTextureCount')) {
        Read-Production "constexpr uint32_t k$name = [^;]+;"
    }
    Read-Production '(?s)bool SceneDynamicSunShadowsRequested\(\).*?(?=// Experimental policies)'
    Read-Production '(?s)uint32_t SceneSunFilter\(\).*?(?=struct SunSettings)'
    Read-Production '(?s)constexpr const char\* kFrameShaderConstants = R"\(.*?(?=// Шесть плоскостей)'
)
$definitions = @(
    Read-Production '(?s)bool GameDx12StaticScene12::Fail\(.*?(?=void GameDx12StaticScene12::BuildShadowCasterTrees)'
    Read-Production '(?s)bool GameDx12StaticScene12::CreatePipeline\(.*?(?=bool GameDx12StaticScene12::CreateFrameConstants)'
)
(@('namespace {') + $declarations + @('}') + $definitions) |
    Set-Content -LiteralPath "$($out.FullName)/production-pso.inc" -Encoding utf8
$cl = "$vc/bin/HostX64/x64/cl.exe"
Push-Location $out.FullName
try {
    & $cl /nologo /std:c++20 /EHsc /utf-8 /MT /O2 /DNOMINMAX /DOGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE `
        "/I$engine" "/I$math" "/I$root/ogsr_engine/Layers/xrRenderDX12" "/I$($out.FullName)" `
        "/I$root/ogsr_engine/Layers/JobSystem/include" `
        "$PSScriptRoot/test-f43-production-pso.cpp" `
        /Fe:production-pso.exe /link d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib 2>&1 |
        Tee-Object -FilePath build.log
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    $failed = $false
    foreach ($case in $Cases) {
        $sun, $local = $case.Split('/')
        if (!$sun -or !$local) { throw "Неверный сценарий $case" }
        $device = if ($Warp) { 'warp' } else { $Adapter }
        $log = "$sun-$local.log"
        & ./production-pso.exe $device $sun $local 2>&1 | Tee-Object -FilePath $log
        if ($LASTEXITCODE) { $failed = $true }
    }
    if ($failed) { exit 1 }
} finally {
    Pop-Location
}
