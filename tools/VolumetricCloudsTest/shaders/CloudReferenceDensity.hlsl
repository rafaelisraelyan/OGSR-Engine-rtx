// V2.3h H1/H2 reference density model.
//
// Mathematical reference: gd-clouds by fractalino (MIT), especially
// cloud_density.gdshaderinc. No source code is copied; the OGSR channel
// layout (R=Perlin, G=Worley, B=Perlin-Worley) and slab bounds are used.
//
// This is an intentionally isolated experiment. It does not call the legacy
// CloudTypeWeights, LocalHeightFraction, CloudHeightProfile, FrontMask or
// weather sampling. The base shape is the existing 3D Perlin-Worley channel
// (.b) sampled in world XYZ; H2 multiplies it by an independent world-Y height
// profile. H3 adds one monotonic coverage remap; H4 adds bounded Worley
// erosion. The shader permutation and stage are explicit, and the probe uses
// this same function.

#ifndef OGSR_REFERENCE_DENSITY_INCLUDED
#define OGSR_REFERENCE_DENSITY_INCLUDED

// Explicit per-axis scales. X/Z use the preset noise scale; Y is deliberately
// slower so the mass has vertical organisation rather than extruded XZ noise.
static const float kReferenceVerticalNoiseScale = 1.80f;
static const float kReferenceBaseScale = 0.55f;
static const float kReferenceBaseFade = 0.12f;
static const float kReferenceTopFade = 0.18f;
static const float kReferenceCoverageEpsilon = 1e-3f;
// Bounded soft 3D coordinate warp used only by the A01 candidate. The warp
// sample is a low-frequency field at a different phase, so it breaks the
// straight Worley/Perlin alignment without adding a new threshold.
static const float kReferenceWarpScale = 0.37f;
static const float kReferenceWarpAmount = 0.10f;
static const float3 kReferenceWarpOffset = float3(0.13f, 0.71f, 0.37f);

float ReferenceShapeLow()
{
    return ReferenceShape.z;
}

float ReferenceShapeHigh()
{
    return max(ReferenceShape.w, ReferenceShape.z + 0.01f);
}

float ReferenceShapeFieldValue(float4 noise)
{
    const uint mix = (uint)ReferenceShape.x;
    if (mix == 0u)
        return noise.r;
    if (mix == 1u)
        return noise.b;
    return noise.r * 0.70f + noise.b * 0.30f;
}

float ReferenceHashCell(int3 cell)
{
    // Non-periodic 32-bit lattice hash. Negative cell indices are allowed;
    // the cast to uint is intentional and deterministic.
    uint h = (uint)cell.x * 374761393u
           + (uint)cell.y * 668265263u
           + (uint)cell.z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)h * (1.0 / 4294967295.0);
}

float ReferenceValueNoise(float3 p)
{
    // Fixed shear breaks the axis-aligned lattice appearance. Quintic
    // interpolation makes the field C2-continuous; no per-frame phase.
    const float3 q = float3(
        p.x + p.z * 0.37f,
        p.y + p.x * 0.21f,
        p.z + p.y * 0.53f);
    const int3 cell = (int3)floor(q);
    const float3 f = frac(q);
    const float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);

    const float c000 = ReferenceHashCell(cell + int3(0, 0, 0));
    const float c100 = ReferenceHashCell(cell + int3(1, 0, 0));
    const float c010 = ReferenceHashCell(cell + int3(0, 1, 0));
    const float c110 = ReferenceHashCell(cell + int3(1, 1, 0));
    const float c001 = ReferenceHashCell(cell + int3(0, 0, 1));
    const float c101 = ReferenceHashCell(cell + int3(1, 0, 1));
    const float c011 = ReferenceHashCell(cell + int3(0, 1, 1));
    const float c111 = ReferenceHashCell(cell + int3(1, 1, 1));

    const float x00 = lerp(c000, c100, u.x);
    const float x10 = lerp(c010, c110, u.x);
    const float x01 = lerp(c001, c101, u.x);
    const float x11 = lerp(c011, c111, u.x);
    const float y0 = lerp(x00, x10, u.y);
    const float y1 = lerp(x01, x11, u.y);
    return lerp(y0, y1, u.z);
}

uint ReferenceTextureFetchCount()
{
    uint count = 1u; // base shape
    if (ReferenceShape.y > 0.5f)
        ++count;     // A01 coordinate warp
    if ((uint)ReferenceField.x == 1u)
        ++count;     // A02 Candidate A second R sample
    if ((uint)LightingFlags.w >= 4u)
        ++count;     // H4 detail erosion
    return count;
}

// V2.3h-A03: non-tiling world-space macro placement. The hash lattice has no
// modulo, so the large-mass layout has no 4.545 km period. Two octaves keep
// the cost bounded; the periodic NoiseVolume remains the detail field only.
float ReferenceMacroNoise(float3 position, float scale, float3 phase)
{
    const float s = max(scale, 1.0f);
    const float3 p = float3(
        (position.x - WindNoiseScale.x) / s,
        position.y / max(s * 2.0f, 1.0f),
        (position.z - WindNoiseScale.z) / s) + phase;
    return ReferenceValueNoise(p);
}

#ifdef OGSR_A03A
bool ReferenceMacroCoverageEnabled()
{
    return ((uint)ReferenceMacro3.y & 1u) != 0u;
}

bool ReferenceMacroSeedHashEnabled()
{
    return ((uint)ReferenceMacro3.y & 2u) != 0u;
}

bool ReferenceMacroDetailCompensateEnabled()
{
    return ((uint)ReferenceMacro3.y & 4u) != 0u;
}

bool ReferenceMacroCalibrateEnabled()
{
    return ((uint)ReferenceMacro3.y & 16u) != 0u;
}

bool ReferenceMacroWeatherQuantileEnabled()
{
    return ((uint)ReferenceMacro3.y & 32u) != 0u;
}

bool ReferenceMacroWeatherLocalEnabled()
{
    return ((uint)ReferenceMacro3.y & 64u) != 0u;
}

bool ReferenceLocalOccupancyEnabled()
{
    return ((uint)ReferenceMacro3.y & 128u) != 0u;
}

bool ReferenceStormLocalEnabled()
{
    return ((uint)ReferenceMacro3.y & 256u) != 0u;
}

bool ReferenceStormMacroGapEnabled()
{
    return ((uint)ReferenceMacro3.y & 8u) != 0u;
}

float3 ReferenceMacroPhaseA()
{
    const float seed = ReferenceMacro2.w;
    float3 phase = float3(seed * 0.113f, seed * 0.071f, seed * 0.197f);
    if (ReferenceMacroSeedHashEnabled())
        phase += ReferenceMacro4.xyz;
    return phase;
}

float3 ReferenceMacroPhaseB()
{
    const float seed = ReferenceMacro2.w;
    float3 phase = float3(seed * 0.163f, seed * 0.091f, seed * 0.137f);
    if (ReferenceMacroSeedHashEnabled())
        phase += ReferenceMacro4.xyz * 1.618034f;
    return phase;
}

// The single monotonic coverage remap used by stage 3. A03a applies it to the
// macro mass before the periodic detail multiplier, so detail can no longer
// decide whether a column exists.
float ReferenceCoverageRemap(float shape)
{
    const float coverage = saturate(LayerShape.y);
    if (coverage >= 0.999f)
        return saturate(shape);
    return saturate((shape - (1.0f - coverage)) /
                    max(coverage, kReferenceCoverageEpsilon));
}
#endif

#ifdef OGSR_A03B
// A03b local occupancy: a second non-periodic world-space field with shorter
// correlation length than the macro placement. Two octaves and a different
// phase break the lattice and the 6 km macro scale. Wind is subtracted by
// ReferenceMacroNoise, so the translation contract is inherited.
float3 ReferenceLocalPhaseA()
{
    return ReferenceMacro4.xyz * 1.91f + float3(11.3f, 7.9f, 3.1f);
}

float3 ReferenceLocalPhaseB()
{
    return ReferenceMacro4.xyz * 2.37f + float3(23.7f, 5.3f, 17.9f);
}

// Local occupancy is deliberately quasi-2D over the 4.3 km cloud layer:
// presence is an XZ decision, while the height envelope and detail provide
// the vertical structure. Without the Y damping a high field value outside
// the cloud band would count as an occupied column but produce no density.
float ReferenceLocalNoise(float3 position, float scale, float3 phase)
{
    // Unlike the macro octaves, the local field uses the same metric scale
    // on Y. This gives the masses a finite vertical extent instead of a
    // column that spans the whole layer.
    const float s = max(scale, 1.0f);
    const float3 p = float3(
        (position.x - WindNoiseScale.x) / s,
        position.y / s,
        (position.z - WindNoiseScale.z) / s) + phase;
    return ReferenceValueNoise(p);
}

// The occupancy field is fully 3D. Its column-max distribution is calibrated
// below, so a high value at one height still creates a 3D mass rather than a
// vertically extruded curtain.
static const float kReferenceLocalOccupancyY = 2000.0f;

float ReferenceLocalField(float3 position)
{
    const float fine =
        ReferenceLocalNoise(position, 2400.0f, ReferenceLocalPhaseA());
    const float medium =
        ReferenceLocalNoise(position, 1000.0f, ReferenceLocalPhaseB());
    return 0.6f * fine + 0.4f * medium;
}

float ReferenceHashCell2D(int2 cell)
{
    uint h = (uint)cell.x * 374761393u + (uint)cell.y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)h * (1.0 / 4294967295.0);
}

float ReferenceValueNoise2D(float2 p)
{
    const int2 cell = (int2)floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float c00 = ReferenceHashCell2D(cell + int2(0, 0));
    const float c10 = ReferenceHashCell2D(cell + int2(1, 0));
    const float c01 = ReferenceHashCell2D(cell + int2(0, 1));
    const float c11 = ReferenceHashCell2D(cell + int2(1, 1));
    return lerp(lerp(c00, c10, u.x), lerp(c01, c11, u.x), u.y);
}

// Per-column vertical geometry for A03b. Top/bottom vary in world XZ only,
// so every voxel of a column sees the same bounds and the cloud ceiling is
// not a flat slab. Amplitude is smaller than the Storm S2new envelope.
float ReferenceColumnTopNoise(float3 position)
{
    const float3 phase = ReferenceLocalPhaseA() + float3(5.1f, 0.0f, 9.7f);
    const float2 p = float2(
        (position.x - WindNoiseScale.x) / 5200.0f + phase.x,
        (position.z - WindNoiseScale.z) / 5200.0f + phase.z);
    return ReferenceValueNoise2D(p);
}

float ReferenceColumnBottomNoise(float3 position)
{
    const float3 phase = ReferenceLocalPhaseB() + float3(2.3f, 0.0f, 7.1f);
    const float2 p = float2(
        (position.x - WindNoiseScale.x) / 3000.0f + phase.x,
        (position.z - WindNoiseScale.z) / 3000.0f + phase.z);
    return ReferenceValueNoise2D(p);
}

#endif

float ReferenceMacroField(float3 position)
{
#ifndef OGSR_A03A
    const float seed = ReferenceMacro2.w;
    const float3 phaseA =
        float3(seed * 0.113f, seed * 0.071f, seed * 0.197f);
    const float3 phaseB =
        float3(seed * 0.163f, seed * 0.091f, seed * 0.137f);
#else
    const float3 phaseA = ReferenceMacroPhaseA();
    const float3 phaseB = ReferenceMacroPhaseB();
#endif
    const float n1 = ReferenceMacroNoise(position, ReferenceMacro.x, phaseA);
    const float n2 = ReferenceMacroNoise(position, ReferenceMacro.y, phaseB);
    const float w2 = saturate(ReferenceMacro2.y);
    return saturate(lerp(n1, n2, w2));
}

#ifdef OGSR_A03B
// macro is placement, local is occupancy. The macro bias is deliberately
// small: it organises cloud groups without re-creating a seed-dependent
// saturated slab. The threshold and width are global; no per-seed table.
// Macro placement as a local anomaly: the 12 km mean is removed before the
// 6 km octave is used. This keeps large-scale organisation while removing
// the seed-dependent low-frequency offset that saturated Clear at seed 4242.
float ReferenceMacroAnomaly(float3 position)
{
    const float3 phaseMean =
        ReferenceLocalPhaseB() + float3(5.3f, 9.1f, 2.7f);
    return ReferenceMacroNoise(position, 6000.0f, ReferenceMacroPhaseA()) -
           ReferenceMacroNoise(position, 12000.0f, phaseMean);
}

// True local mean of the primary 3.2 km octave. Four taps at +/-6 km remove
// the slowly varying bias of the local field itself; the residual has a
// ~3 km correlation length, unlike the ~10 km W2 residual that formed slabs.
float ReferenceLocalMean(float3 position)
{
    const float3 phase = ReferenceLocalPhaseA();
    const float3 pxp = position + float3(7200.0f, 0.0f, 0.0f);
    const float3 pxm = position - float3(7200.0f, 0.0f, 0.0f);
    const float3 pzp = position + float3(0.0f, 0.0f, 7200.0f);
    const float3 pzm = position - float3(0.0f, 0.0f, 7200.0f);
    return 0.25f * (
        ReferenceLocalNoise(pxp, 2400.0f, phase) +
        ReferenceLocalNoise(pxm, 2400.0f, phase) +
        ReferenceLocalNoise(pzp, 2400.0f, phase) +
        ReferenceLocalNoise(pzm, 2400.0f, phase));
}

float ReferenceLocalAnomaly(float3 position)
{
    const float3 phase = ReferenceLocalPhaseB();
    const float fine =
        0.65f * ReferenceLocalNoise(
            position, 2400.0f, ReferenceLocalPhaseA()) +
        0.35f * ReferenceLocalNoise(position, 1000.0f, phase);
    return fine - ReferenceLocalMean(position);
}

float ReferenceLocalOccupancyField(float3 position)
{
    // Presence is decided on a fixed horizontal plane. Column occupancy then
    // equals the field quantile and no longer inherits the seed-dependent
    // vertical-max inflation of a 3D threshold. The 3D density body,
    // column top/bottom and lens profile still provide the volume.
    const float3 p = float3(position.x, kReferenceLocalOccupancyY, position.z);
    return ReferenceLocalAnomaly(p) +
           0.12f * ReferenceMacroAnomaly(p);
}
#endif

float ReferenceMacroMass(float3 position, float detailTransfer)
{
    const float field = ReferenceMacroField(position);
    float centeredDetail = detailTransfer - 0.5f;
#ifdef OGSR_A03A
    if (ReferenceMacroCoverageEnabled())
    {
        // The R detail transfer averages ~0.05, not 0.5. Centring the edge
        // jitter on the measured mean removes a systematic -0.068 bias that
        // otherwise erases whole macro columns.
        centeredDetail = detailTransfer - 0.05f;
    }
#endif
    const float jitter = centeredDetail * ReferenceMacro2.z;
    float low = ReferenceMacro.z;
#ifdef OGSR_A03A
    if (ReferenceMacroWeatherLocalEnabled())
    {
        // W2: local mean of the primary macro octave from four taps spanning
        // 6 km. The weather quantile offset is applied to this local mean, so
        // a dense low-frequency region does not saturate a Clear preset.
        const float3 pxp = position + float3(6000.0f, 0.0f, 0.0f);
        const float3 pxm = position - float3(6000.0f, 0.0f, 0.0f);
        const float3 pzp = position + float3(0.0f, 0.0f, 6000.0f);
        const float3 pzm = position - float3(0.0f, 0.0f, 6000.0f);
        const float3 phase = ReferenceMacroPhaseA();
        const float meanN1 =
            0.25f * (ReferenceMacroNoise(pxp, ReferenceMacro.x, phase) +
                     ReferenceMacroNoise(pxm, ReferenceMacro.x, phase) +
                     ReferenceMacroNoise(pzp, ReferenceMacro.x, phase) +
                     ReferenceMacroNoise(pzm, ReferenceMacro.x, phase));
        low = meanN1 + ReferenceMacro3.z;
    }
    else if (ReferenceMacroWeatherQuantileEnabled())
    {
        // W1: threshold the continuous macro value at the coverage quantile.
        // This runs before any saturation; weather controls occupancy, not
        // the brightness of an already saturated mass.
        low = 0.5f + ReferenceMacro3.z;
    }
    else if (ReferenceMacroCalibrateEnabled())
    {
        // A03a C3-2 experiment (kept for provenance): coarse local mean.
        const float coarse =
            ReferenceMacroNoise(position, 12000.0f, ReferenceMacroPhaseA());
        low = 0.5f + (coarse - 0.5f) * 0.5f + ReferenceMacro3.z;
    }
#endif
    const float high = low + max(ReferenceMacro.w, 0.01f);
    return smoothstep(low, high, field + jitter);
}

float ReferenceHeightFraction(float worldY)
{
    const float bottom = LayerBottom();
    const float thickness = max(LayerTop() - bottom, 1.0f);
    return (worldY - bottom) / thickness;
}

float ReferenceHeightProfile(float worldY)
{
    const float bottom = LayerBottom();
    const float top = LayerTop();
    const float thickness = top - bottom;
    if (thickness <= 1e-3f)
        return 0.0f;

    const float t = (worldY - bottom) / thickness;
    if (t <= 0.0f || t >= 1.0f)
        return 0.0f;

    const float baseFade = smoothstep(0.0f, kReferenceBaseFade, t);
    const float topFade = 1.0f - smoothstep(1.0f - kReferenceTopFade, 1.0f, t);
    return saturate(baseFade * topFade);
}

float3 ReferenceNoiseCoordinate(float3 position)
{
    // V1 wind contract: horizontal world offset is subtracted; Y is vertical
    // world space and is never replaced by a normalized layer coordinate.
    const float scale = WindNoiseScale.w;
    const float3 wind = WindNoiseScale.xyz;
    return float3(
        (position.x - wind.x) * scale,
        position.y * scale * kReferenceVerticalNoiseScale,
        (position.z - wind.z) * scale);
}

float ReferenceStormNoise(float3 position)
{
    const float3 p = float3(
        (position.x - WindNoiseScale.x) / 5000.0f,
        position.y / 5000.0f,
        (position.z - WindNoiseScale.z) / 5000.0f) +
        float3(ReferenceDebug.z * 11.0f,
               ReferenceDebug.w * 7.0f,
               ReferenceDebug.z * 3.0f);
    return ReferenceValueNoise(p);
}

// A02c: the same non-periodic 5 km field sampled on a fixed horizontal plane.
// Removing sample Y makes the storm envelope a true world-XZ column property.
float ReferenceStormNoiseXZ(float3 position)
{
    const float3 p = float3(
        (position.x - WindNoiseScale.x) / 5000.0f,
        0.5f,
        (position.z - WindNoiseScale.z) / 5000.0f) +
        float3(ReferenceDebug.z * 11.0f,
               ReferenceDebug.w * 7.0f,
               ReferenceDebug.z * 3.0f);
    return ReferenceValueNoise(p);
}

float2 ReferenceStormColumnBounds(float3 position)
{
    // bottom = f(worldXZ), top = g(worldXZ). No sample Y enters the field,
    // so every voxel in a vertical column sees the same boundaries. The
    // two fields keep the original phase separation; only Y dependence is
    // removed. Both are C2 value noise without a modulo period.
    const float nTop = ReferenceStormNoiseXZ(position);
    const float3 p = float3(
        (position.x - WindNoiseScale.x) / 5000.0f,
        0.5f,
        (position.z - WindNoiseScale.z) / 5000.0f) +
        float3(5.3f, 9.1f, 2.7f) +
        float3(ReferenceDebug.w * 11.0f,
               ReferenceDebug.z * 7.0f,
               ReferenceDebug.w * 3.0f);
    const float nBottom = ReferenceValueNoise(p);
    const float bottom =
        LayerBottom() + (nBottom - 0.5f) * 2.0f * ReferenceStorm.z;
    float top = LayerTop() + (nTop - 0.5f) * 2.0f * ReferenceStorm.y;
    top = max(top, bottom + 150.0f);
    return float2(bottom, top);
}

float ReferenceStormTop(float3 position)
{
    if (ReferenceStorm.w < 0.5f)
        return LayerTop();
    const float n = ReferenceStormNoise(position);
    return LayerTop() + (n - 0.5f) * 2.0f * ReferenceStorm.y;
}

float ReferenceStormBottom(float3 position)
{
    if (ReferenceStorm.w < 0.5f)
        return LayerBottom();
    const float3 p = float3(
        (position.x - WindNoiseScale.x) / 5000.0f,
        position.y / 5000.0f,
        (position.z - WindNoiseScale.z) / 5000.0f) +
        float3(5.3f, 9.1f, 2.7f) +
        float3(ReferenceDebug.w * 11.0f,
               ReferenceDebug.z * 7.0f,
               ReferenceDebug.w * 3.0f);
    const float n = ReferenceValueNoise(p);
    return LayerBottom() + (n - 0.5f) * 2.0f * ReferenceStorm.z;
}

float ReferenceHeightProfileAtScaled(float3 position, float heightScale)
{
    if (ReferenceStorm.w < 0.5f)
    {
        float bottom = LayerBottom();
        float top = LayerTop();
#ifdef OGSR_A03B
        if (ReferenceLocalOccupancyEnabled())
        {
            top += (ReferenceColumnTopNoise(position) - 0.5f) * 1200.0f;
            bottom += (ReferenceColumnBottomNoise(position) - 0.5f) * 1000.0f;
            top = max(top, bottom + 300.0f);
        }
#endif
        if (heightScale < 0.999f)
            top = bottom + (top - bottom) * saturate(heightScale);
        const float thickness = top - bottom;
        if (thickness <= 1e-3f)
            return 0.0f;
        const float t = (position.y - bottom) / thickness;
        if (t <= 0.0f || t >= 1.0f)
            return 0.0f;
        float baseFade = smoothstep(0.0f, kReferenceBaseFade, t);
        float topFade =
            1.0f - smoothstep(1.0f - kReferenceTopFade, 1.0f, t);
#ifdef OGSR_A03B
        if (ReferenceLocalOccupancyEnabled())
        {
            // A03b non-storm clouds are lens-shaped: density tapers over the
            // lower/upper fifth instead of filling a flat layer slab.
            baseFade = smoothstep(0.0f, 0.22f, t);
            topFade = 1.0f - smoothstep(0.78f, 1.0f, t);
        }
#endif
        return saturate(baseFade * topFade);
    }

    float bottom;
    float top;
    if (ReferenceStorm.w > 1.5f)
    {
        const float2 bounds = ReferenceStormColumnBounds(position);
        bottom = bounds.x;
        top = bounds.y;
    }
    else
    {
        bottom = ReferenceStormBottom(position);
        top = max(ReferenceStormTop(position), bottom + 150.0f);
    }
    const float thickness = top - bottom;
    if (thickness <= 1e-3f)
        return 0.0f;
    const float t = (position.y - bottom) / thickness;
    if (t <= 0.0f || t >= 1.0f)
        return 0.0f;
    const float baseFade = smoothstep(0.0f, kReferenceBaseFade, t);
    const float topFade = 1.0f - smoothstep(1.0f - kReferenceTopFade, 1.0f, t);
    return saturate(baseFade * topFade);
}

float ReferenceHeightProfileAt(float3 position)
{
    return ReferenceHeightProfileAtScaled(position, 1.0f);
}

float3 ReferenceBaseNoiseCoordinate(float3 position)
{
    float3 coordinate =
        ReferenceNoiseCoordinate(position) * kReferenceBaseScale;
    if (ReferenceShape.y > 0.5f)
    {
        // Soft bounded warp: one low-frequency 3D sample, max offset 0.10
        // noise units. This is the only A01 decorrelation candidate.
        const float3 warp = NoiseTexture.SampleLevel(
            NoiseSampler,
            coordinate * kReferenceWarpScale + kReferenceWarpOffset, 0).rgb;
        coordinate += (warp - 0.5f) * (2.0f * kReferenceWarpAmount);
    }
    return coordinate;
}

float SampleReferenceShapeField(float3 position, out float4 noise)
{
    const float3 coordinate = ReferenceBaseNoiseCoordinate(position);
    noise = NoiseTexture.SampleLevel(NoiseSampler, coordinate, 0);
    const uint mode = (uint)ReferenceField.x;

    if (mode == 1u)
    {
        // Candidate A: two R samples at a non-integer scale ratio (0.73 by
        // default). The second sample multiplies the base shape instead of
        // averaging it: the base keeps its fine structure, while the second
        // field breaks the visible identity of the 4.53 km copies. The
        // combined exact period is 100 base tiles (~454 km), far outside the
        // measured 40 km range. Wind translation is preserved because both
        // samples use the same wind-corrected coordinate.
        const float3 phase =
            float3(0.37f, 0.61f, 0.19f) +
            ReferenceDebug.z * float3(0.71f, 0.29f, 0.47f) +
            ReferenceDebug.w * float3(0.13f, 0.43f, 0.67f);
        const float4 second = NoiseTexture.SampleLevel(
            NoiseSampler, coordinate * ReferenceField.y + phase, 0);
        const float weight = ReferenceField.z;
        const float modulation = lerp(
            1.0f - weight, 1.0f + weight, second.r);
        return saturate(noise.r * modulation);
    }

    if (mode == 2u)
    {
        // Candidate B: non-periodic world-space low-frequency modulation.
        // The hash lattice has no modulo, so it does not inherit the 4.53 km
        // NoiseVolume tile. Wind is subtracted on XZ to preserve the V1 wind
        // translation contract.
        const float3 p = float3(
            (position.x - WindNoiseScale.x) / 12000.0f,
            position.y / 12000.0f,
            (position.z - WindNoiseScale.z) / 12000.0f);
        const float3 phase = float3(
            ReferenceDebug.z * 17.0f,
            ReferenceDebug.w * 13.0f,
            ReferenceDebug.z * 7.0f + ReferenceDebug.w * 3.0f);
        const float modulation = ReferenceValueNoise(p + phase);
        return saturate(noise.r +
                        (modulation - 0.5f) * ReferenceField.w);
    }

    if (mode == 3u)
    {
        // A03: macro placement before detail. The periodic R channel is
        // transferred and used as an interior/edge detail only; it does not
        // decide where the large masses are. mass is a non-tiling world-space
        // field from ReferenceMacroNoise (no wrap period).
        // A03a (flag): the single coverage remap runs on the macro mass
        // before the detail multiplier, so detail cannot delete a column.
        const float detailRaw = ReferenceShapeFieldValue(noise);
        const float detail = smoothstep(
            ReferenceShapeLow(), ReferenceShapeHigh(), detailRaw);
#ifdef OGSR_A03B
        if (ReferenceLocalOccupancyEnabled())
        {
            // A03b: local occupancy drives presence; the macro anomaly is a
            // placement bias. The weather control reaches the gate through
            // the global gamma curve in the constant buffer. Detail still
            // shapes the edges.
            const float localCut = ReferenceMacro3.z;
            const float localOcc = smoothstep(
                localCut - 0.05f, localCut + 0.05f,
                ReferenceLocalOccupancyField(position));
            const float sparse = lerp(0.85f, 1.0f, detail);
            return saturate(localOcc * sparse);
        }
#endif
        float mass = ReferenceMacroMass(position, detail);
#ifdef OGSR_A03A
        if (ReferenceMacroCoverageEnabled() &&
            !ReferenceMacroCalibrateEnabled() &&
            !ReferenceMacroWeatherQuantileEnabled() &&
            !ReferenceMacroWeatherLocalEnabled() &&
            (uint)LightingFlags.w >= (uint)3u)
        {
            mass = ReferenceCoverageRemap(mass);
        }
#endif
        const float weight = saturate(ReferenceMacro2.x);
        float compensated = mass;
#ifdef OGSR_A03A
        if (ReferenceMacroDetailCompensateEnabled())
        {
            // mean detail transfer of the 128^3 R field is ~0.05; the
            // multiplier therefore averages 1 - w*0.95. Apply the inverse on
            // the macro mass so detail keeps its contrast without deleting
            // the column. The gain is a transfer calibration, not a new
            // weather system.
            const float meanDetail = 0.05f;
            const float meanMultiplier =
                1.0f - weight * (1.0f - meanDetail);
            compensated = saturate(mass / max(meanMultiplier, 0.05f));
        }
#endif
        return saturate(compensated * lerp(1.0f - weight, 1.0f, detail));
    }

    // N0/N1 direct path: R-only, B-only or 0.70R+0.30B. This is bit-identical
    // to V2.3h-A01 when the A02 controls are at their defaults.
    return ReferenceShapeFieldValue(noise);
}

float SampleReferenceBaseShape(float3 position, out float4 noise,
                               out float shapeField)
{
    shapeField = SampleReferenceShapeField(position, noise);
    if ((uint)ReferenceField.x == 3u)
    {
        // The macro path already applied the detail transfer and the mass
        // gating; applying the transfer again would re-cut the silhouette.
        return saturate(shapeField);
    }
    // The transfer itself is one fixed smoothstep.
    return smoothstep(ReferenceShapeLow(), ReferenceShapeHigh(), shapeField);
}

// A02c: one shared evaluation used by the visible renderer and by the Float32
// volume dump. The exported density is therefore the exact value that the ray
// marcher integrates; no diagnostic reimplementation can drift from it.
struct ReferenceDensityFields
{
    float rawShape;     // base field after the A01/A02 field mode
    float transfer;     // fixed smoothstep transfer
    float shaped;       // transfer plus the single coverage remap
    float envelope;     // height profile (0 outside the column)
    float gap;          // storm large-scale mask (1 when inactive)
    float density;      // saturated final density used by the ray marcher
};

ReferenceDensityFields SampleReferenceDensityFields(float3 position,
                                                     out float4 noise)
{
    ReferenceDensityFields fields;
    fields.rawShape = 0.0f;
    fields.transfer = 0.0f;
    fields.shaped = 0.0f;
    fields.envelope = 0.0f;
    fields.gap = 1.0f;
    fields.density = 0.0f;
    noise = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // F01 analytic probe: identical world-space values through the volume and
    // column paths. 1 = constant 0.5, 2 = vertical X stripe.
    if (ReferenceMacro4.w > 0.5f)
    {
        const float pattern = ReferenceMacro4.w < 1.5f
            ? 0.5f
            : (frac(position.x / 2000.0f) < 0.5f ? 1.0f : 0.0f);
        fields.rawShape = pattern;
        fields.transfer = pattern;
        fields.shaped = pattern;
        fields.envelope = 1.0f;
        fields.density = pattern;
        return fields;
    }

    const float densityMultiplier =
        max(LayerShape.z, 0.0f) * max(ReferenceStorm.x, 0.0f);
    if (densityMultiplier <= 0.0f)
        return fields;

    float shapeField;
    const float baseShape =
        SampleReferenceBaseShape(position, noise, shapeField);
    fields.rawShape = shapeField;
    fields.transfer = baseShape;

    const uint stage = (uint)LightingFlags.w;
    fields.envelope = 1.0f;
    if (stage >= (uint)2u)
    {
        // A03 macro masses also thin the layer towards their edges. Storm
        // keeps the accepted S2new envelope unchanged.
        float heightScale = 1.0f;
        if ((uint)ReferenceField.x == 3u && ReferenceStorm.w < 0.5f)
        {
            heightScale = lerp(1.0f - ReferenceMacro3.x, 1.0f,
                               saturate(baseShape));
        }
        fields.envelope =
            ReferenceHeightProfileAtScaled(position, heightScale);
    }

    float shape = baseShape;
    float coverage = 0.0f;
    if (stage >= (uint)3u)
    {
        // H3: exactly one monotonic coverage remap, no separate visibility
        // multiplier. coverage = 0 is an empty volume by construction.
        coverage = saturate(LayerShape.y);
        if (coverage <= kReferenceCoverageEpsilon)
            return fields;
#ifdef OGSR_A03A
        if ((uint)ReferenceField.x == 3u && ReferenceMacroCoverageEnabled())
            shape = saturate(baseShape);
        else
#endif
            shape = saturate((shapeField - (1.0f - coverage)) /
                             max(coverage, kReferenceCoverageEpsilon));
    }
    fields.shaped = shape;

    float density = shape * fields.envelope;
    if (ReferenceStorm.w > 0.5f && stage >= (uint)2u)
    {
        // Large-scale Storm geometry: low regions of the same non-periodic
        // field become gaps, high regions become tall heavy masses. A02c
        // keeps this as a 3D density modulation; only the column envelope
        // bounds were made XZ-stable, so the accepted S2 look is preserved.
        const float stormField = ReferenceStormNoise(position);
        fields.gap = smoothstep(0.45f, 0.60f, stormField);
#ifdef OGSR_A03A
        if ((uint)ReferenceField.x == 3u &&
            ReferenceStormMacroGapEnabled())
        {
            // A03a: the macro mass already provides the large-scale
            // placement. The 5 km storm gap would be a second independent
            // sparse mask and double-gate the field (56% -> 14.6% columns).
            // Keep the S2new column envelope; replace only the redundant gap.
            fields.gap = 1.0f;
        }
#endif
        density *= fields.gap;
    }
    if (stage >= (uint)4u)
    {
        // H4: bounded medium-frequency Worley erosion. Only the boundary is
        // modified; the dense core remains intact.
        const float3 detailCoordinate =
            ReferenceNoiseCoordinate(position) * 2.7f +
            float3(0.31f, 0.17f, 0.53f);
        const float4 detail = NoiseTexture.SampleLevel(
            NoiseSampler, detailCoordinate, 0);
        const float edge = 1.0f - smoothstep(0.35f, 0.75f, shape);
        const float erosion = saturate(detail.g * 1.15f);
        density *= lerp(1.0f, erosion, edge * 0.55f);
    }

    fields.density = saturate(density * densityMultiplier);

#ifdef OGSR_A03B
    if (ReferenceStorm.w > 0.5f && ReferenceStormLocalEnabled())
    {
        // Operate on the final density so the accepted S2new core above 0.5
        // is bit-identical. Only the low-density shell and gaps are carved,
        // so 6-neighbour connectivity of the heavy front is inherited.
        const float local = ReferenceLocalField(position);
        const float mask = smoothstep(0.35f, 0.65f, local);
        const float core = smoothstep(0.30f, 0.50f, fields.density);
        fields.density = saturate(fields.density *
            lerp(lerp(0.30f, 1.0f, mask), 1.0f, core));
    }
#endif
    return fields;
}

ReferenceDensityFields SampleReferenceDensityFields(float3 position)
{
    float4 unused;
    return SampleReferenceDensityFields(position, unused);
}

float SampleReferenceCloudDensity(float3 position, out CloudDensityDebug dbg)
{
    dbg.height = 0.0;
    dbg.profile = 0.0;
    dbg.shaped = 0.0;
    dbg.macroR = 0.0;
    dbg.midG = 0.0;
    dbg.weatherR = 0.0;
    dbg.typeValue = 0.0;
    dbg.coverage = 0.0;

    float4 noise;
    const ReferenceDensityFields fields =
        SampleReferenceDensityFields(position, noise);
    dbg.height = ReferenceHeightFraction(position.y);
    dbg.profile = fields.envelope;
    dbg.shaped = fields.shaped;
    dbg.macroR = noise.r;
    dbg.midG = noise.g;
    dbg.weatherR = 0.0f;
    dbg.typeValue = 0.0f;
    dbg.coverage = (uint)LightingFlags.w >= (uint)3u
        ? saturate(LayerShape.y) : 0.0f;
    return fields.density;
}

#endif // OGSR_REFERENCE_DENSITY_INCLUDED
