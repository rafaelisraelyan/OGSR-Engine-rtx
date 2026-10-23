$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root "src"
$shaders = Join-Path $root "shaders"

function Assert-True([bool]$condition, [string]$message) {
    if (-not $condition) {
        Write-Error "FAIL: $message"
        exit 1
    }
}

$required = @(
    (Join-Path $src "main.cpp"),
    (Join-Path $src "Application.cpp"),
    (Join-Path $src "Renderer.cpp"),
    (Join-Path $src "NoiseVolume.cpp"),
    (Join-Path $src "SliceViewer.cpp"),
    (Join-Path $src "CloudRayMarcher.cpp"),
    (Join-Path $src "CloudCompositor.cpp"),
    (Join-Path $src "CloudProfiler.cpp"),
    (Join-Path $src "CloudTypes.h"),
    (Join-Path $src "Validation.cpp"),
    (Join-Path $src "Window.cpp"),
    (Join-Path $src "AuditPolicy.h"),
    (Join-Path $root "tests\audit_policy_test.cpp"),
    (Join-Path $shaders "NoiseGenerate.hlsl"),
    (Join-Path $shaders "SliceViewer.hlsl"),
    (Join-Path $shaders "CloudRayMarch.hlsl"),
    (Join-Path $shaders "CloudComposite.hlsl")
)
foreach ($path in $required) {
    Assert-True (Test-Path $path) "missing $path"
}

$sourceText = (Get-ChildItem $src -Filter *.cpp | Get-Content -Raw) -join "`n"
$sourceText += (Get-ChildItem $src -Filter *.h | Get-Content -Raw) -join "`n"
$cmake = Get-Content -Raw (Join-Path $root "CMakeLists.txt")
$noise = Get-Content -Raw (Join-Path $shaders "NoiseGenerate.hlsl")
$viewer = Get-Content -Raw (Join-Path $shaders "SliceViewer.hlsl")
$cloudMarch = Get-Content -Raw (Join-Path $shaders "CloudRayMarch.hlsl")
$cloudComposite = Get-Content -Raw (Join-Path $shaders "CloudComposite.hlsl")

foreach ($forbidden in @("xrCore", "xrRenderDX12", "xrRender_R4", "Game/", "Game\")) {
    Assert-True (-not $sourceText.Contains($forbidden)) "forbidden engine reference: $forbidden"
    Assert-True (-not $cmake.Contains($forbidden)) "forbidden engine reference in CMake: $forbidden"
}
foreach ($library in @("d3d12", "dxgi", "d3dcompiler", "user32")) {
    Assert-True ($cmake.Contains($library)) "missing system library: $library"
}

$contracts = @(
    @("GenerateNoise", "cs_5_1", $noise),
    @("ProbePeriodicity", "cs_5_1", $noise),
    @("ProbeSamplerWrap", "cs_5_1", $noise),
    @("CloudRayMarch", "cs_5_1", $cloudMarch),
    @("FullscreenVS", "vs_5_1", $viewer),
    @("SlicePS", "ps_5_1", $viewer),
    @("FullscreenVS", "vs_5_1", $cloudComposite),
    @("CloudCompositePS", "ps_5_1", $cloudComposite)
)
foreach ($contract in $contracts) {
    $entry = $contract[0]
    $profile = $contract[1]
    $shader = $contract[2]
    $pattern = "CompileShaderFile(?s).*?`"$entry`".*?`"$profile`""
    Assert-True ($sourceText -match $pattern) "missing CompileShaderFile contract for $entry/$profile"
    Assert-True ($shader -match "\b$entry\s*\(") "missing shader entry $entry"
}

Assert-True ($noise.Contains("numthreads(8, 8, 4)")) "thread group contract changed"
Assert-True ($sourceText.Contains("kThreadGroupX = 8")) "missing kThreadGroupX"
Assert-True ($sourceText.Contains("kThreadGroupZ = 4")) "missing kThreadGroupZ"
Assert-True ($sourceText.Contains("D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE")) "missing SRV target state"
Assert-True ($sourceText.Contains("D3D12_RESOURCE_BARRIER_TYPE_UAV")) "missing UAV barrier"
Assert-True ($sourceText.Contains("InsertUavBarrier")) "missing UAV barrier helper use"
Assert-True ($sourceText.Contains("D3D12_RESOURCE_STATE_COPY_SOURCE")) "missing readback transition"
Assert-True ($sourceText.Contains("GetCopyableFootprints")) "missing copyable footprint query"
Assert-True ($sourceText.Contains("audit::ClassifyFenceCompletion")) "fence sentinel policy not used"
Assert-True ($sourceText.Contains("audit::EvaluateValidationStatus")) "validation policy not used"
Assert-True ($sourceText.Contains("InfoQueueAvailable")) "info queue capability not tracked"
Assert-True ($sourceText.Contains("--test-no-validation")) "negative validation hook missing"
Assert-True ($sourceText.Contains("--test-no-info-queue")) "negative info queue hook missing"
Assert-True ($noise.Contains("NoiseOutput[id] = EvaluateNoise(uv)")) "channel contract changed"
Assert-True ($noise.Contains("RWStructuredBuffer<float4> ProbeOutput : register(u1)")) "probe buffer binding changed"
Assert-True ($noise.Contains("ProbeSamplerWrap")) "sampler probe missing"
Assert-True ($noise.Contains("SampleLevel(LinearWrapSampler")) "sampler probe sampling changed"
Assert-True ($sourceText.Contains("D3D12_TEXTURE_ADDRESS_MODE_WRAP")) "sampler wrap state missing"
Assert-True ($viewer.Contains("SliceIndex + 0.5")) "slice-center contract changed"
Assert-True ($noise.Contains("id >= uint3(Resolution, Resolution, Resolution)")) "bounds guard changed"
Assert-True ($sourceText.Contains("kSrvUavDescriptorCount = 16")) "cloud descriptor heap capacity changed"
Assert-True ($sourceText.Contains("CloudFrameConstants")) "cloud constants missing"
Assert-True ($sourceText.Contains("D3D12_QUERY_HEAP_TYPE_TIMESTAMP")) "GPU timestamp query missing"
Assert-True ($sourceText.Contains("ResolveQueryData")) "GPU timestamp resolve missing"
Assert-True ($cloudMarch.Contains("RWTexture2D<float4> CloudOutput")) "cloud UAV binding changed"
Assert-True ($cloudComposite.Contains("CloudTexture.SampleLevel")) "cloud composite sampling missing"
Assert-True ($sourceText.Contains("WindOffsetWorld")) "wind world-offset contract missing"
Assert-True ($cloudMarch.Contains("position.x - windOffset.x")) "wind subtraction contract missing"
Assert-True ($cloudMarch.Contains("executedSteps")) "executed step counter missing"
Assert-True ($sourceText.Contains("CloudDebugMode::Occupancy")) "occupancy debug mode missing"
Assert-True ($sourceText.Contains("--time=")) "fixed cloud time hook missing"

Write-Output "PASS: V1 source checks"
Write-Output "  isolation: no engine renderer/runtime references"
Write-Output "  shaders: Noise + slices + CloudRayMarch/CloudComposite entry points"
Write-Output "  cloud: per-frame output, descriptor capacity, timestamp queries"
Write-Output "  readback/capture: copyable footprints and COPY_SOURCE transitions"
