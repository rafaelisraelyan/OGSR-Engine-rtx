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
    (Join-Path $src "WeatherMap.cpp"),
    (Join-Path $src "WeatherMap.h"),
    (Join-Path $src "Validation.cpp"),
    (Join-Path $src "Window.cpp"),
    (Join-Path $src "AuditPolicy.h"),
    (Join-Path $root "tests\audit_policy_test.cpp"),
    (Join-Path $shaders "NoiseGenerate.hlsl"),
    (Join-Path $shaders "SliceViewer.hlsl"),
    (Join-Path $shaders "CloudRayMarch.hlsl"),
    (Join-Path $shaders "CloudComposite.hlsl"),
    (Join-Path $shaders "WeatherGenerate.hlsl")
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
$weather = Get-Content -Raw (Join-Path $shaders "WeatherGenerate.hlsl")

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
Assert-True ($sourceText.Contains("WeatherMap")) "weather map owner missing"
Assert-True ($sourceText.Contains("kUavSlot = 9")) "weather map UAV slot changed"
Assert-True ($sourceText.Contains("kSrvSlot = 10")) "weather map SRV slot changed"
Assert-True ($cloudMarch.Contains("Texture2D<float4> WeatherTexture : register(t1)")) "weather SRV binding missing"
Assert-True ($cloudMarch.Contains("WeatherWorldPosition")) "weather world-space contract missing"
Assert-True ($weather.Contains("GenerateWeather")) "weather generation entry point missing"
Assert-True ($sourceText.Contains("CloudDebugMode::Weather")) "weather debug mode missing"
Assert-True ($sourceText.Contains("CloudDebugMode::CloudType")) "cloud-type debug mode missing"
Assert-True ($sourceText.Contains("ApplyPartlyPreset")) "partly preset missing"
Assert-True ($sourceText.Contains("ApplyRainPreset")) "rain preset missing"
Assert-True ($sourceText.Contains("ApplyStormPreset")) "storm preset missing"
Assert-True ($sourceText.Contains("ApplySunsetPreset")) "sunset preset missing"
Assert-True ($sourceText.Contains("--cloud-scale=")) "cloud scale option missing"
Assert-True ($sourceText.Contains("--weather-seed=")) "weather seed option missing"
Assert-True ($sourceText.Contains("VK_F5")) "weather transition key missing"
Assert-True ($sourceText.Contains("WeatherMapSeamOk")) "weather seam test missing"
Assert-True ($sourceText.Contains("CloudDebugMode::DensitySlice")) "density slice mode missing"
Assert-True ($cloudMarch.Contains("VolumeBottom")) "conservative volume bounds missing"
Assert-True ($cloudMarch.Contains("WeatherC.z")) "volume margin missing"
Assert-True ($sourceText.Contains("CloudAblationMode")) "ablation mode class missing"
Assert-True ($sourceText.Contains("--ablation=")) "ablation CLI missing"
Assert-True ($weather.Contains("ProbeWeatherSampler")) "weather sampler probe missing"
Assert-True ($sourceText.Contains("kProbeUavSlot = 11")) "weather probe descriptor slot missing"
Assert-True ($sourceText.Contains("weatherEvolution")) "weather evolution reservation marker missing"
Assert-True ($sourceText.Contains("CloudAblationMode::NoWarp")) "no-warp ablation missing"
Assert-True ($sourceText.Contains("CloudAblationMode::MacroOnly")) "macro-only ablation missing"
Assert-True ($sourceText.Contains("CloudAblationMode::NoShadow")) "no-shadow ablation missing"
Assert-True ($sourceText.Contains("--scene=")) "single-scene diagnostic missing"
Assert-True ($sourceText.Contains("--noise-slice-capture")) "noise slice capture missing"
Assert-True ($sourceText.Contains("--vertical-noise=")) "vertical noise diagnostic missing"
Assert-True ($viewer.Contains("SliceAxis")) "slice axis contract missing"
Assert-True ($sourceText.Contains("ResetStatistics")) "profiler reset missing"
Assert-True ($cmake.Contains("WeatherMap.cpp")) "weather source not in CMake"
Assert-True ($cmake.Contains("WeatherGenerate.hlsl")) "weather shader validation missing"

# V2.3 lighting, atmosphere, jitter and quality profiles.
Assert-True ($sourceText.Contains("CloudLightingMode")) "lighting mode class missing"
Assert-True ($sourceText.Contains("CloudJitterMode")) "jitter mode class missing"
Assert-True ($sourceText.Contains("--shadow-steps=")) "shadow step CLI missing"
Assert-True ($sourceText.Contains("--sun-shadow-distance=")) "sun shadow distance CLI missing"
Assert-True ($sourceText.Contains("--jitter=")) "jitter CLI missing"
Assert-True ($sourceText.Contains("--lighting=")) "lighting CLI missing"
Assert-True ($sourceText.Contains("--atmosphere=")) "atmosphere CLI missing"
Assert-True ($sourceText.Contains("--profile=")) "quality profile CLI missing"
Assert-True ($sourceText.Contains("--sequence=")) "sequence capture missing"
Assert-True ($sourceText.Contains("RunCloudSequence")) "sequence runner missing"
Assert-True ($cloudMarch.Contains("SunRaySegment")) "sun ray segment clipping missing"
Assert-True ($cloudMarch.Contains("AtmosphereOpticalDepth")) "atmospheric optical depth missing"
Assert-True ($cloudMarch.Contains("AtmosphereColor")) "atmospheric colour missing"
Assert-True ($cloudMarch.Contains("Lighting.x")) "sun shadow distance cap not used"
Assert-True ($cloudMarch.Contains("Lighting.y")) "shadow step count not used"
Assert-True ($cloudMarch.Contains("SampleCloudDensityForLight(float3 position)")) "light density wrapper missing"
Assert-True (-not $cloudMarch.Contains("bool detailed")) "density geometry split must not return"
Assert-True ($sourceText.Contains("CloudDebugMode::SunOpticalDepth")) "sun optical depth debug missing"
Assert-True ($sourceText.Contains("CloudDebugMode::ShadowDensityDifference")) "shadow density diff debug missing"
Assert-True ($sourceText.Contains("CloudDebugMode::AtmosphereTransmittance")) "atmosphere debug missing"
Assert-True ($sourceText.Contains("CloudDebugMode::ShadowPathLength")) "shadow path debug missing"
Assert-True ($sourceText.Contains("shadow/primary density max difference")) "shadow consistency selftest missing"
Assert-True ($sourceText.Contains("sun shadow path max")) "shadow path selftest missing"
Assert-True ($sourceText.Contains("31 * 16")) "constant buffer row count not updated"
Assert-True ($sourceText.Contains("DensityHorizontal")) "horizontal density slice missing"
Assert-True ($sourceText.Contains("densitySliceHeight")) "slice height parameter missing"
Assert-True ($sourceText.Contains("selfShadowEnabled")) "self-shadow diagnostic missing"
Assert-True ($sourceText.Contains("CloudAblationMode::Base3D")) "base-3d ablation missing"
Assert-True ($sourceText.Contains("CloudAblationMode::NoBaseWarp")) "no-base-warp ablation missing"
Assert-True ($sourceText.Contains("CloudAblationMode::NoHeightVariation")) "no-height-variation ablation missing"
Assert-True ($sourceText.Contains("CloudAblationMode::NoAnisotropy")) "no-anisotropy ablation missing"
Assert-True ($sourceText.Contains("CloudAblationMode::FlatBottom")) "flat-bottom ablation missing"
Assert-True ($sourceText.Contains("CloudAblationMode::NoTypeHeightProfile")) "no-type-height-profile ablation missing"
Assert-True ($sourceText.Contains("CloudAblationMode::NoTypeThresholds")) "no-type-thresholds ablation missing"
Assert-True ($sourceText.Contains("CloudAblationMode::NoTypeAnisotropy")) "no-type-anisotropy ablation missing"
Assert-True ($sourceText.Contains("CloudAblationMode::NoTypeLocalBounds")) "no-type-local-bounds ablation missing"
Assert-True ($sourceText.Contains("CloudAblationMode::NoTypeShapeBlending")) "no-type-shape-blending ablation missing"
Assert-True ($sourceText.Contains("RayProfile")) "ray-profile debug mode missing"
Assert-True ($sourceText.Contains("DensityVerticalZ")) "vertical Z slice missing"
Assert-True ($sourceText.Contains("CloudProbeReadback")) "float32 probe readback type missing"
Assert-True ($sourceText.Contains("ReadbackProbe")) "float32 probe readback missing"
Assert-True ($sourceText.Contains("probe_float32.bin")) "float32 probe file missing"
Assert-True ($sourceText.Contains("FileSha256")) "running EXE SHA256 export missing"
Assert-True ($sourceText.Contains("BCryptHash")) "SHA256 backend missing"
Assert-True ($cloudMarch.Contains("OGSR_PROBE")) "probe shader permutation guard missing"
Assert-True (Test-Path (Join-Path $shaders "CloudRayMarchProbe.hlsl")) "probe shader file missing"
Assert-True ($sourceText.Contains("densitySliceExtentKm")) "slice extent parameter missing"
Assert-True ($sourceText.Contains("probeColumn")) "ray-profile column parameter missing"
Assert-True ($sourceText.Contains("topFade")) "top fade diagnostic missing"
Assert-True ($cloudMarch.Contains("CloudDensityDebug")) "ray-profile component capture missing"
Assert-True ($sourceText.Contains("ShapePrototype")) "shape prototype enum missing"
Assert-True ($sourceText.Contains("pipelineStateProtoA_")) "prototype A pipeline missing"
Assert-True ($sourceText.Contains("MacroShape")) "macro-shape debug missing"
Assert-True (Test-Path (Join-Path $shaders "CloudRayMarchPrototypeA.hlsl")) "prototype A shader missing"
Assert-True (Test-Path (Join-Path $shaders "CloudRayMarchPrototypeB.hlsl")) "prototype B shader missing"
Assert-True ($sourceText.Contains("CloudFormula")) "cloud formula enum missing"
Assert-True ($sourceText.Contains("pipelineStateFormula_")) "formula pipeline array missing"
Assert-True ($sourceText.Contains("CrossTypeDifference")) "cross-type difference debug missing"
Assert-True (Test-Path (Join-Path $shaders "CloudRayMarchFormulaF1.hlsl")) "formula F1 shader missing"
Assert-True (Test-Path (Join-Path $shaders "CloudRayMarchFormulaF2.hlsl")) "formula F2 shader missing"
Assert-True (Test-Path (Join-Path $shaders "CloudRayMarchFormulaF3.hlsl")) "formula F3 shader missing"
Assert-True (Test-Path (Join-Path $shaders "CloudRayMarchFormulaF4.hlsl")) "formula F4 shader missing"
Assert-True (Test-Path (Join-Path $shaders "CloudRayMarchFormulaF5.hlsl")) "formula F5 shader missing"
Assert-True (Test-Path (Join-Path $PSScriptRoot "measure_v23_performance.ps1")) "V2.3 performance script missing"
Assert-True (Test-Path (Join-Path $PSScriptRoot "run_v23_evidence.ps1")) "V2.3 evidence script missing"

# V2.3 final delta: validation must be re-evaluated after cloud GPU work and a
# sun below the horizon must remove direct and multiple scattering.
Assert-True ($sourceText.Contains("PollDebugMessages")) "final debug drain missing"
Assert-True ($sourceText.Contains("InjectDebugError")) "injected debug error hook missing"
Assert-True ($sourceText.Contains("--test-inject-debug-error")) "injected error CLI missing"
Assert-True ($sourceText.Contains("final validation after cloud GPU work")) "final validation report missing"
Assert-True ($sourceText.Contains("renderer_.WaitForGpu()")) "GPU wait before final verdict missing"
Assert-True ($sourceText.Contains("DebugErrorCount() != 0")) "post-shutdown error check missing"
Assert-True ($sourceText.Contains("sun below horizon removes direct")) "sun-below-horizon selftest missing"
Assert-True ($cloudMarch.Contains("multiScatterVisibility")) "multiple scattering visibility missing"
Assert-True ($cloudMarch.Contains("sunAboveHorizon")) "horizon visibility term missing"

Write-Output "PASS: V2.3 source checks"
Write-Output "  isolation: no engine renderer/runtime references"
Write-Output "  shaders: Noise + slices (XY/XZ/YZ) + CloudRayMarch/CloudComposite/WeatherGenerate"
Write-Output "  cloud: conservative bounds, weather slots 9/10/11, ablation, density-slice"
Write-Output "  diagnostics: scene filter, noise slices, vertical scale, RGBA sampler probe"
Write-Output "  lighting: clipped sun path, shared density, sky ambient, analytic atmosphere"
Write-Output "  qa: jitter modes, deterministic sequences, quality profiles, perf matrix"
