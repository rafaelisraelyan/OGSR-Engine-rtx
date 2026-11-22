// OGSR Volumetric Clouds V1 - compute ray marcher.
//
// Output convention:
//   rgb = premultiplied in-scattered radiance (linear)
//   a   = transmittance (1 = clear sky, 0 = fully opaque)
// Debug modes write a displayable value in rgb and keep a = 1.

#pragma warning(disable: 4000)

#ifndef OGSR_PROBE
#define OGSR_PROBE 0
#endif

#ifndef OGSR_SHAPE_PROTOTYPE
#define OGSR_SHAPE_PROTOTYPE 0
#endif

#ifndef OGSR_FORMULA
#define OGSR_FORMULA 0
#endif

#ifndef OGSR_DENSITY_MODEL
#define OGSR_DENSITY_MODEL 0
#endif

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
    float4 WeatherA;                    // coverage/type/density biases, storminess
    float4 WeatherB;                    // inverse world size, front offset/width/strength
    float4 WeatherC;                    // x height variation, y evolution (unused), z volume margin, w ablation
    float4 Lighting;                    // x sun shadow cap (m), y shadow steps, z multiple scatter, w ambient scale
    float4 Atmosphere;                  // x enabled, y extinction, z height falloff, w jitter mode
    float4 LightingFlags;               // x lighting mode, y self-shadow, z formula bias, w reference stage
    float4 Morphology;
    float4 ReferenceShape;              // x mix, y coordinate mode, z shape low, w shape high
    float4 ReferenceDebug;              // x axis, y offset, z/w seed phases
    float4 ReferenceField;              // x mode, y multi scale, z weight, w modulation
    float4 ReferenceStorm;              // x density, y top var, z bottom var, w flag
    float4 ReferenceVolume;             // xyz grid origin, w cell size (m)
    float4 ReferenceGrid;               // xyz resolution, w probe mode
    float4 ReferenceAudit;              // x/y boundary heights, z/w flags
    float4 ReferenceMacro;              // x scale1, y scale2, z threshold, w softness
    float4 ReferenceMacro2;             // x detail, y secondary, z edge jitter, w seed
    float4 ReferenceMacro3;             // x height weight, y flags, z/w reserved
    float4 ReferenceMacro4;             // xyz hashed seed phase, w reserved
};

Texture3D<float4> NoiseTexture : register(t0);
Texture2D<float4> WeatherTexture : register(t1);
SamplerState NoiseSampler : register(s0);
RWTexture2D<float4> CloudOutput : register(u0);
#if OGSR_PROBE
RWStructuredBuffer<float4> ProbeOutput : register(u1);
#endif

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

float DensityMargin()
{
    // Conservative margin computed on the CPU from height variation and layer
    // thickness. It lets IntersectLayer see the full possible local volume.
    return max(WeatherC.z, 0.0);
}

float VolumeBottom()
{
    return LayerBottom() - DensityMargin();
}

float VolumeTop()
{
    return LayerTop() + DensityMargin();
}

float VolumeThickness()
{
    return max(VolumeTop() - VolumeBottom(), 1.0);
}

bool IntersectLayer(float3 origin, float3 direction, out float t0, out float t1)
{
    t0 = 0.0;
    t1 = -1.0;

    // Use the conservative volume bounds, not the nominal slab: local height
    // warp and local cloud base/top may extend beyond LayerBottom/LayerTop.
    const float bottom = VolumeBottom();
    const float top = VolumeTop();
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

float3 WeatherWorldPosition(float3 position)
{
    // Weather advects with the same world offset as the cloud noise. This is
    // required for the V1 wind translation invariant: translating the camera
    // by windOffset must also translate the weather field by the same amount.
    return position - float3(WindNoiseScale.x, 0.0, WindNoiseScale.z);
}

float4 SampleWeather(float3 position)
{
    if ((uint)WeatherC.w == (uint)1) // ablation: no-weather
        return float4(0.5, 0.5, 0.5, 0.5);
    const float2 uv = WeatherWorldPosition(position).xz * WeatherB.x;
    return WeatherTexture.SampleLevel(NoiseSampler, uv, 0);
}

float3 CloudTypeWeights(float typeValue)
{
    // Three overlapping bands with a smooth hand-off. The type field is kept
    // separate from coverage so one frame can contain cumulus, stratocumulus
    // and overcast regions at the same time.
    const float cumulus = 1.0 - smoothstep(0.24, 0.56, typeValue);
    const float overcast = smoothstep(0.62, 0.88, typeValue);
    const float stratocumulus = max(0.0, 1.0 - cumulus - overcast);
    const float sum = max(cumulus + stratocumulus + overcast, 1e-3);
    return float3(cumulus, stratocumulus, overcast) / sum;
}

float LocalHeightFraction(float3 position, float4 weather, float overcastWeight,
                          float4 baseNoise)
{
    // Move the bottom and top locally. This is what prevents the overcast
    // layer from reading as a perfectly flat ceiling with a texture pasted on.
    const uint ablation = (uint)WeatherC.w;
    const bool noHeightVariation = ablation == 15u;
    const float variation = noHeightVariation ? 0.0 : WeatherC.x;
    const float bottom = LayerBottom() +
        (weather.b - 0.5) * variation * (0.35 + 0.65 * overcastWeight);
    const float top = LayerTop() +
        (weather.g - 0.5) * variation * 0.70;
    float height = (position.y - bottom) / max(top - bottom, 1.0);
    const float warp = (baseNoise.r - 0.5) * 0.28 +
                       (baseNoise.b - 0.5) * 0.12 +
                       (noHeightVariation ? 0.0 : (weather.a - 0.5) * 0.08);
    // Do not clamp here. The profile functions return zero outside [0,1], and
    // the expanded IntersectLayer bounds must be able to see a local base below
    // or a local top above the nominal slab.
    height += warp * (0.55 + 0.45 * overcastWeight);
    return height;
}

float CloudHeightProfile(float height, float3 typeWeights)
{
    const float cumulus = smoothstep(0.08, 0.20, height) *
                          (1.0 - smoothstep(0.55, 0.82, height));
    const float stratocumulus = smoothstep(0.0, 0.045, height) *
                                (1.0 - smoothstep(0.34, 0.66, height));
    const float overcast = smoothstep(0.0, 0.025, height) *
                           (1.0 - smoothstep(0.86, 1.0, height));
    return saturate(cumulus * typeWeights.x +
                    stratocumulus * typeWeights.y +
                    overcast * typeWeights.z);
}

// F2 applies the nonlinear shape transfer before the type blend, so each
// type keeps its own profile and only the resulting densities are mixed.
float ShapeTransferF2(float value)
{
    value = saturate(value);
    value = value * value * (3.0 - 2.0 * value);
    return pow(value, 0.85);
}

float FrontMask(float3 position)
{
    // Broad world-space plane, not a screen-space vignette. The map's A
    // channel modulates the storm strength inside this band.
    const float2 direction = normalize(float2(0.92, 0.39));
    const float distance = dot(WeatherWorldPosition(position).xz, direction) -
                           WeatherB.y;
    return 1.0 - smoothstep(-max(WeatherB.z, 1.0), max(WeatherB.z, 1.0),
                            distance);
}

float PixelJitter(uint2 pixel)
{
    // Deterministic spatial jitter hook. It is currently disabled because the
    // 8-bit screenshot path has no temporal accumulation to resolve 1-spp
    // noise; keep the function for the future temporal pipeline.
    return frac(52.9829189 * frac(dot(float2(pixel) + 0.5,
                                      float2(0.06711056, 0.00583715))));
}

// Density is a composition of independent scales:
//   - weather map: large-scale coverage, type, local density and storm zone;
//   - local height warp: non-flat bottom/top boundaries;
//   - cloud type: cumulus, stratocumulus or overcast shape and profile;
//   - macro 3D noise: the actual volume;
//   - detail/fine erosion: diagnostic-only paths (ablation 11/12).
//
// V2.3B: there is exactly one geometry function. The previous bool argument
// silently replaced the base noise with a constant for shadow samples, which
// changed the domain warp, the local height and therefore the shadowed shape.
// Diagnostic component capture for the ray-profile mode. The struct is
// populated by the same geometry function used for rendering, so the exported
// numbers are exactly the values that produced the visible density.
struct CloudDensityDebug
{
    float height;
    float profile;
    float shaped;
    float macroR;
    float midG;
    float weatherR;
    float typeValue;
    float coverage;
};

#if OGSR_DENSITY_MODEL == 1
#include "CloudReferenceDensity.hlsl"
#endif

float SampleCloudDensityInternal(float3 position, out CloudDensityDebug dbg)
{
    dbg.height = 0.0;
    dbg.profile = 0.0;
    dbg.shaped = 0.0;
    dbg.macroR = 0.0;
    dbg.midG = 0.0;
    dbg.weatherR = 0.0;
    dbg.typeValue = 0.0;
    dbg.coverage = 0.0;

#if OGSR_DENSITY_MODEL == 1
    // H1/H2 reference model: no legacy type, height-warp, weather, thresholds
    // or erosion. The selected stage is carried in LightingFlags.w.
    return SampleReferenceCloudDensity(position, dbg);
#else
    const float coverageGlobal = saturate(LayerShape.y);
    const float densityMultiplier = max(LayerShape.z, 0.0);
    if (coverageGlobal <= 0.0 || densityMultiplier <= 0.0)
        return 0.0;

    const float scale = WindNoiseScale.w;
    const float3 windOffset = WindNoiseScale.xyz;
    const float4 weather = SampleWeather(position);
    const uint ablation = (uint)WeatherC.w;
    const bool macroOnly = ablation == 9u;
    const bool noWarp = ablation == 8u;
    const bool base3d = ablation == 13u;
    const bool noBaseWarp = ablation == 14u;
    const bool noHeightVariation = ablation == 15u;
    const bool noAnisotropy = ablation == 16u;
    const bool flatBottom = ablation == 17u;
    const bool noTypeProfile = ablation == 18u;
    const bool noTypeThresholds = ablation == 19u;
    const bool noTypeAnisotropy = ablation == 20u;
    const bool noTypeLocalBounds = ablation == 21u;
    const bool noTypeShape = ablation == 22u;
    const float typeValue = saturate(
        weather.g * 0.90 + WeatherA.y + (weather.b - 0.5) * 0.08);
    const float3 typeWeights = ablation == 7u
        ? float3(1.0, 0.0, 0.0)
        : CloudTypeWeights(typeValue);
    // V2.3c factor isolation: each weight set changes exactly one mechanism.
    const float3 localWeights =
        noTypeLocalBounds ? float3(1.0, 0.0, 0.0) : typeWeights;
    const float3 profileWeights =
        noTypeProfile ? float3(1.0, 0.0, 0.0) : typeWeights;
    const float3 anisotropyWeights =
        noTypeAnisotropy ? float3(1.0, 0.0, 0.0) : typeWeights;
    const float3 shapeWeights =
        noTypeShape ? float3(1.0, 0.0, 0.0) : typeWeights;
    const float overcastWeight = localWeights.z;

    // Diagnostic base-3d: the base field is normally a 2D XZ field (z=0.45).
    // This variant adds a slow vertical component to test whether the fixed
    // third coordinate is responsible for vertically coherent pillars.
    const float rawHeight = saturate(
        (position.y - LayerBottom()) / LayerThickness());
    const float3 baseCoordinate = float3(
        (position.x - windOffset.x) * scale * 0.70,
        (position.z - windOffset.z) * scale * 0.70,
        base3d ? (0.45 + rawHeight * Shaping.x * 0.35) : 0.45);
    const float4 baseNoise = (!macroOnly)
        ? (ablation == 3u ? float4(0.5, 0.5, 0.5, 1.0)
                          : NoiseTexture.SampleLevel(NoiseSampler,
                                                     baseCoordinate, 0))
        : float4(0.5, 0.5, 0.5, 1.0);
    // Diagnostic no-base-warp: keep baseNoise for the domain warp, but remove
    // its contribution to the local height field.
    const float4 warpNoise = noBaseWarp
        ? float4(0.5, 0.5, 0.5, 1.0) : baseNoise;

    const float height = LocalHeightFraction(position, weather, overcastWeight,
                                             warpNoise);
    dbg.height = height;
    const float profile = CloudHeightProfile(height, profileWeights);
#if OGSR_FORMULA != 2
    if (profile <= 0.0)
        return 0.0;
#endif
    dbg.profile = profile;

    const float storm = saturate(WeatherA.w * (0.55 + 0.45 * weather.a));
    const float front = FrontMask(position) * WeatherB.w;
    const float frontBoost = front * (0.25 + 0.35 * storm);

    // coverageGlobal is a control, not a visibility multiplier: it changes the
    // threshold/contrast of the macro field, while the weather map supplies the
    // spatial organization.
    const float coverage = saturate(
        0.15 + coverageGlobal * 0.65 + WeatherA.x +
        (weather.r - 0.5) * 1.20 + frontBoost);
    const float weatherGate = smoothstep(
        0.10, 0.55, weather.r + WeatherA.x * 0.35);
    const float gatedCoverage = coverage * weatherGate;
    dbg.weatherR = weather.r;
    dbg.typeValue = typeValue;
    dbg.coverage = coverage;

    // Type-specific vertical anisotropy: cumulus gets a full 3D tower, while
    // stratocumulus and overcast are progressively flatter layers. This is a
    // shape control, not a lighting trick.
    const float verticalAnisotropy = noAnisotropy ? 1.0 :
        (anisotropyWeights.x * 1.00 + anisotropyWeights.y * 0.55 +
         anisotropyWeights.z * 0.55);
#if OGSR_FORMULA == 4
    // F4: the macro 3D morphology is sampled directly in world XYZ. The
    // normalized height still drives the cloud height profile.
    const float3 coordinate = float3(
        (position.x - windOffset.x) * scale,
        position.y * scale * 2.2f * verticalAnisotropy,
        (position.z - windOffset.z) * scale);
#else
    const float3 coordinate = float3(
        (position.x - windOffset.x) * scale,
        (position.z - windOffset.z) * scale,
        height * Shaping.x * verticalAnisotropy);
#endif
    // Gentle low-frequency domain warp. The previous 0.55-noise-unit warp
    // created high-frequency interference/chevrons; 0.12 is enough to break
    // lattice alignment without destroying the mass silhouette.
    const float3 warpedCoordinate = noWarp ? coordinate : float3(
        coordinate.x + (baseNoise.r - 0.5) * 0.12,
        coordinate.y + (baseNoise.g - 0.5) * 0.12,
        coordinate.z + (baseNoise.b - 0.5) * 0.10);

    const float4 macro = NoiseTexture.SampleLevel(
        NoiseSampler, warpedCoordinate * 0.42 + float3(0.17, 0.31, 0.07), 0);
    dbg.macroR = macro.r;
    const float4 mid = (ablation == 2u || macroOnly)
        ? float4(0.5, 0.5, 0.5, 1.0)
        : NoiseTexture.SampleLevel(
              NoiseSampler, warpedCoordinate * 0.55 + float3(0.11, 0.47, 0.29),
              0);
    dbg.midG = mid.g;
    const float4 fine = (ablation == 12u)
        ? NoiseTexture.SampleLevel(
              NoiseSampler, warpedCoordinate * 3.1 + float3(0.53, 0.11, 0.37),
              0)
        : float4(0.5, 0.5, 0.5, 1.0);

    const float formulaThresholdBias =
        (OGSR_FORMULA == 1 || OGSR_FORMULA == 3)
            ? clamp(LightingFlags.z, -0.20f, 0.20f)
            : 0.0f;
#if OGSR_FORMULA == 3
    // F3: coverage is separated from the shape threshold. The type shape uses
    // fixed thresholds; spatial placement is applied once after the shape.
    const float thresholdCumulus = 0.55f + formulaThresholdBias;
    const float thresholdStrat = 0.45f + formulaThresholdBias;
    const float thresholdOvercast = 0.38f + formulaThresholdBias;
    const float weatherPlacement = smoothstep(
        0.10f, 0.55f, weather.r + WeatherA.x * 0.35f);
    const float coverageScale = smoothstep(
        0.15f, 0.55f, coverageGlobal);
    const float placement = weatherPlacement * coverageScale;
#else
    const float thresholdCumulus =
        lerp(0.66, 0.44, gatedCoverage) + formulaThresholdBias;
    const float thresholdStrat = noTypeThresholds
        ? thresholdCumulus : lerp(0.58, 0.34, gatedCoverage) +
            formulaThresholdBias;
    const float thresholdOvercast = noTypeThresholds
        ? thresholdCumulus : lerp(0.52, 0.30, gatedCoverage) +
            formulaThresholdBias;
    const float placement = 1.0f;
#endif

    // Macro shape owns the mass; low-frequency Worley gives it rounded cells.
    // Detail/fine only erode the boundary, not the dense core.
#if OGSR_FORMULA == 1
    // F1: Perlin-Worley (.b) is the primary shape field. The existing
    // mid-frequency Worley adds a small amount of cell organization.
    const float f1Base = macro.b * 0.85f + mid.g * 0.15f;
    const float cumulusBase = f1Base;
    const float stratBase = f1Base;
    const float overcastBase = f1Base;
#else
    const float cumulusBase = macro.r * 0.70 + macro.b * 0.20 + mid.g * 0.10;
    const float stratBase = macro.r * 0.70 + macro.b * 0.15 + mid.g * 0.15;
    const float overcastBase = macro.r * 0.70 + macro.b * 0.20 + mid.g * 0.10;
#endif

    const float cumulusShape = saturate(
        (cumulusBase - thresholdCumulus) / max(1.0 - thresholdCumulus, 0.05));
    const float stratShape = saturate(
        (stratBase - thresholdStrat) / max(1.0 - thresholdStrat, 0.05));
    const float overcastShape = saturate(
        (overcastBase - thresholdOvercast) /
        max(1.0 - thresholdOvercast, 0.05));

    float shaped = noTypeShape
        ? cumulusShape
        : (cumulusShape * shapeWeights.x +
           stratShape * shapeWeights.y +
           overcastShape * shapeWeights.z);
    shaped = shaped * shaped * (3.0 - 2.0 * shaped);
    shaped = pow(saturate(shaped), 0.85);
    dbg.shaped = shaped;

#if OGSR_FORMULA == 2
    const float cumulusProfileF2 = CloudHeightProfile(height, float3(1.0, 0.0, 0.0));
    const float stratProfileF2 = CloudHeightProfile(height, float3(0.0, 1.0, 0.0));
    const float overcastProfileF2 = CloudHeightProfile(height, float3(0.0, 0.0, 1.0));
    const float cumulusDensityF2 = ShapeTransferF2(cumulusShape) * cumulusProfileF2;
    const float stratDensityF2 = ShapeTransferF2(stratShape) * stratProfileF2;
    const float overcastDensityF2 = ShapeTransferF2(overcastShape) * overcastProfileF2;
    float shapeForEdge = typeWeights.x * ShapeTransferF2(cumulusShape) +
        typeWeights.y * ShapeTransferF2(stratShape) +
        typeWeights.z * ShapeTransferF2(overcastShape);
    float densityBase = typeWeights.x * cumulusDensityF2 +
        typeWeights.y * stratDensityF2 +
        typeWeights.z * overcastDensityF2;
    dbg.shaped = shapeForEdge;
#else
    float shapeForEdge = shaped;
    float densityBase = shaped * profile;
#endif
    float formulaDifference = 0.0f;
#if OGSR_FORMULA == 2
    formulaDifference = abs(densityBase - shaped * profile);
#endif
    float prototypeWeight = 0.0f;

#if OGSR_SHAPE_PROTOTYPE == 1
    // Prototype A: deep convective masses built from smoothed Worley/Perlin
    // cell distance fields, soft-unioned. The macro Perlin threshold is no
    // longer the primary shape.
    {
        const float billowA = 1.0f - abs(2.0f * macro.r - 1.0f);
        const float billowB = 1.0f - abs(2.0f * mid.r - 1.0f);
        const float blobA = smoothstep(0.55f, 0.88f, billowA);
        const float blobB = smoothstep(0.60f, 0.92f, billowB);
        const float cells = 1.0f - (1.0f - blobA) * (1.0f - blobB * 0.50f);
        const float topStart = 0.65f + 0.10f * storm;
        const float convectiveProfile =
            smoothstep(0.06f, 0.25f, height) *
            (1.0f - smoothstep(topStart, 0.92f, height));
        const float placement = smoothstep(0.35f, 0.80f, gatedCoverage);
        const float densityA = cells * convectiveProfile * placement;
        const float convectiveWeight =
            saturate(typeWeights.x + storm * 0.55f);
        prototypeWeight = convectiveWeight;
        densityBase = lerp(densityBase, densityA, convectiveWeight);
        shapeForEdge = lerp(shapeForEdge, cells * placement,
                            convectiveWeight);
    }
#elif OGSR_SHAPE_PROTOTYPE == 2
    // Prototype B: a continuous layered deck with 3D thickness and limited
    // large-scale boundary erosion. No thresholded macro iso-surface.
    {
        const float deckBottom = clamp(
            0.05f + (mid.r - 0.5f) * 0.25f + (macro.b - 0.5f) * 0.08f,
            0.0f, 0.40f);
        const float deckTop = clamp(
            0.86f + (macro.b - 0.5f) * 0.15f, 0.60f, 1.0f);
        const float soft = 0.10f + 0.06f * (1.0f - mid.g);
        float deck = smoothstep(deckBottom, deckBottom + soft, height) *
            (1.0f - smoothstep(deckTop - soft, deckTop, height));
        deck *= lerp(0.45f, 1.0f, macro.b) *
            lerp(0.65f, 1.0f, mid.g);
        const float erosion = smoothstep(0.20f, 0.75f, mid.r);
        const float boundaryBand =
            smoothstep(0.0f, 0.25f, height) *
            (1.0f - smoothstep(0.65f, 1.0f, height));
        deck *= lerp(1.0f, erosion, boundaryBand);
        const float gaps = smoothstep(
            0.06f, 0.38f, weather.r + (macro.b - 0.5f) * 0.25f);
        deck *= lerp(1.0f, gaps, typeWeights.y);
        const float densityB = deck;
        const float layeredWeight =
            saturate(typeWeights.y + typeWeights.z);
        prototypeWeight = layeredWeight;
        densityBase = lerp(densityBase, densityB, layeredWeight);
        shapeForEdge = lerp(shapeForEdge, deck, layeredWeight);
    }
#endif

    const float edgeFactor = macroOnly ? 0.0 :
        1.0 - smoothstep(0.35, 0.85, shapeForEdge);
    // Detail/fine are intentionally not multiplied into the V2.2b cumulus
    // density: their 128Ві slice signature produced regular stripes. They stay
    // in the shader behind diagnostic-only paths for future cirrus research.
    // Boundary erosion now comes from the low-frequency Worley mass field.
    float density = densityBase;
    density *= lerp(1.0, lerp(0.72, 1.0, mid.g), edgeFactor * 0.55);
#if OGSR_FORMULA == 5
    // F5: controlled medium-frequency boundary erosion using the existing
    // macro Worley channel. No new texture fetch and no high-frequency path.
    const float erosionStrength = clamp(LightingFlags.z, 0.0f, 1.0f);
    const float controlledErosion = smoothstep(0.45f, 0.90f, macro.g);
    density *= lerp(1.0f, controlledErosion,
                    edgeFactor * erosionStrength);
#endif

#if OGSR_FORMULA == 3
    density *= placement;
#else
    density *= weatherGate;
#endif
    density *= lerp(0.65, 1.35, weather.b);
    density *= 1.0 + storm * 0.35 + front * 0.25;
    // The top surface is a 3D height field, not a fixed plane. This is the
    // difference between a rippled ceiling and billowing cloud tops.
    float topThreshold = 0.88f -
        shapeWeights.x * 0.10f - shapeWeights.y * 0.04f +
        (macro.r - 0.5f) * 0.20f + (mid.g - 0.5f) * 0.10f;
    // V2.3d candidate 5: cumulus top widening is applied through the
    // threshold above, not by moving the top boundary.
    // V2.3c diagnostic: top fade width. A longer fade below the threshold
    // turns the thin cumulus spire into a softer, wider cloud top.
    const float topFade = clamp(Morphology.z, 0.02f, 0.30f);
    const float topCut = 1.0f - smoothstep(topThreshold - topFade,
                                           topThreshold + 0.06f, height);
    density *= lerp(topCut, 1.0f, prototypeWeight);
    // Ragged lower edge: the overcast base must not look like a flat ceiling.
    if (!flatBottom)
    {
        const float bottomCut = smoothstep(
            0.0f, 0.12f, height + (macro.r - 0.5f) * 0.08f);
        density *= lerp(bottomCut, 1.0f, prototypeWeight);
    }
    density *= densityMultiplier;
#if OGSR_FORMULA == 2
    if ((uint)Shaping.w == 17u)
        return saturate(formulaDifference * 4.0f);
#endif
    return saturate(density);
#endif
}

float SampleCloudDensity(float3 position)
{
    CloudDensityDebug unused;
    return SampleCloudDensityInternal(position, unused);
}

// V2.3B: the shadow path uses the same geometry as the primary path. Keeping
// the function separate makes the contract explicit and lets a future version
// trade small-scale detail for cost without touching base/top or mass layout.
float SampleCloudDensityForLight(float3 position)
{
    CloudDensityDebug unused;
    return SampleCloudDensityInternal(position, unused);
}

float HenyeyGreenstein(float cosTheta, float anisotropy)
{
    const float g = clamp(anisotropy, -0.85, 0.85);
    const float denominator = 1.0 + g * g - 2.0 * g * cosTheta;
    return (1.0 - g * g) /
           max(4.0 * kPi * pow(max(denominator, 1e-4), 1.5), 1e-4);
}

// V2.3D: cheap analytic aerial perspective. Extinction falls exponentially
// with altitude; the average height of the segment approximates the integral
// without a second ray march.
float AtmosphereOpticalDepth(float3 origin, float3 direction, float distance)
{
    const float heightFalloff = max(Atmosphere.z, 1.0);
    const float h0 = max(origin.y, 0.0);
    const float h1 = max(origin.y + direction.y * distance, 0.0);
    const float averageHeight = (h0 + h1) * 0.5;
    const float density = exp(-averageHeight / heightFalloff);
    return max(Atmosphere.y, 0.0) * density * max(distance, 0.0);
}

// Must stay identical to SkyColor() in CloudComposite.hlsl. The haze added in
// front of a cloud then matches the sky the composite would show behind it,
// so the silhouette has no brightness step.
float3 AtmosphereColor(float3 rayDirection)
{
    const float horizon = pow(saturate(1.0 - saturate(rayDirection.y)), 3.0);
    float3 sky = lerp(SkyZenith.xyz, SkyHorizon.xyz, horizon);
    if (rayDirection.y < 0.0)
    {
        const float down = saturate(-rayDirection.y);
        const float3 groundHaze = SkyHorizon.xyz * 0.10 + SkyZenith.xyz * 0.05;
        sky = lerp(SkyHorizon.xyz, groundHaze, pow(down, 0.45));
    }
    const float3 sunDirection = normalize(SunDirectionIntensity.xyz);
    const float sunDot = saturate(dot(rayDirection, sunDirection));
    // Keep the sky glow consistent with the cloud sun visibility: a sun below
    // the horizon must not paint a highlight into the haze.
    const float sunAboveHorizon = smoothstep(-0.03, 0.02, sunDirection.y);
    sky += SunColorAmbientIntensity.xyz * SunDirectionIntensity.w *
           pow(sunDot, 8.0) * 0.035 * sunAboveHorizon;
    sky += SunColorAmbientIntensity.xyz * SunDirectionIntensity.w *
           pow(sunDot, 256.0) * 0.20 * sunAboveHorizon;
    return sky;
}

// V2.3A: the sun ray is clipped to the conservative cloud slab and to the
// configured shadow distance. The old code marched a fixed 6 * full volume
// thickness regardless of sun elevation, so a vertical sun overshot the cloud
// top and a horizontal sun integrated an arbitrary path.
bool SunRaySegment(float3 position, out float t0, out float t1)
{
    const float3 sunDirection = normalize(SunDirectionIntensity.xyz);
    const float distanceCap = max(Lighting.x, 1.0);
    t0 = 0.0;
    t1 = distanceCap;

    if (sunDirection.y > 1e-4)
    {
        const float toTop = (VolumeTop() - position.y) / sunDirection.y;
        t1 = min(max(toTop, 0.0), distanceCap);
    }
    else if (sunDirection.y < -1e-4)
    {
        const float toBottom = (VolumeBottom() - position.y) / sunDirection.y;
        t1 = min(max(toBottom, 0.0), distanceCap);
    }
    // Near-horizontal sun has no finite slab exit; the distance cap is the
    // only bound, and it is always applied.
    return t1 > t0;
}

float OpticalDepthToSun(float3 position, out float pathLength)
{
    pathLength = 0.0;
    const uint ablation = (uint)WeatherC.w;
    if (ablation == 10u || LightingFlags.y < 0.5) // no self-shadow
        return 0.0;

    float t0 = 0.0;
    float t1 = 0.0;
    if (!SunRaySegment(position, t0, t1))
        return 0.0;

    int shadowSteps = clamp((int)Lighting.y, 1, 16);
    if (ablation == 5u)
        shadowSteps = 2;
    else if (ablation == 6u)
        shadowSteps = 4;

    const float stepLength = (t1 - t0) / float(shadowSteps);
    pathLength = t1 - t0;
    const float3 sunDirection = normalize(SunDirectionIntensity.xyz);
    float depth = 0.0;
    [loop]
    for (int i = 0; i < shadowSteps; ++i)
    {
        const float3 samplePosition =
            position + sunDirection * (t0 + (float(i) + 0.5) * stepLength);
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
    const uint debugMode = (uint)Shaping.w;

#if OGSR_PROBE
    if (debugMode == 14)
    {
        // World-space ray profile. The left 256 output columns hold
        // (density, height, profile, shaped); the next 256 hold
        // (macro.r, mid.g, weather.r, typeValue); the next 256 hold
        // (coverage, normalized distance, profile*shaped). Each output row is
        // a probe at that screen row along the configured column.
        const uint profileSteps = (uint)clamp((int)Shaping.z, 8, 256);
        const uint channel = pixel.x / 256u;
        const uint stepIndex = pixel.x % 256u;
        if (stepIndex >= profileSteps || channel > 3u)
        {
            CloudOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        const uint probeX = min(
            (uint)(saturate(Morphology.x) * float(width)), width - 1u);
        const float2 probeUv =
            (float2(float(probeX), float(pixel.y)) + 0.5) /
            float2(float(width), float(height));
        const float2 probeNdc =
            float2(probeUv.x * 2.0 - 1.0, 1.0 - probeUv.y * 2.0);
        const float probeTanHalfFov = tan(CameraRightFov.w * 0.5);
        const float3 probeDirection = normalize(
            CameraForward() +
            CameraRight() * (probeNdc.x * probeTanHalfFov *
                             CameraUpAspect.w) +
            CameraUp() * (probeNdc.y * probeTanHalfFov));
        float probeT0 = 0.0;
        float probeT1 = 0.0;
        if (!IntersectLayer(CameraPosition(), probeDirection, probeT0,
                            probeT1))
        {
            if (channel == 0u)
            {
                const uint probeBase =
                    (pixel.y * profileSteps + stepIndex) * 3u;
                ProbeOutput[probeBase + 0u] = float4(0, 0, 0, 0);
                ProbeOutput[probeBase + 1u] = float4(0, 0, 0, 0);
                ProbeOutput[probeBase + 2u] = float4(0, 0, 0, 0);
            }
            CloudOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        const float profileStep = (probeT1 - probeT0) / float(profileSteps);
        const float probeT =
            probeT0 + (float(stepIndex) + 0.5) * profileStep;
        const float3 probePosition =
            CameraPosition() + probeDirection * probeT;
        CloudDensityDebug profileDebug;
        const float profileDensity =
            SampleCloudDensityInternal(probePosition, profileDebug);
        if (channel == 0u)
        {
            const uint probeBase =
                (pixel.y * profileSteps + stepIndex) * 3u;
            ProbeOutput[probeBase + 0u] = float4(
                profileDensity, profileDebug.height, profileDebug.profile,
                profileDebug.shaped);
            ProbeOutput[probeBase + 1u] = float4(
                profileDebug.macroR, profileDebug.midG,
                profileDebug.weatherR, profileDebug.typeValue);
            ProbeOutput[probeBase + 2u] = float4(
                profileDebug.coverage,
                saturate(probeT / max(probeT1, 1.0)),
                profileDebug.profile * profileDebug.shaped, 0.0);
            CloudOutput[pixel] = float4(profileDensity, profileDebug.height,
                                        profileDebug.profile,
                                        profileDebug.shaped);
        }
        else if (channel == 1u)
        {
            CloudOutput[pixel] = float4(profileDebug.macroR,
                                        profileDebug.midG,
                                        profileDebug.weatherR,
                                        profileDebug.typeValue);
        }
        else if (channel == 2u)
        {
            CloudOutput[pixel] = float4(
                profileDebug.coverage,
                saturate(probeT / max(probeT1, 1.0)),
                profileDebug.profile * profileDebug.shaped, 1.0);
        }
        else
        {
            CloudOutput[pixel] = float4(profileDebug.shaped,
                                        profileDebug.typeValue, 0.0, 1.0);
        }
        return;
    }
#endif

#if OGSR_PROBE && OGSR_DENSITY_MODEL == 1
    if (debugMode == 18)
    {
        // V2.3h-A01 raw noise field. Only the first 256x256 pixels carry
        // data; the probe buffer stores three float4 records per pixel:
        //   0: noise.r, noise.g, noise.b, shapeField
        //   1: smoothstep, height profile, height fraction, density before coverage
        //   2: world xyz, texture fetch count
        // This path samples the reference field only; it does not run height
        // multiplication, coverage, erosion, ray marching or lighting.
        const uint fieldSize = 256u;
        if (pixel.x >= fieldSize || pixel.y >= fieldSize)
        {
            CloudOutput[pixel] = float4(0.0, 0.0, 0.0, 0.0);
            return;
        }

        const uint axis = (uint)(ReferenceDebug.x + 0.5);
        const float offset = saturate(ReferenceDebug.y);
        const float extent = max(Morphology.y, 2.0) * 1000.0;
        const float u = (float(pixel.x) + 0.5) / float(fieldSize);
        const float v = (float(pixel.y) + 0.5) / float(fieldSize);
        const float volumeY = VolumeBottom() + v * VolumeThickness();
        const float offsetX =
            CameraPosition().x + (offset - 0.5) * 2.0 * extent;
        const float offsetZ =
            CameraPosition().z + (offset - 0.5) * 2.0 * extent;
        float3 samplePosition;
        if (axis == 0u)
        {
            samplePosition = float3(
                CameraPosition().x + (u - 0.5) * 2.0 * extent,
                volumeY, offsetZ);
        }
        else if (axis == 1u)
        {
            samplePosition = float3(
                CameraPosition().x + (u - 0.5) * 2.0 * extent,
                VolumeBottom() + offset * VolumeThickness(),
                CameraPosition().z + (v - 0.5) * 2.0 * extent);
        }
        else
        {
            samplePosition = float3(
                offsetX, volumeY,
                CameraPosition().z + (u - 0.5) * 2.0 * extent);
        }

        float4 noise;
        const float shapeField =
            SampleReferenceShapeField(samplePosition, noise);
        const float transfer =
            smoothstep(ReferenceShapeLow(), ReferenceShapeHigh(), shapeField);
        const float profile = ReferenceHeightProfileAt(samplePosition);
        const float heightFraction =
            ReferenceHeightFraction(samplePosition.y);
        const uint stage = (uint)LightingFlags.w;
        float densityField = transfer * profile;
        if (stage >= 3u)
        {
            // Stage-aware density after the single coverage remap. This is
            // still before the density multiplier, ray marching and lighting.
            const float coverage = saturate(LayerShape.y);
            densityField = coverage <= kReferenceCoverageEpsilon
                ? 0.0f
                : saturate((shapeField - (1.0f - coverage)) /
                           max(coverage, kReferenceCoverageEpsilon)) *
                      profile;
        }

        const uint probeIndex =
            (pixel.y * fieldSize + pixel.x) * 3u;
        ProbeOutput[probeIndex + 0u] =
            float4(noise.r, noise.g, noise.b, shapeField);
        ProbeOutput[probeIndex + 1u] =
            float4(transfer, profile, heightFraction, densityField);
        ProbeOutput[probeIndex + 2u] =
            float4(samplePosition.x, samplePosition.y, samplePosition.z,
                   float(ReferenceTextureFetchCount()));

        CloudOutput[pixel] =
            float4(transfer, shapeField, noise.g, 1.0);
        return;
    }
#endif

#if OGSR_PROBE
    if (debugMode == 7)
    {
        // Vertical world-space slice through the cloud volume. X spans 4 km
        // around the camera, Y spans the conservative volume bounds. This is a
        // density cross-section, not a ray-integrated debug image.
        const float3 samplePosition = float3(
            CameraPosition().x - 2000.0 + uv.x * 4000.0,
            VolumeBottom() + uv.y * VolumeThickness(),
            CameraPosition().z);
        const float density = SampleCloudDensity(samplePosition);
        CloudOutput[pixel] = float4(saturate(density * 2.0).xxx, 1.0);
        return;
    }

    if (debugMode == 13)
    {
        // Horizontal world-space slice at a normalized height. Capturing the
        // same camera at several heights shows whether a screen-space pillar
        // is a real vertical column or only a ray/grazing effect.
        const float h = saturate(Morphology.w);
        const float extent = max(Morphology.y, 2.0) * 1000.0;
        const float3 samplePosition = float3(
            CameraPosition().x - extent + uv.x * extent * 2.0,
            VolumeBottom() + h * VolumeThickness(),
            CameraPosition().z - extent + uv.y * extent * 2.0);
        const float density = SampleCloudDensity(samplePosition);
        CloudOutput[pixel] = float4(saturate(density * 2.0).xxx, 1.0);
        return;
    }

    if (debugMode == 15)
    {
        // Vertical world-space slice along Z at the camera X. Used with a
        // camera moved to the defect XZ to cut the cloud mass locally.
        const float extent = max(Morphology.y, 2.0) * 1000.0;
        const float3 samplePosition = float3(
            CameraPosition().x,
            VolumeBottom() + uv.y * VolumeThickness(),
            CameraPosition().z - extent + uv.x * extent * 2.0);
        const float density = SampleCloudDensity(samplePosition);
        CloudOutput[pixel] = float4(saturate(density * 2.0).xxx, 1.0);
        return;
    }
#endif

    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float tanHalfFov = tan(CameraRightFov.w * 0.5);
    const float3 rayDirection = normalize(
        CameraForward() +
        CameraRight() * (ndc.x * tanHalfFov * CameraUpAspect.w) +
        CameraUp() * (ndc.y * tanHalfFov));

    float t0 = 0.0;
    float t1 = 0.0;
    const bool hitLayer = IntersectLayer(CameraPosition(), rayDirection, t0, t1);
    if (debugMode == 5 || debugMode == 6)
    {
        const float sampleDistance = hitLayer
            ? (t0 + t1) * 0.5
            : 8000.0;
        const float4 weather =
            SampleWeather(CameraPosition() + rayDirection * sampleDistance);
        if (debugMode == 5)
        {
            CloudOutput[pixel] = float4(weather.r.xxx, 1.0);
        }
        else
        {
            const float typeValue =
                saturate(weather.g * 0.90 + WeatherA.y);
            const float3 typeWeights = CloudTypeWeights(typeValue);
            const float3 typeColor =
                typeWeights.x * float3(0.18, 0.46, 0.95) +
                typeWeights.y * float3(0.20, 0.88, 0.56) +
                typeWeights.z * float3(0.95, 0.62, 0.28);
            CloudOutput[pixel] = float4(typeColor, 1.0);
        }
        return;
    }

#if OGSR_FORMULA == 2
    if (debugMode == 17)
    {
        const float sampleDistance = hitLayer
            ? (t0 + t1) * 0.5
            : 8000.0;
        CloudDensityDebug diffDebug;
        const float difference = SampleCloudDensityInternal(
            CameraPosition() + rayDirection * sampleDistance, diffDebug);
        CloudOutput[pixel] = float4(difference.xxx, 1.0);
        return;
    }
#endif

#if OGSR_PROBE
    if (debugMode == 16)
    {
        const float sampleDistance = hitLayer
            ? (t0 + t1) * 0.5
            : 8000.0;
        CloudDensityDebug shapeDebug;
        SampleCloudDensityInternal(
            CameraPosition() + rayDirection * sampleDistance, shapeDebug);
        CloudOutput[pixel] = float4(shapeDebug.shaped.xxx, 1.0);
        return;
    }
#endif

    if (!hitLayer)
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

    const int stepCount = clamp((int)Shaping.z, 8, 256);
    const float stepLength = (t1 - t0) / float(stepCount);
    // V2.3E: spatial jitter is deterministic per pixel; Off samples ray
    // centers so temporal comparisons have no sampling noise.
    const bool jitterEnabled = Atmosphere.w > 0.5;
    const float stepJitter = jitterEnabled ? PixelJitter(pixel) : 0.5;
    const float3 sunDirection = normalize(SunDirectionIntensity.xyz);
    const float anisotropy = AmbientColorAnisotropy.w;
    const float extinction = max(LayerShape.w, 0.0);
    const uint lightingMode = (uint)LightingFlags.x;
    const bool atmosphereEnabled = Atmosphere.x > 0.5;
    const bool lightingDebug = debugMode >= 8 && debugMode <= 12;

    float transmittance = 1.0;
    float3 scattered = 0.0;
    float densitySum = 0.0;
    float occupiedSteps = 0.0;
    float executedSteps = 0.0;
    float debugOpticalDepth = 0.0;
    float debugSunTransmittance = 1.0;
    float debugShadowPath = 0.0;
    float debugShadowDifference = 0.0;
    float depthWeightSum = 0.0;
    float depthWeightedDistance = 0.0;

    [loop]
    for (int step = 0; step < stepCount; ++step)
    {
        executedSteps += 1.0;
        const float t = t0 + (float(step) + stepJitter) * stepLength;
        const float3 position = CameraPosition() + rayDirection * t;
        const float density = SampleCloudDensity(position);
        densitySum += density;

        if (density > 1e-4 || lightingDebug)
        {
            if (density > 1e-4)
                occupiedSteps += 1.0;

            const float sigma = extinction * density;
            float shadowPath = 0.0;
            const float opticalDepth = OpticalDepthToSun(position, shadowPath);
            const float sunTransmittance = exp(-extinction * opticalDepth);
            const float height = HeightFraction(position.y);
            const float4 weather = SampleWeather(position);
            const float weatherThickness =
                saturate(weather.b * 0.80 + weather.a * 0.20);
            const float storm = saturate(WeatherA.w);

            if (lightingDebug)
            {
                debugOpticalDepth = max(
                    debugOpticalDepth, 1.0 - exp(-opticalDepth * 0.002));
                debugSunTransmittance =
                    min(debugSunTransmittance, sunTransmittance);
                debugShadowPath = max(
                    debugShadowPath,
                    saturate(shadowPath / max(Lighting.x, 1.0)));
                const float primaryDensity = SampleCloudDensity(position);
                const float lightDensity = SampleCloudDensityForLight(position);
                debugShadowDifference = max(
                    debugShadowDifference,
                    saturate(abs(primaryDensity - lightDensity) * 1000.0));
            }

            if (density > 1e-4)
            {
                // Direct sun: Beer-Lambert along the clipped sun segment.
                const float phase = lerp(
                    HenyeyGreenstein(
                        clamp(dot(rayDirection, sunDirection), -1.0, 1.0),
                        anisotropy),
                    1.0 / (4.0 * kPi), 0.30);
                const float sunAboveHorizon =
                    smoothstep(-0.03, 0.02, sunDirection.y);
                // Cheap multiple-scattering proxy. It only fills the part of
                // the signal that direct sunlight lost, so a fully lit sample
                // stays at 1.0 instead of being over-brightened.
                const float multiScatter =
                    max(Lighting.z, 0.0) *
                    (0.16 * exp(-opticalDepth * 0.15) +
                     0.06 * exp(-opticalDepth * 0.04));
                const float directVisibility =
                    saturate(sunTransmittance * sunAboveHorizon);
                const float multiScatterVisibility =
                    multiScatter * (0.40 + 0.60 * height) * sunAboveHorizon;
                const float sunVisibility = saturate(
                    directVisibility +
                    multiScatterVisibility * (1.0 - directVisibility));
                float3 sun =
                    SunColorAmbientIntensity.xyz * SunDirectionIntensity.w *
                    sunVisibility * phase * (1.0 - 0.30 * storm) *
                    lerp(0.78, 1.14, weatherThickness) *
                    lerp(1.08, 0.62, saturate(density * 2.0));

                // Diffuse skylight: zenith above, horizon below, occluded by
                // local density. This is the term that keeps Overcast bases
                // readable without inflating the sun.
                const float3 skyLight = lerp(
                    SkyHorizon.xyz, SkyZenith.xyz,
                    saturate(0.15 + 0.85 * height));
                const float densityOcclusion = exp(-density * 1.5);
                const float skyVisibility =
                    lerp(0.28, 1.0, saturate(height)) *
                    lerp(0.55, 1.0, densityOcclusion);
                float3 ambient =
                    (AmbientColorAnisotropy.xyz + skyLight * 0.60) *
                    SunColorAmbientIntensity.w * 0.45 *
                    max(Lighting.w, 0.0) * skyVisibility *
                    (1.0 - 0.15 * storm);

                if (lightingMode == 1u)
                    ambient = 0.0;
                else if (lightingMode == 2u)
                    sun = 0.0;

                const float sampleAtmosphereTransmittance = atmosphereEnabled
                    ? exp(-AtmosphereOpticalDepth(
                          CameraPosition(), rayDirection, t))
                    : 1.0;
                const float stepTransmittance = exp(-sigma * stepLength);
                const float weight =
                    transmittance * (1.0 - stepTransmittance);
                scattered += weight * (sun + ambient) *
                             sampleAtmosphereTransmittance;
                depthWeightSum += weight;
                depthWeightedDistance += weight * t;
                transmittance *= stepTransmittance;
            }

            if (transmittance < 0.005 && !lightingDebug)
                break;
        }
    }

    // V2.3D: aerial perspective replaces the old per-sample distanceFade and
    // the post-hoc horizonHaze lerp. Cloud radiance is attenuated by the
    // atmosphere between cloud and camera; the in-front haze is added using
    // Cloud-only transmittance before the atmosphere is applied. Used by the
    // A02b silhouette/optical-depth diagnostic.
    const float cloudTransmittance = transmittance;

    // the transmittance-weighted cloud distance.
    float atmosphereTransmittance = 1.0;
    if (atmosphereEnabled)
    {
        const float meanDistance = depthWeightSum > 1e-5
            ? depthWeightedDistance / depthWeightSum
            : 0.0;
        atmosphereTransmittance = exp(-AtmosphereOpticalDepth(
            CameraPosition(), rayDirection, meanDistance));
        scattered += AtmosphereColor(rayDirection) *
                     (1.0 - atmosphereTransmittance);
        transmittance *= atmosphereTransmittance;
    }

    // Guard against a driver producing an unexpected non-finite value.
    if (scattered.x != scattered.x || scattered.y != scattered.y ||
        scattered.z != scattered.z || scattered.x > 1e6 ||
        scattered.y > 1e6 || scattered.z > 1e6)
    {
        scattered = 0.0;
        transmittance = 1.0;
    }

    if (debugMode == 19)
    {
        // A02c linear diagnostic. The visible image keeps the displayable
        // monotonic map OD/(OD+1); the same shader also writes the raw linear
        // values into the Float32 probe buffer, bypassing the composite,
        // exposure, tone mapping, sRGB and 8-bit capture entirely.
        float od;
        if (ReferenceAudit.w > 0.5f && ReferenceAudit.w < 1.5f)
        {
            // Analytic test pattern: eight horizontal bands with known OD.
            const uint band = min(pixel.y * 8u / height, 7u);
            const float kOd[8] = { 0.0f, 0.05f, 0.10f, 0.25f,
                                   0.50f, 1.00f, 2.00f, 5.00f };
            od = kOd[band];
        }
        else
        {
            od = -log(max(cloudTransmittance, 1e-6));
        }
        const float mapped = od / (od + 1.0f);
#if OGSR_PROBE
        ProbeOutput[pixel.y * width + pixel.x] =
            float4(mapped, od, 0.0f, 1.0f);
#endif
        CloudOutput[pixel] = float4(mapped.xxx, 1.0);
    }
    else if (debugMode == 1)
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
    else if (debugMode == 8)
    {
        CloudOutput[pixel] = float4(debugOpticalDepth.xxx, 1.0);
    }
    else if (debugMode == 9)
    {
        // Gamma 0.25 is a display transform only; the selftest compares the
        // monotonic relationship, not this value.
        CloudOutput[pixel] =
            float4(pow(saturate(debugSunTransmittance), 0.25).xxx, 1.0);
    }
    else if (debugMode == 10)
    {
        CloudOutput[pixel] = float4(debugShadowDifference.xxx, 1.0);
    }
    else if (debugMode == 11)
    {
        CloudOutput[pixel] = float4(atmosphereTransmittance.xxx, 1.0);
    }
    else if (debugMode == 12)
    {
        CloudOutput[pixel] = float4(debugShadowPath.xxx, 1.0);
    }
    else
    {
        CloudOutput[pixel] = float4(scattered, saturate(transmittance));
    }
}
