// OGSR Volumetric Clouds V2 - world-space weather map generation.
//
// This pass is intentionally low frequency and cheap: it runs once at startup
// (and on an explicit regeneration), never every frame. The result is a
// periodic RGBA8 map with four independent fields:
//   R = large-scale coverage
//   G = cloud type (cumulus -> stratocumulus -> overcast)
//   B = local density / thickness
//   A = storm / front potential
//
// All 2D noise is periodic on the integer lattice. Domain warping keeps the
// same period because the warp field itself is periodic.

cbuffer WeatherParams : register(b0)
{
    uint Resolution;
    uint Seed;
    float Time;
    float Padding;
};

RWTexture2D<float4> WeatherOutput : register(u0);
RWStructuredBuffer<float4> ProbeOutput : register(u1);
Texture2D<float4> WeatherTexture : register(t0);
SamplerState LinearWrapSampler : register(s0);

uint HashMix(uint h)
{
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    h ^= h >> 16u;
    return h;
}

uint WrapCell(int value, int period)
{
    return uint(value + period) % uint(period);
}

float HashFloat(uint2 cell, uint seed)
{
    uint h = seed + 0x9e3779b9u;
    h = HashMix(h ^ cell.x);
    h = HashMix(h ^ cell.y);
    return float(h & 0xffffu) * (1.0 / 65535.0);
}

float Fade(float t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Periodic 2D value noise. Frequencies are integer divisors of Resolution so
// the map tiles exactly along both axes.
float ValueNoise(float2 uv, int frequency, uint seed)
{
    const float2 q = uv * float(frequency);
    const int2 base = int2(floor(q));
    const float2 f = frac(q);
    const float2 w = float2(Fade(f.x), Fade(f.y));

    const int period = max(frequency, 1);
    const uint2 c00 = uint2(WrapCell(base.x, period),
                            WrapCell(base.y, period));
    const uint2 c10 = uint2(WrapCell(base.x + 1, period),
                            WrapCell(base.y, period));
    const uint2 c01 = uint2(WrapCell(base.x, period),
                            WrapCell(base.y + 1, period));
    const uint2 c11 = uint2(WrapCell(base.x + 1, period),
                            WrapCell(base.y + 1, period));
    const float v00 = HashFloat(c00, seed);
    const float v10 = HashFloat(c10, seed);
    const float v01 = HashFloat(c01, seed);
    const float v11 = HashFloat(c11, seed);

    const float x0 = lerp(v00, v10, w.x);
    const float x1 = lerp(v01, v11, w.x);
    return lerp(x0, x1, w.y);
}

float Fbm(float2 uv, int baseFrequency, int octaves, uint seed)
{
    float sum = 0.0;
    float amplitude = 1.0;
    float normalization = 0.0;
    int frequency = max(baseFrequency, 1);

    [loop]
    for (int octave = 0; octave < octaves; ++octave)
    {
        sum += amplitude * ValueNoise(uv, frequency,
                                      seed + uint(octave) * 0x9e3779b9u);
        normalization += amplitude;
        frequency *= 2;
        amplitude *= 0.5;
    }
    return sum / max(normalization, 1e-6);
}

[numthreads(8, 8, 1)]
void GenerateWeather(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Resolution || id.y >= Resolution)
        return;

    const float2 uv = (float2(id.xy) + 0.5) / float(Resolution);

    // Large-scale domain warp. The warp is periodic, so the final map is
    // periodic even though it visibly distorts the coverage boundaries.
    const float warpX = Fbm(uv + float2(0.11, 0.37), 4, 4,
                            Seed + 0x51u) - 0.5;
    const float warpY = Fbm(uv + float2(0.73, 0.19), 4, 4,
                            Seed + 0x91u) - 0.5;
    const float2 p = uv + float2(warpX, warpY) * 0.14;

    const float coverageNoise = Fbm(p, 3, 5, Seed + 0x107u);
    const float typeNoise = Fbm(p + float2(0.31, 0.67), 3, 5,
                                Seed + 0x211u);
    const float densityNoise = Fbm(p + float2(0.59, 0.23), 6, 5,
                                   Seed + 0x331u);
    const float stormNoise = Fbm(p + float2(0.17, 0.83), 4, 5,
                                 Seed + 0x433u);

    // Broad coverage zones with large clear gaps. The narrower smoothstep is
    // intentional: V1 suffered from a nearly uniform noise field.
    const float coverage = smoothstep(0.38, 0.62, coverageNoise);
    const float type = smoothstep(0.18, 0.88, typeNoise);
    const float density = smoothstep(0.28, 0.76, densityNoise);

    // A large diagonal storm band gives the storm/rain presets a visible front
    // anchor instead of making every frame uniformly dark. The frequency vector
    // is integer-valued so the A channel remains exactly periodic at uv=1.
    const float2 frontFrequency = float2(3.0, 2.0);
    const float diagonal = 0.5 + 0.5 *
        sin(dot(uv, frontFrequency) * 6.2831853 + stormNoise * 2.4);
    const float storm = saturate(0.62 * smoothstep(0.48, 0.86, stormNoise) +
                                 0.38 * smoothstep(0.35, 0.95, diagonal));

    WeatherOutput[id.xy] = float4(coverage, type, density, storm);
}

// GPU sampler seam probe. Samples the same map both inside the tile and after
// an exact +1 tile shift; the WRAP sampler must return the same result.
// Axis 0 probes X, axis 1 probes Y. Each thread writes the RGBA difference.
[numthreads(32, 1, 1)]
void ProbeWeatherSampler(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 16u)
        return;

    const uint axis = id.x & 1u;
    const uint sampleIndex = id.x >> 1u;
    const float t = (float(sampleIndex) + 0.5) / 8.0;
    const float a = (id.x * 13u) & 31u;
    const float2 uv = axis == 0u
        ? float2(0.05 + 0.9 * t, a / 32.0)
        : float2(a / 32.0, 0.05 + 0.9 * t);
    const float2 shifted = uv + (axis == 0u ? float2(1.0, 0.0)
                                             : float2(0.0, 1.0));
    const float4 first = WeatherTexture.SampleLevel(LinearWrapSampler, uv, 0);
    const float4 second =
        WeatherTexture.SampleLevel(LinearWrapSampler, shifted, 0);
    ProbeOutput[id.x] = abs(first - second);
}
