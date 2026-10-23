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
    src / "Validation.cpp",
    src / "Window.cpp",
    src / "AuditPolicy.h",
    root / "tests" / "audit_policy_test.cpp",
    shaders / "NoiseGenerate.hlsl",
    shaders / "SliceViewer.hlsl",
    shaders / "CloudRayMarch.hlsl",
    shaders / "CloudComposite.hlsl",
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

print("PASS: V1 source checks")
print("  isolation: no engine renderer/runtime references")
print("  shaders: Noise + slices + CloudRayMarch/CloudComposite entry points")
print("  cloud: per-frame output, descriptor capacity, timestamp queries")
print("  readback/capture: copyable footprints and COPY_SOURCE transitions")
