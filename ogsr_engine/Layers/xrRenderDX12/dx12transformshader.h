#pragma once

// F3.5.6B: single HLSL source shared by the per-object SmokeMesh12 path and
// the indexed geometry registry. Keep this file ASCII-only: MSVC on CI uses
// code page 1252 for embedded shader string literals.
namespace dx12transformshader12
{
inline constexpr const char* kVertexSource = R"(
struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color    : COLOR;
};

cbuffer FrameCB : register(b0)
{
    float4x4 ViewProjection;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 World;
    float4 Tint;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    // Row-vector convention: local -> World -> ViewProjection.
    output.position = mul(mul(float4(input.position, 1.0f), World), ViewProjection);
    output.color = Tint;
    return output;
}
)";

inline constexpr const char* kPixelSource = R"(
struct PSInput
{
    float4 position : SV_Position;
    float4 color    : COLOR;
};

float4 main(PSInput input) : SV_Target
{
    return input.color;
}
)";
} // namespace dx12transformshader12
