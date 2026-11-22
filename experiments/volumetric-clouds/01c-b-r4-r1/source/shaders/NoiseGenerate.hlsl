// OGSR Volumetric Clouds V0 - periodic 3D noise generation.
//
// Output channels (all in [0, 1]):
//   .r = Perlin fBm (normalized octave sum, mapped to [0,1])
//   .g = inverted Worley F1 (1 at feature points, 0 at cell boundaries)
//   .b = Perlin-Worley base with cellular erosion
//   .a = 1
//
// Every lattice period is an integer number of cells across the volume, and
// every hash wraps modulo that period, so the volume tiles exactly in X/Y/Z.

RWTexture3D<float4> NoiseOutput : register(u0);
RWStructuredBuffer<float4> ProbeOutput : register(u1);
Texture3D<float4> NoiseTexture : register(t0);
SamplerState LinearWrapSampler : register(s0);

cbuffer NoiseParams : register(b0)
{
    uint Resolution;
    uint Seed;
    uint Octaves;
    uint BaseFrequency;

    uint Lacunarity;
    uint WorleyFrequency;
    float Gain;
    float Blend;

    float Erosion;
    uint Reserved0;
    uint Reserved1;
    uint Reserved2;

    uint Reserved3;
    uint Reserved4;
    uint Reserved5;
    uint Reserved6;
};

static const float3 kGradients[12] =
{
    float3( 1,  1,  0), float3(-1,  1,  0),
    float3( 1, -1,  0), float3(-1, -1,  0),
    float3( 1,  0,  1), float3(-1,  0,  1),
    float3( 1,  0, -1), float3(-1,  0, -1),
    float3( 0,  1,  1), float3( 0, -1,  1),
    float3( 0,  1, -1), float3( 0, -1, -1)
};

uint HashMix(uint h)
{
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    h ^= h >> 16u;
    return h;
}

uint HashCell(uint3 cell)
{
    uint h = Seed + 0x9e3779b9u;
    h = HashMix(h ^ cell.x);
    h = HashMix(h ^ cell.y);
    h = HashMix(h ^ cell.z);
    return h;
}

// Returns a point in the unit cell. Ten bits per axis are plenty for a 128^3
// volume and keep the 27-cell Worley search affordable.
float3 HashOffset(uint3 cell)
{
    const uint h = HashCell(cell);
    const float inv = 1.0 / 1023.0;
    return float3(
        float(h & 0x3ffu),
        float((h >> 10u) & 0x3ffu),
        float((h >> 20u) & 0x3ffu)) * inv;
}

uint3 WrapCell(int3 cell, int period)
{
    // cell >= -1 and period >= 1, so adding the period makes the value
    // non-negative and lets the modulo run on unsigned integers.
    const uint3 biased = uint3(cell + period);
    return biased % uint(period);
}

float Fade(float t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Classic gradient noise on a periodic integer lattice. The 12 edge-midpoint
// gradients make the result deterministic and unbiased (no normalize()).
// Scaled by 1.4 so the useful range is close to [-1, 1].
float GradientNoise(float3 position, int period)
{
    const int3 base = int3(floor(position));
    const float3 f = frac(position);
    const float3 w = float3(Fade(f.x), Fade(f.y), Fade(f.z));

    float values[8];
    [unroll]
    for (int z = 0; z < 2; ++z)
    {
        [unroll]
        for (int y = 0; y < 2; ++y)
        {
            [unroll]
            for (int x = 0; x < 2; ++x)
            {
                const int3 corner = int3(x, y, z);
                const uint3 wrapped = WrapCell(base + corner, period);
                const uint hash = HashCell(wrapped);
                const float3 gradient = kGradients[hash % 12u];
                values[x + 2 * y + 4 * z] =
                    dot(gradient, f - float3(corner));
            }
        }
    }

    const float x00 = lerp(values[0], values[1], w.x);
    const float x10 = lerp(values[2], values[3], w.x);
    const float x01 = lerp(values[4], values[5], w.x);
    const float x11 = lerp(values[6], values[7], w.x);
    const float y0 = lerp(x00, x10, w.y);
    const float y1 = lerp(x01, x11, w.y);
    return lerp(y0, y1, w.z) * 1.4;
}

// Periodic fractional Brownian motion. Frequencies stay integers because
// BaseFrequency and Lacunarity are integers; the amplitude sum normalizes the
// result to approximately [-1, 1] regardless of octave count.
float PerlinFbm(float3 uv)
{
    float frequency = float(BaseFrequency);
    float amplitude = 1.0;
    float sum = 0.0;
    float normalization = 0.0;

    [loop]
    for (uint octave = 0; octave < Octaves; ++octave)
    {
        sum += amplitude * GradientNoise(uv * frequency, int(frequency));
        normalization += amplitude;
        frequency *= float(Lacunarity);
        amplitude *= Gain;
    }

    return sum / max(normalization, 1e-6);
}

// Distance to the nearest feature point in a periodic 3D cell lattice.
// Hash coordinates are wrapped, and feature points are evaluated relative to
// the current cell, which is exactly translation invariant across the seam.
float WorleyF1(float3 uv)
{
    const float frequency = float(WorleyFrequency);
    const float3 q = uv * frequency;
    const int3 base = int3(floor(q));
    const float3 local = frac(q);
    float minDistanceSquared = 8.0;

    [loop]
    for (int z = -1; z <= 1; ++z)
    {
        [loop]
        for (int y = -1; y <= 1; ++y)
        {
            [loop]
            for (int x = -1; x <= 1; ++x)
            {
                const int3 offset = int3(x, y, z);
                const uint3 wrapped = WrapCell(base + offset, int(WorleyFrequency));
                const float3 feature = float3(offset) + HashOffset(wrapped);
                const float3 delta = feature - local;
                minDistanceSquared = min(minDistanceSquared, dot(delta, delta));
            }
        }
    }

    // 1.5 is the practical upper bound of F1 for jittered cells; the result is
    // inverted so cavities are dark and feature seeds are bright.
    return saturate(1.0 - sqrt(minDistanceSquared) * (1.0 / 1.5));
}

// EvaluateNoise is the single source of truth for the three output channels.
// Both the volume generator and the periodicity probe call it.
float4 EvaluateNoise(float3 uv)
{
    const float perlin = saturate(0.5 + 0.5 * PerlinFbm(uv));
    const float worley = WorleyF1(uv);

    // Perlin-Worley base with cellular erosion. Blend = 0 gives pure Perlin,
    // Blend = 1 gives pure Worley; Erosion removes density at cell boundaries.
    const float base = lerp(perlin, worley, Blend);
    const float combined = saturate(base * (1.0 - Erosion * (1.0 - worley)));
    return float4(perlin, worley, combined, 1.0);
}

[numthreads(8, 8, 4)]
void GenerateNoise(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= uint3(Resolution, Resolution, Resolution)))
        return;

    const float3 uv = (float3(id) + 0.5) / float(Resolution);
    NoiseOutput[id] = EvaluateNoise(uv);
}

// Direct mathematical periodicity probe: evaluate the exact same noise
// functions at p and p + unit axis, then write the per-axis maximum channel
// difference. A periodic field gives values near zero even across the seam.
[numthreads(64, 1, 1)]
void ProbePeriodicity(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 64)
        return;

    // Low-discrepancy sample positions inside [0,1)^3.
    const float3 uv = frac(float3(
        id.x * 0.6180339887,
        id.x * 0.7548776662,
        id.x * 0.5698402910));

    const float4 value = EvaluateNoise(uv);
    const float4 dx = abs(value - EvaluateNoise(uv + float3(1.0, 0.0, 0.0)));
    const float4 dy = abs(value - EvaluateNoise(uv + float3(0.0, 1.0, 0.0)));
    const float4 dz = abs(value - EvaluateNoise(uv + float3(0.0, 0.0, 1.0)));

    // Negative control: a non-periodic offset must produce a visible
    // difference. This proves the probe is not trivially returning zero.
    const float4 control = abs(value - EvaluateNoise(uv + float3(0.37, 0.0, 0.0)));

    const float maxX = max(dx.x, max(dx.y, dx.z));
    const float maxY = max(dy.x, max(dy.y, dy.z));
    const float maxZ = max(dz.x, max(dz.y, dz.z));
    const float maxControl = max(control.x, max(control.y, control.z));
    ProbeOutput[id.x] = float4(maxX, maxY, maxZ, maxControl);
}

// Texture-sampling probe. Samples the generated volume with the same linear
// WRAP sampler used by the viewer, at coordinates just across the X/Y/Z seams,
// and at the same coordinates shifted by +1 (one full volume period).
[numthreads(64, 1, 1)]
void ProbeSamplerWrap(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 64)
        return;

    const uint axis = id.x % 3u;
    const uint sampleIndex = id.x / 3u;
    const float t = float(sampleIndex) / 21.0;
    const float seamCoordinate = (0.05 + 0.4 * t) / float(Resolution);

    // Deterministic coordinates; the CPU reference uses the same integers.
    float3 uv = float3(
        float((id.x * 13u) & 63u) / 64.0,
        float((id.x * 29u) & 63u) / 64.0,
        float((id.x * 47u) & 63u) / 64.0);

    float3 uvWrapped = uv;
    if (axis == 0u)
    {
        uv.x = seamCoordinate;
        uvWrapped.x = seamCoordinate + 1.0;
    }
    else if (axis == 1u)
    {
        uv.y = seamCoordinate;
        uvWrapped.y = seamCoordinate + 1.0;
    }
    else
    {
        uv.z = seamCoordinate;
        uvWrapped.z = seamCoordinate + 1.0;
    }

    ProbeOutput[id.x * 2u] = NoiseTexture.SampleLevel(LinearWrapSampler, uv, 0);
    ProbeOutput[id.x * 2u + 1u] = NoiseTexture.SampleLevel(LinearWrapSampler, uvWrapped, 0);
}

