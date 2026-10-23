// OGSR Volumetric Clouds V1 - fullscreen composite.
// Cloud texture stores linear, premultiplied in-scattered radiance in rgb and
// transmittance in a. No alpha blending is used: the final swap-chain pixel is
// written opaque after sky * T + cloudRGB, exposure, tone mapping and sRGB.

cbuffer CloudParams : register(b0)
{
    float4 CameraPositionTime;
    float4 CameraRightFov;
    float4 CameraUpAspect;
    float4 CameraForwardLayerBottom;
    float4 LayerShape;
    float4 SunDirectionIntensity;
    float4 SunColorAmbientIntensity;
    float4 AmbientColorAnisotropy;
    float4 WindNoiseScale;
    float4 Shaping;
    float4 Output;
    float4 SkyZenith;
    float4 SkyHorizon;
};

Texture2D<float4> CloudTexture : register(t0);
SamplerState LinearClamp : register(s0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput FullscreenVS(uint vertexID : SV_VertexID)
{
    const float2 v = float2((vertexID << 1) & 2, vertexID & 2);
    VertexOutput output;
    output.position = float4(v * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = v;
    return output;
}

float3 LinearToSrgb(float3 value)
{
    value = saturate(value);
    const float3 low = value * 12.92;
    const float3 high = 1.055 * pow(value, 1.0 / 2.4) - 0.055;
    return lerp(low, high, step(0.0031308, value));
}

float3 SkyColor(float2 uv, float3 rayDirection)
{
    const float horizon = pow(saturate(1.0 - saturate(rayDirection.y)), 3.0);
    float3 sky = lerp(SkyZenith.xyz, SkyHorizon.xyz, horizon);

    // There is no terrain in the standalone tool. Keep the lower hemisphere a
    // cold atmospheric haze instead of a flat clear color; this is explicitly a
    // V1 limitation, not a fake landscape.
    if (rayDirection.y < 0.0)
    {
        const float down = saturate(-rayDirection.y);
        const float3 groundHaze = SkyHorizon.xyz * 0.10 + SkyZenith.xyz * 0.05;
        sky = lerp(SkyHorizon.xyz, groundHaze, pow(down, 0.45));
    }

    const float3 sunDirection = normalize(SunDirectionIntensity.xyz);
    const float sunDot = saturate(dot(rayDirection, sunDirection));
    // Broad atmospheric glow plus a compact sun highlight. Overcast has a low
    // sun intensity, so this stays subordinate to the cloud layer.
    sky += SunColorAmbientIntensity.xyz * SunDirectionIntensity.w *
           pow(sunDot, 8.0) * 0.035;
    sky += SunColorAmbientIntensity.xyz * SunDirectionIntensity.w *
           pow(sunDot, 256.0) * 0.20;
    return sky;
}

float4 CloudCompositePS(VertexOutput input) : SV_Target0
{
    const float2 uv = input.uv;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float tanHalfFov = tan(CameraRightFov.w * 0.5);
    const float3 rayDirection = normalize(
        CameraForwardLayerBottom.xyz +
        CameraRightFov.xyz * (ndc.x * tanHalfFov * CameraUpAspect.w) +
        CameraUpAspect.xyz * (ndc.y * tanHalfFov));

    const float4 cloud = CloudTexture.SampleLevel(LinearClamp, uv, 0);
    const uint debugMode = (uint)Shaping.w;

    float3 color = 0.0;
    if (debugMode >= 1 && debugMode <= 4)
    {
        color = cloud.rgb;
    }
    else
    {
        const float3 sky = SkyColor(uv, rayDirection);
        color = sky * saturate(cloud.a) + max(cloud.rgb, 0.0);
        color *= max(Output.z, 0.0);
        // Exponential tone map keeps bright forward scattering under control
        // without a hard clip.
        color = 1.0 - exp(-color);
    }

    // A tiny ordered dither prevents 8-bit banding in dark overcast skies.
    const float dither =
        frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453) / 255.0;
    color += dither;

    return float4(LinearToSrgb(color), 1.0);
}
