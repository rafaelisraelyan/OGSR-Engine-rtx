// OGSR Volumetric Clouds V1 - compute ray marcher.
//
// Output convention:
//   rgb = premultiplied in-scattered radiance (linear)
//   a   = transmittance (1 = clear sky, 0 = fully opaque)
// Debug modes write a displayable value in rgb and keep a = 1.

static const float kPi = 3.14159265358979323846;

cbuffer CloudParams : register(b0)
{
    float4 CameraPositionTime;          // xyz position, w time
    float4 CameraRightFov;              // xyz right, w vertical fov (radians)
    float4 CameraUpAspect;              // xyz up, w aspect
    float4 CameraForwardLayerBottom;    // xyz forward, w layer bottom
    float4 LayerShape;                  // x top, y coverage, z density, w extinction
    float4 SunDirectionIntensity;       // xyz direction to sun, w intensity
    float4 SunColorAmbientIntensity;    // xyz sun color, w ambient intensity
    float4 AmbientColorAnisotropy;      // xyz ambient color, w HG anisotropy
    float4 WindNoiseScale;              // xyz world-space wind offset to subtract, w inverse scale
    float4 Shaping;                     // x vertical scale, y max distance, z steps, w debug
    float4 Output;                      // xy output size, z exposure, w far plane
    float4 SkyZenith;                   // xyz zenith color
    float4 SkyHorizon;                  // xyz horizon color
};

Texture3D<float4> NoiseTexture : register(t0);
SamplerState NoiseSampler : register(s0);
RWTexture2D<float4> CloudOutput : register(u0);

float3 CameraPosition()
{
    return CameraPositionTime.xyz;
}

float3 CameraRight()
{
    return CameraRightFov.xyz;
}

float3 CameraUp()
{
    return CameraUpAspect.xyz;
}

float3 CameraForward()
{
    return CameraForwardLayerBottom.xyz;
}

float LayerBottom()
{
    return CameraForwardLayerBottom.w;
}

float LayerTop()
{
    return LayerShape.x;
}

float LayerThickness()
{
    return max(LayerTop() - LayerBottom(), 1.0);
}

bool IntersectLayer(float3 origin, float3 direction, out float t0, out float t1)
{
    t0 = 0.0;
    t1 = -1.0;

    const float bottom = LayerBottom();
    const float top = LayerTop();
    const float maxDistance = Shaping.y;

    if (abs(direction.y) < 1e-5)
    {
        // Parallel to the layer boundary: only a ray already inside the slab
        // can hit cloud volume, and it keeps going to the distance cap.
        if (origin.y >= bottom && origin.y <= top)
            t1 = maxDistance;
    }
    else
    {
        const float a = (bottom - origin.y) / direction.y;
        const float b = (top - origin.y) / direction.y;
        t0 = max(min(a, b), 0.0);
        t1 = min(max(a, b), maxDistance);
    }

    return t1 > t0;
}

float HeightFraction(float y)
{
    return saturate((y - LayerBottom()) / LayerThickness());
}

// Density is deliberately not a direct copy of one noise channel:
//   - a lower-frequency sample creates large cloud masses;
//   - coverage is a contrast threshold, not a lerp;
//   - the full-frequency Worley channel erodes the silhouette;
//   - vertical profile shapes formation and dissipation;
//   - density is a separate multiplier;
//   - wind and scale are applied before sampling.
float SampleCloudDensity(float3 position)
{
    const float coverage = saturate(LayerShape.y);
    const float densityMultiplier = max(LayerShape.z, 0.0);
    float result = 0.0;

    if (coverage > 0.0 && densityMultiplier > 0.0)
    {
        const float height = HeightFraction(position.y);
        const float baseProfile = smoothstep(0.0, 0.11, height);
        const float topProfile = 1.0 - smoothstep(0.70, 1.0, height);
        const float profile = baseProfile * topProfile;

        if (profile > 0.0)
        {
            const float scale = WindNoiseScale.w;
            const float3 windOffset = WindNoiseScale.xyz;
            const float3 coordinate = float3(
                (position.x - windOffset.x) * scale,
                (position.z - windOffset.z) * scale,
                height * Shaping.x);

            // Macro sample: larger masses. Detail sample: erodes the edges
            // without regenerating the V0 3D texture.
            const float4 macro = NoiseTexture.SampleLevel(
                NoiseSampler, coordinate * 0.45 + float3(0.17, 0.31, 0.07), 0);
            const float4 detail =
                NoiseTexture.SampleLevel(NoiseSampler, coordinate, 0);
            const float4 fine = NoiseTexture.SampleLevel(
                NoiseSampler, coordinate * 3.3 + float3(0.53, 0.11, 0.37), 0);

            const float threshold = 1.0 - coverage;
            float shaped =
                saturate((macro.b - threshold) / max(coverage, 0.001));
            shaped = shaped * shaped * (3.0 - 2.0 * shaped);
            const float erosion = lerp(0.10, 1.0,
                                       smoothstep(0.20, 0.80, detail.g));
            const float textureDetail = lerp(
                0.10, 1.0, smoothstep(0.25, 0.75, detail.b));
            const float fineErosion = lerp(
                0.10, 1.0, smoothstep(0.30, 0.70, fine.b));
            result = saturate(shaped * shaped * erosion * textureDetail *
                              fineErosion * profile * densityMultiplier);
        }
    }

    return result;
}

// Cheaper density used for the four sun samples. It preserves the macro shape
// and vertical profile but skips the second texture read.
float SampleCloudDensityForLight(float3 position)
{
    const float coverage = saturate(LayerShape.y);
    const float densityMultiplier = max(LayerShape.z, 0.0);
    float result = 0.0;

    if (coverage > 0.0 && densityMultiplier > 0.0)
    {
        const float height = HeightFraction(position.y);
        const float profile = smoothstep(0.0, 0.11, height) *
                              (1.0 - smoothstep(0.70, 1.0, height));
        if (profile > 0.0)
        {
            const float scale = WindNoiseScale.w;
            const float3 windOffset = WindNoiseScale.xyz;
            const float3 coordinate = float3(
                (position.x - windOffset.x) * scale,
                (position.z - windOffset.z) * scale,
                height * Shaping.x);
            const float4 macro = NoiseTexture.SampleLevel(
                NoiseSampler, coordinate * 0.45 + float3(0.17, 0.31, 0.07), 0);
            const float threshold = 1.0 - coverage;
            float shaped =
                saturate((macro.b - threshold) / max(coverage, 0.001));
            shaped = shaped * shaped * (3.0 - 2.0 * shaped);
            result = saturate(shaped * shaped * profile * densityMultiplier);
        }
    }

    return result;
}

float HenyeyGreenstein(float cosTheta, float anisotropy)
{
    const float g = clamp(anisotropy, -0.85, 0.85);
    const float denominator = 1.0 + g * g - 2.0 * g * cosTheta;
    return (1.0 - g * g) /
           max(4.0 * kPi * pow(max(denominator, 1e-4), 1.5), 1e-4);
}

float OpticalDepthToSun(float3 position)
{
    const float stepLength = LayerThickness() / 6.0;
    float depth = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const float3 samplePosition =
            position + SunDirectionIntensity.xyz * stepLength * (float(i) + 0.5);
        depth += SampleCloudDensityForLight(samplePosition) * stepLength;
    }
    return depth;
}

[numthreads(8, 8, 1)]
void CloudRayMarch(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadID.xy;
    const uint width = max((uint)Output.x, 1u);
    const uint height = max((uint)Output.y, 1u);
    if (pixel.x >= width || pixel.y >= height)
        return;

    const float2 uv = (float2(pixel) + 0.5) / float2(width, height);
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float tanHalfFov = tan(CameraRightFov.w * 0.5);
    const float3 rayDirection = normalize(
        CameraForward() +
        CameraRight() * (ndc.x * tanHalfFov * CameraUpAspect.w) +
        CameraUp() * (ndc.y * tanHalfFov));

    const uint debugMode = (uint)Shaping.w;

    float t0 = 0.0;
    float t1 = 0.0;
    if (!IntersectLayer(CameraPosition(), rayDirection, t0, t1))
    {
        if (debugMode == 1)
            CloudOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        else if (debugMode == 2)
            CloudOutput[pixel] = float4(1.0, 1.0, 1.0, 1.0);
        else if (debugMode == 3)
            CloudOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        else if (debugMode == 4)
            CloudOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        else
            CloudOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const int stepCount = clamp((int)Shaping.z, 8, 128);
    const float stepLength = (t1 - t0) / float(stepCount);
    const float3 sunDirection = normalize(SunDirectionIntensity.xyz);
    const float anisotropy = AmbientColorAnisotropy.w;
    const float extinction = max(LayerShape.w, 0.0);

    float transmittance = 1.0;
    float3 scattered = 0.0;
    float densitySum = 0.0;
    float occupiedSteps = 0.0;
    float executedSteps = 0.0;

    [loop]
    for (int step = 0; step < stepCount; ++step)
    {
        executedSteps += 1.0;
        const float t = t0 + (float(step) + 0.5) * stepLength;
        const float3 position = CameraPosition() + rayDirection * t;
        const float density = SampleCloudDensity(position);
        densitySum += density;

        if (density > 1e-4)
        {
            occupiedSteps += 1.0;
            const float sigma = extinction * density;
            const float opticalDepth = OpticalDepthToSun(position);
            const float sunTransmittance = exp(-extinction * opticalDepth);
            const float phase = HenyeyGreenstein(
                clamp(dot(rayDirection, sunDirection), -1.0, 1.0),
                anisotropy);
            const float height = HeightFraction(position.y);
            const float3 ambient =
                AmbientColorAnisotropy.xyz * SunColorAmbientIntensity.w *
                (0.35 + 0.65 * height);
            const float3 sun =
                SunColorAmbientIntensity.xyz * SunDirectionIntensity.w *
                sunTransmittance * phase;
            const float stepTransmittance = exp(-sigma * stepLength);
            scattered += transmittance * (1.0 - stepTransmittance) *
                         (sun + ambient);
            transmittance *= stepTransmittance;

            if (transmittance < 0.005)
                break;
        }
    }

    // Guard against a driver producing an unexpected non-finite value.
    if (scattered.x != scattered.x || scattered.y != scattered.y ||
        scattered.z != scattered.z || scattered.x > 1e6 ||
        scattered.y > 1e6 || scattered.z > 1e6)
    {
        scattered = 0.0;
        transmittance = 1.0;
    }

    if (debugMode == 1)
    {
        const float averageDensity = densitySum / float(stepCount);
        CloudOutput[pixel] = float4(saturate(averageDensity * 6.0).xxx, 1.0);
    }
    else if (debugMode == 2)
    {
        CloudOutput[pixel] = float4(transmittance.xxx, 1.0);
    }
    else if (debugMode == 3)
    {
        const float executed = executedSteps / float(stepCount);
        CloudOutput[pixel] = float4(saturate(executed).xxx, 1.0);
    }
    else if (debugMode == 4)
    {
        const float occupied = occupiedSteps / float(stepCount);
        CloudOutput[pixel] = float4(saturate(occupied).xxx, 1.0);
    }
    else
    {
        CloudOutput[pixel] = float4(scattered, saturate(transmittance));
    }
}
