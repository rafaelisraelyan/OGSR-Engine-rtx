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
    (Join-Path $src "Validation.cpp"),
    (Join-Path $src "Window.cpp"),
    (Join-Path $src "AuditPolicy.h"),
    (Join-Path $root "tests\audit_policy_test.cpp"),
    (Join-Path $shaders "NoiseGenerate.hlsl"),
    (Join-Path $shaders "SliceViewer.hlsl")
)
foreach ($path in $required) {
    Assert-True (Test-Path $path) "missing $path"
}

$sourceText = (Get-ChildItem $src -Filter *.cpp | Get-Content -Raw) -join "`n"
$sourceText += (Get-ChildItem $src -Filter *.h | Get-Content -Raw) -join "`n"
$cmake = Get-Content -Raw (Join-Path $root "CMakeLists.txt")
$noise = Get-Content -Raw (Join-Path $shaders "NoiseGenerate.hlsl")
$viewer = Get-Content -Raw (Join-Path $shaders "SliceViewer.hlsl")

foreach ($forbidden in @("xrCore", "xrRenderDX12", "xrRender_R4", "Game/", "Game\")) {
    Assert-True (-not $sourceText.Contains($forbidden)) "forbidden engine reference: $forbidden"
    Assert-True (-not $cmake.Contains($forbidden)) "forbidden engine reference in CMake: $forbidden"
}
foreach ($library in @("d3d12", "dxgi", "d3dcompiler", "user32")) {
    Assert-True ($cmake.Contains($library)) "missing system library: $library"
}

$contracts = @(
    @("GenerateNoise", "cs_5_1"),
    @("ProbePeriodicity", "cs_5_1"),
    @("ProbeSamplerWrap", "cs_5_1"),
    @("FullscreenVS", "vs_5_1"),
    @("SlicePS", "ps_5_1")
)
foreach ($contract in $contracts) {
    $entry = $contract[0]
    $profile = $contract[1]
    $pattern = "CompileShaderFile(?s).*?`"$entry`".*?`"$profile`""
    Assert-True ($sourceText -match $pattern) "missing CompileShaderFile contract for $entry/$profile"
    if ($entry -eq "GenerateNoise" -or $entry -eq "ProbePeriodicity" -or $entry -eq "ProbeSamplerWrap") {
        Assert-True ($noise -match "\b$entry\s*\(") "missing shader entry $entry"
    } else {
        Assert-True ($viewer -match "\b$entry\s*\(") "missing shader entry $entry"
    }
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

Write-Output "PASS: V0 source checks"
Write-Output "  isolation: no engine renderer/runtime references"
Write-Output "  shaders: GenerateNoise/ProbePeriodicity/ProbeSamplerWrap cs_5_1, FullscreenVS/vs_5_1, SlicePS/ps_5_1"
Write-Output "  compute: numthreads(8,8,4), UAV barrier, SRV transition"
Write-Output "  readback/capture: copyable footprints and COPY_SOURCE transitions"
