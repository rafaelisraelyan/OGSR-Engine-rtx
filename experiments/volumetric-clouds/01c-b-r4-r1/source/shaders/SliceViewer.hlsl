// OGSR Volumetric Clouds V0 - fullscreen slice viewer.
// The 3D texture remains in ALL_SHADER_RESOURCE state for the whole frame.
// Sampling uses a static linear-wrap sampler, which also lets the diagnostic
// mode tile the volume 3x3 and expose any discontinuity at the seams.

#pragma warning(disable: 4000)

Texture3D<float4> NoiseTexture : register(t0);
SamplerState LinearWrap : register(s0);

cbuffer ViewParams : register(b0)
{
    float SliceIndex;   // continuous [0, Resolution-1]
    uint Channel;       // 0 Perlin, 1 Worley, 2 combined
    uint Diagnostic;    // 0 normal, 1 3x3 seam inspection
    uint Resolution;

    float Time;
    uint SliceAxis;     // 0 XY, 1 XZ, 2 YZ
    uint Reserved1;
    uint Reserved2;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput FullscreenVS(uint vertexID : SV_VertexID)
{
    // Oversized triangle covering the viewport; UV is 0..1 in the visible area.
    const float2 v = float2((vertexID << 1) & 2, vertexID & 2);

    VertexOutput output;
    output.position = float4(v * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = v;
    return output;
}

float SelectChannel(float4 value, uint channel)
{
    return channel == 0 ? value.r : (channel == 1 ? value.g : value.b);
}

float3 AxisUv(float2 planeUv, float z, uint axis)
{
    if (axis == 1)
        return float3(planeUv.x, z, planeUv.y); // XZ
    if (axis == 2)
        return float3(z, planeUv.x, planeUv.y); // YZ
    return float3(planeUv, z);                  // XY
}

float4 SlicePS(VertexOutput input) : SV_Target0
{
    // SliceIndex is in texel units, so texel centers map to (i + 0.5) / N.
    // Using a continuous index keeps animation smooth; the title reports the
    // nearest integer slice.
    const float z = (SliceIndex + 0.5) / float(Resolution);

    if (Diagnostic != 0)
    {
        // Repeat the slice three times in X and Y. If generation is periodic,
        // the 3x3 tiling has no visible discontinuity at the internal borders.
        // Only the outer image frame is tinted; internal joins stay untouched.
        const float2 tiledUv = input.uv * 3.0;
        const float4 noise = NoiseTexture.SampleLevel(
            LinearWrap, AxisUv(tiledUv, z, SliceAxis), 0);
        const float value = SelectChannel(noise, Channel);

        const float2 edge = min(input.uv, 1.0 - input.uv);
        const bool frame = edge.x < 0.002 || edge.y < 0.002;
        const float3 color = frame ? float3(0.85, 0.12, 0.08) : float3(value, value, value);
        return float4(color, 1.0);
    }

    const float4 noise = NoiseTexture.SampleLevel(
        LinearWrap, AxisUv(input.uv, z, SliceAxis), 0);
    const float value = SelectChannel(noise, Channel);
    return float4(value, value, value, 1.0);
}
