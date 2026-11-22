"""Source-level checks for the standalone V0 clouds test.

These checks do not replace a Windows build or a GPU run. They guard the
isolation boundary and the shader/CPP contract that is easy to break while
iterating on the noise math.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parent.parent
src = root / "src"
shaders = root / "shaders"
scripts = root / "scripts"

required = [
    src / "main.cpp",
    src / "Application.cpp",
    src / "Renderer.cpp",
    src / "NoiseVolume.cpp",
    src / "SliceViewer.cpp",
    src / "CloudRayMarcher.cpp",
    src / "CloudCompositor.cpp",
    src / "CloudProfiler.cpp",
    src / "CloudTypes.h",
    src / "WeatherMap.cpp",
    src / "WeatherMap.h",
    src / "Validation.cpp",
    src / "Window.cpp",
    src / "AuditPolicy.h",
    root / "tests" / "audit_policy_test.cpp",
    shaders / "NoiseGenerate.hlsl",
    shaders / "SliceViewer.hlsl",
    shaders / "CloudRayMarch.hlsl",
    shaders / "CloudReferenceDensity.hlsl",
    shaders / "CloudDensityVolume.hlsl",
    shaders / "CloudComposite.hlsl",
    shaders / "WeatherGenerate.hlsl",
]
for path in required:
    assert path.is_file(), f"missing {path}"

sources = "\n".join(
    path.read_text(encoding="utf-8")
    for path in list(src.glob("*.cpp")) + list(src.glob("*.h"))
)
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
noise = (shaders / "NoiseGenerate.hlsl").read_text(encoding="utf-8")
viewer = (shaders / "SliceViewer.hlsl").read_text(encoding="utf-8")
cloud_march = (shaders / "CloudRayMarch.hlsl").read_text(encoding="utf-8")
cloud_composite = (shaders / "CloudComposite.hlsl").read_text(encoding="utf-8")
weather = (shaders / "WeatherGenerate.hlsl").read_text(encoding="utf-8")

# Isolation: this tool must never touch the game renderer.
for forbidden in ("xrCore", "xrRenderDX12", "xrRender_R4", "Game/", "Game\\"):
    assert forbidden not in sources, f"forbidden engine reference: {forbidden}"
    assert forbidden not in cmake, f"forbidden engine reference in CMake: {forbidden}"

for library in ("d3d12", "dxgi", "d3dcompiler", "user32"):
    assert library in cmake, f"missing system library: {library}"

# Entry points must exist and match the profiles compiled by the C++ code.
contracts = {
    "GenerateNoise": ("cs_5_1", noise),
    "ProbePeriodicity": ("cs_5_1", noise),
    "ProbeSamplerWrap": ("cs_5_1", noise),
    "CloudRayMarch": ("cs_5_1", cloud_march),
    "FullscreenVS": ("vs_5_1", viewer),
    "SlicePS": ("ps_5_1", viewer),
    "CloudCompositePS": ("ps_5_1", cloud_composite),
    "GenerateWeather": ("cs_5_1", weather),
}
for entry, (profile, shader) in contracts.items():
    assert re.search(rf"CompileShaderFile\([^;]*\"{entry}\"[^;]*\"{profile}\"", sources, re.S), \
        f"missing CompileShaderFile contract for {entry}/{profile}"
    assert re.search(rf"\b{entry}\s*\(", shader), f"missing shader entry {entry}"
assert re.search(r"\bFullscreenVS\s*\(", cloud_composite), "missing cloud composite VS"

assert "numthreads(8, 8, 4)" in noise, "thread group contract changed"
assert "kThreadGroupX = 8" in sources and "kThreadGroupZ = 4" in sources
assert "D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE" in sources
assert "D3D12_RESOURCE_BARRIER_TYPE_UAV" in sources
assert "InsertUavBarrier" in sources
assert "D3D12_RESOURCE_STATE_COPY_SOURCE" in sources
assert "GetCopyableFootprints" in sources
assert "audit::ClassifyFenceCompletion" in sources
assert "audit::EvaluateValidationStatus" in sources
assert "InfoQueueAvailable" in sources
assert "--test-no-validation" in sources
assert "--test-no-info-queue" in sources

# The four channels are documented in one place and written in one place.
assert "NoiseOutput[id] = EvaluateNoise(uv)" in noise
assert "RWStructuredBuffer<float4> ProbeOutput : register(u1)" in noise
assert "ProbeSamplerWrap" in noise and "SampleLevel(LinearWrapSampler" in noise
assert "AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP" in sources

assert "SliceIndex + 0.5" in viewer
assert "id >= uint3(Resolution, Resolution, Resolution)" in noise
assert "kSrvUavDescriptorCount = 16" in sources
assert "CloudFrameConstants" in sources
assert "D3D12_QUERY_HEAP_TYPE_TIMESTAMP" in sources
assert "ResolveQueryData" in sources
assert "RWTexture2D<float4> CloudOutput" in cloud_march
assert "CloudTexture.SampleLevel" in cloud_composite
assert "WindOffsetWorld" in sources
assert "position.x - windOffset.x" in cloud_march
assert "executedSteps" in cloud_march
assert "CloudDebugMode::Occupancy" in sources
assert "--time=" in sources
assert "WeatherMap" in sources
assert "kUavSlot = 9" in sources
assert "kSrvSlot = 10" in sources
assert "Texture2D<float4> WeatherTexture : register(t1)" in cloud_march
assert "WeatherWorldPosition" in cloud_march
assert "GenerateWeather" in weather
assert "CloudDebugMode::Weather" in sources
assert "CloudDebugMode::CloudType" in sources
assert "ApplyPartlyPreset" in sources
assert "ApplyRainPreset" in sources
assert "ApplyStormPreset" in sources
assert "ApplySunsetPreset" in sources
assert "--cloud-scale=" in sources
assert "--weather-seed=" in sources
assert "VK_F5" in sources
assert "WeatherMapSeamOk" in sources
assert "CloudDebugMode::DensitySlice" in sources
assert "VolumeBottom" in cloud_march
assert "WeatherC.z" in cloud_march
assert "CloudAblationMode" in sources
assert "--ablation=" in sources
assert "ProbeWeatherSampler" in weather
assert "kProbeUavSlot = 11" in sources
assert "weatherEvolution" in sources
assert "CloudAblationMode::NoWarp" in sources
assert "CloudAblationMode::MacroOnly" in sources
assert "CloudAblationMode::NoShadow" in sources
assert "--scene=" in sources
assert "--noise-slice-capture" in sources
assert "--vertical-noise=" in sources
assert "SliceAxis" in viewer
assert "ResetStatistics" in sources

assert "CloudLightingMode" in sources
assert "CloudJitterMode" in sources
assert "--shadow-steps=" in sources
assert "--sun-shadow-distance=" in sources
assert "--jitter=" in sources
assert "--lighting=" in sources
assert "--atmosphere=" in sources
assert "--profile=" in sources
assert "--sequence=" in sources
assert "RunCloudSequence" in sources
assert "SunRaySegment" in cloud_march
assert "AtmosphereOpticalDepth" in cloud_march
assert "AtmosphereColor" in cloud_march
assert "Lighting.x" in cloud_march
assert "Lighting.y" in cloud_march
assert "SampleCloudDensityForLight(float3 position)" in cloud_march
assert "bool detailed" not in cloud_march
assert "CloudDebugMode::SunOpticalDepth" in sources
assert "CloudDebugMode::ShadowDensityDifference" in sources
assert "CloudDebugMode::AtmosphereTransmittance" in sources
assert "CloudDebugMode::ShadowPathLength" in sources
assert "shadow/primary density max difference" in sources
assert "sun shadow path max" in sources
assert "31 * 16" in sources
assert "DensityHorizontal" in sources
assert "densitySliceHeight" in sources
assert "selfShadowEnabled" in sources
assert "CloudAblationMode::Base3D" in sources
assert "CloudAblationMode::NoBaseWarp" in sources
assert "CloudAblationMode::NoHeightVariation" in sources
assert "CloudAblationMode::NoAnisotropy" in sources
assert "CloudAblationMode::FlatBottom" in sources
assert "CloudAblationMode::NoTypeHeightProfile" in sources
assert "CloudAblationMode::NoTypeThresholds" in sources
assert "CloudAblationMode::NoTypeAnisotropy" in sources
assert "CloudAblationMode::NoTypeLocalBounds" in sources
assert "CloudAblationMode::NoTypeShapeBlending" in sources
assert "RayProfile" in sources
assert "DensityVerticalZ" in sources
assert "CloudProbeReadback" in sources
assert "ReadbackProbe" in sources
assert "probe_float32.bin" in sources
assert "FileSha256" in sources
assert "BCryptHash" in sources
assert "OGSR_PROBE" in cloud_march
assert (shaders / "CloudRayMarchProbe.hlsl").exists()
assert "densitySliceExtentKm" in sources
assert "probeColumn" in sources
assert "topFade" in sources
assert "CloudDensityDebug" in cloud_march
assert "ShapePrototype" in sources
assert "pipelineStateProtoA_" in sources
assert "MacroShape" in sources
assert (shaders / "CloudRayMarchPrototypeA.hlsl").exists()
assert (shaders / "CloudRayMarchPrototypeB.hlsl").exists()
assert "CloudFormula" in sources
assert "pipelineStateFormula_" in sources
assert "CrossTypeDifference" in sources
assert (shaders / "CloudRayMarchFormulaF1.hlsl").exists()
assert (shaders / "CloudRayMarchFormulaF2.hlsl").exists()
assert (shaders / "CloudRayMarchFormulaF3.hlsl").exists()
assert (shaders / "CloudRayMarchFormulaF4.hlsl").exists()
assert (shaders / "CloudRayMarchFormulaF5.hlsl").exists()
# Probe permutations must match the selected runtime formula/prototype.
# A probe PSO compiled without OGSR_FORMULA reproduces the old silent F0 bug.
for index in range(1, 6):
    wrapper = shaders / f"CloudRayMarchProbeFormulaF{index}.hlsl"
    assert wrapper.is_file(), f"missing formula probe wrapper: {wrapper}"
    content = wrapper.read_text(encoding="utf-8")
    assert "#define OGSR_PROBE 1" in content
    assert f"#define OGSR_FORMULA {index}" in content
    assert '#include "CloudRayMarch.hlsl"' in content
    assert wrapper.name in sources and wrapper.name in cmake
for label, number in (("A", 1), ("B", 2)):
    wrapper = shaders / f"CloudRayMarchProbePrototype{label}.hlsl"
    assert wrapper.is_file(), f"missing shape probe wrapper: {wrapper}"
    content = wrapper.read_text(encoding="utf-8")
    assert "#define OGSR_PROBE 1" in content
    assert f"#define OGSR_SHAPE_PROTOTYPE {number}" in content
    assert '#include "CloudRayMarch.hlsl"' in content
    assert wrapper.name in sources and wrapper.name in cmake
assert "pipelineStateProbeFormula_[index].Get()" in sources
assert "cloud probe PSO selected:" in sources
# V2.3h reference density model must have isolated render/probe permutations.
assert (shaders / "CloudReferenceDensity.hlsl").is_file()
assert (shaders / "CloudRayMarchReference.hlsl").is_file()
assert (shaders / "CloudRayMarchProbeReference.hlsl").is_file()
reference_wrapper = (shaders / "CloudRayMarchReference.hlsl").read_text(encoding="utf-8")
assert "#define OGSR_DENSITY_MODEL 1" in reference_wrapper
assert '#include "CloudRayMarch.hlsl"' in reference_wrapper
probe_reference = (shaders / "CloudRayMarchProbeReference.hlsl").read_text(encoding="utf-8")
assert "#define OGSR_PROBE 1" in probe_reference
assert "#define OGSR_DENSITY_MODEL 1" in probe_reference
assert "CloudReferenceDensity.hlsl" in cloud_march
reference_density = (shaders / "CloudReferenceDensity.hlsl").read_text(
    encoding="utf-8")
assert "SampleReferenceBaseShape" in reference_density
assert "ReferenceHeightProfile" in reference_density
assert "pipelineStateReference_" in sources
assert "pipelineStateProbeReference_" in sources
assert "cloud render PSO selected:" in sources
assert "Density model requested:" in sources
assert "Probe PSO:" in sources
# V2.3h-A01 noise-field isolation controls and raw Float32 probe.
assert "ReferenceNoiseMix" in sources
assert "ReferenceCoordinateMode" in sources
assert "ReferenceControls" in sources
assert "ReferenceFieldMode" in sources
assert "multi-r" in sources
assert "hash-mod" in sources
assert "ReferenceValueNoise" in reference_density
assert "ReferenceStorm" in reference_density
assert "storm-volume" in sources
assert "ReferenceHeightProfileAt" in reference_density
assert "ReferenceDensityFields" in reference_density
assert "ReferenceStormColumnBounds" in reference_density
assert "ReferenceMacroField" in reference_density
assert "ReferenceMacroMass" in reference_density
assert "NonTilingMacro" in sources
assert "macro-scale" in sources
assert "ReferenceCoverageRemap" in reference_density
assert "macro-probe" in sources
assert "macro_probe_float32" in sources
assert "macro-coverage" in sources
assert "macro-coverage-calibrate" in sources
assert "macro-detail-compensate" in sources
assert "macro-seed-hash" in sources
assert "storm-gap" in sources
assert "CloudRayMarchReferenceA03a" in sources
assert "CloudDensityVolumeA03a" in sources
assert "pipelineStateDensityColumnsA03a" in sources
assert "CloudRayMarchReferenceA03b" in sources
assert "CloudDensityVolumeA03b" in sources
assert "pipelineStateDensityColumnsA03b" in sources
assert "columns-crosscheck" in sources
assert "macro-weather" in sources
assert "local-occupancy" in sources
assert "storm-local" in sources
assert "density-test" in sources
assert "shader_permutation" in sources
assert "NormalQuantile" in sources
assert "referenceMacro4" in sources
assert "storm-column" in sources
assert "CloudDensityVolume" in sources
assert "NoiseField" in sources
assert "noise-field" in sources
assert "referenceShape" in sources
assert "referenceDebug" in sources
assert "ReferenceBaseNoiseCoordinate" in reference_density
assert "ReferenceShapeFieldValue" in reference_density
assert "ProbeOutput[probeIndex" in cloud_march
assert "WriteNoiseFieldFiles" in sources
assert (scripts / "analyze_noise_field.py").is_file()
assert (scripts / "match_reference_mixes.py").is_file()
assert (scripts / "compare_capture_diffs.ps1").is_file()

assert (scripts / "measure_v23_performance.ps1").exists()
assert (scripts / "run_v23_evidence.ps1").exists()
assert "PollDebugMessages" in sources
assert "InjectDebugError" in sources
assert "--test-inject-debug-error" in sources
assert "final validation after cloud GPU work" in sources
assert "renderer_.WaitForGpu()" in sources
assert "DebugErrorCount() != 0" in sources
assert "sun below horizon removes direct" in sources
assert "multiScatterVisibility" in cloud_march
assert "sunAboveHorizon" in cloud_march

print("PASS: V2.3 source checks")
print("  isolation: no engine renderer/runtime references")
print("  shaders: Noise + slices (XY/XZ/YZ) + CloudRayMarch/CloudComposite/WeatherGenerate")
print("  cloud: conservative bounds, weather slots 9/10/11, ablation, density-slice")
print("  diagnostics: scene filter, noise slices, vertical scale, RGBA sampler probe")
print("  lighting: clipped sun path, shared density, sky ambient, analytic atmosphere")
print("  qa: jitter modes, deterministic sequences, quality profiles, perf matrix")
