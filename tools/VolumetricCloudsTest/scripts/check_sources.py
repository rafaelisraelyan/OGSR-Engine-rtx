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
    src / "Validation.cpp",
    src / "Window.cpp",
    src / "AuditPolicy.h",
    root / "tests" / "audit_policy_test.cpp",
    shaders / "NoiseGenerate.hlsl",
    shaders / "SliceViewer.hlsl",
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

# Isolation: this tool must never touch the game renderer.
for forbidden in ("xrCore", "xrRenderDX12", "xrRender_R4", "Game/", "Game\\"):
    assert forbidden not in sources, f"forbidden engine reference: {forbidden}"
    assert forbidden not in cmake, f"forbidden engine reference in CMake: {forbidden}"

for library in ("d3d12", "dxgi", "d3dcompiler", "user32"):
    assert library in cmake, f"missing system library: {library}"

# Entry points must exist and match the profiles compiled by the C++ code.
contracts = {
    "GenerateNoise": "cs_5_1",
    "ProbePeriodicity": "cs_5_1",
    "ProbeSamplerWrap": "cs_5_1",
    "FullscreenVS": "vs_5_1",
    "SlicePS": "ps_5_1",
}
for entry, profile in contracts.items():
    assert re.search(rf"CompileShaderFile\([^;]*\"{entry}\"[^;]*\"{profile}\"", sources, re.S), \
        f"missing CompileShaderFile contract for {entry}/{profile}"
for entry in ("GenerateNoise", "ProbePeriodicity", "ProbeSamplerWrap",
              "FullscreenVS", "SlicePS"):
    shader = noise if entry in ("GenerateNoise", "ProbePeriodicity",
                                "ProbeSamplerWrap") else viewer
    assert re.search(rf"\b{entry}\s*\(", shader), f"missing shader entry {entry}"

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

print("PASS: V0 source checks")
print("  isolation: no engine renderer/runtime references")
print("  shaders: GenerateNoise/ProbePeriodicity/ProbeSamplerWrap cs_5_1, FullscreenVS/vs_5_1, SlicePS/ps_5_1")
print("  compute: numthreads(8,8,4), UAV barrier, SRV transition")
print("  readback/capture: copyable footprints and COPY_SOURCE transitions")
