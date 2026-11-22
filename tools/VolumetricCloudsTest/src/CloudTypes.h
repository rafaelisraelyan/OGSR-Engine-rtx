#pragma once

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace clouds
{
enum class RenderMode
{
    Slices,
    Clouds,
};

enum class ShapePrototype : uint32_t
{
    None = 0,
    A = 1, // deep convective: cumulus + storm
    B = 2, // layered deck: stratocumulus + overcast
};

enum class CloudFormula : uint32_t
{
    None = 0,
    F1 = 1, // Perlin-Worley primary shape
    F2 = 2, // per-type density blending
    F3 = 3, // coverage separation
    F4 = 4, // world-space XYZ noise
    F5 = 5, // controlled edge erosion
};

inline const char* CloudFormulaName(CloudFormula formula)
{
    switch (formula)
    {
    case CloudFormula::F1: return "F1";
    case CloudFormula::F2: return "F2";
    case CloudFormula::F3: return "F3";
    case CloudFormula::F4: return "F4";
    case CloudFormula::F5: return "F5";
    default: return "none";
    }
}

// V2.3h density models. Legacy is the accepted V2.3 renderer; Reference is
// the isolated natural-density experiment. The shader permutation and the PSO
// are selected explicitly, never by an implicit fallback.
enum class CloudDensityModel : uint32_t
{
    Legacy = 0,
    Reference = 1,
};

enum class ReferenceStage : uint32_t
{
    None = 0,
    Base = 1,   // H1: 3D Perlin-Worley shape only
    Height = 2, // H2: base shape * independent height profile
    Coverage = 3, // H3: single coverage remap
    Erosion = 4,  // H4: bounded Worley erosion
};

inline const char* DensityModelName(CloudDensityModel model)
{
    switch (model)
    {
    case CloudDensityModel::Reference: return "reference";
    default: return "legacy";
    }
}

inline const char* ReferenceStageName(ReferenceStage stage)
{
    switch (stage)
    {
    case ReferenceStage::Base: return "base";
    case ReferenceStage::Height: return "height";
    case ReferenceStage::Coverage: return "coverage";
    case ReferenceStage::Erosion: return "erosion";
    default: return "none";
    }
}

enum class ReferenceNoiseMix : uint32_t
{
    ROnly = 0,
    BOnly = 1,
    RPlusB = 2,
};

enum class ReferenceCoordinateMode : uint32_t
{
    Plain = 0,
    Warped = 1,
};

inline const char* ReferenceNoiseMixName(ReferenceNoiseMix mix)
{
    switch (mix)
    {
    case ReferenceNoiseMix::ROnly: return "r";
    case ReferenceNoiseMix::BOnly: return "b";
    default: return "rb";
    }
}

inline const char* ReferenceCoordinateModeName(ReferenceCoordinateMode mode)
{
    switch (mode)
    {
    case ReferenceCoordinateMode::Warped: return "warped";
    default: return "plain";
    }
}

// V2.3h-A02 base-field candidates. Direct is the A01 path; MultiFrequencyR
// combines two R samples at a non-integer scale ratio; LowFrequencyModulation
// adds a non-periodic world-space hash field to the R-only shape. A03 adds a
// non-tiling macro placement field; it does not reuse the NoiseVolume tile for
// the large masses.
enum class ReferenceFieldMode : uint32_t
{
    Direct = 0,
    MultiFrequencyR = 1,
    LowFrequencyModulation = 2,
    NonTilingMacro = 3,
};

inline const char* ReferenceFieldModeName(ReferenceFieldMode mode)
{
    switch (mode)
    {
    case ReferenceFieldMode::MultiFrequencyR: return "multi-r";
    case ReferenceFieldMode::LowFrequencyModulation: return "hash-mod";
    case ReferenceFieldMode::NonTilingMacro: return "macro";
    default: return "direct";
    }
}

// V2.3h-A01 diagnostic controls. These affect only the reference density
// model; legacy keeps its accepted V2.3 path. The values are explicit in the
// CLI, the frame constants, the capture log and the probe metadata.
struct ReferenceControls
{
    ReferenceNoiseMix noiseMix = ReferenceNoiseMix::RPlusB;
    ReferenceCoordinateMode coordinateMode = ReferenceCoordinateMode::Plain;
    ReferenceFieldMode fieldMode = ReferenceFieldMode::Direct;
    float shapeLow = 0.58f;
    float shapeHigh = 0.88f;
    float multiFrequencyScale = 0.73f;
    float multiFrequencyWeight = 0.25f;
    float modulationAmplitude = 0.10f;
    float densityScale = 1.0f;
    bool stormVolume = false;
    // A02c: corrected S2 column envelope. bottom/top become functions of
    // world XZ only (no sample-Y dependence); the old S2 stays available.
    bool stormColumnEnvelope = false;
    float stormHeightVariation = 0.0f;
    float stormBottomVariation = 0.0f;
    // A02c: analytic optical-depth test pattern for the Float32 readback.
    bool odTestPattern = false;
    // A03: non-tiling world-space macro placement field. The hash lattice has
    // no modulo; macro mass gates the periodic detail instead of modulating
    // an already placed cloud.
    float macroScale = 6000.0f;
    float macroSecondaryScale = 2500.0f;
    float macroThreshold = 0.42f;
    float macroSoftness = 0.10f;
    float macroDetailWeight = 0.45f;
    float macroSecondaryWeight = 0.35f;
    float macroEdgeJitter = 0.15f;
    float macroHeightWeight = 0.35f;
    uint32_t macroSeed = 1337u;
    // F01: analytic density test for shared-coordinate probe validation.
    // 0 = off, 1 = constant 0.5, 2 = vertical X stripe.
    uint32_t densityTestPattern = 0;
    // A03a diagnostics. Both default off so the A03 M1 field stays
    // byte-identical. macroCoveragePreserve applies the single coverage
    // remap to the macro mass before the periodic detail multiplier.
    // macroSeedHash replaces the 1/64-quantised A03 seed phase with a
    // full-precision 32-bit hash of macroSeed.
    bool macroCoveragePreserve = false;
    bool macroSeedHash = false;
    // A03a C3-1b: compensate the systematic mean of the detail multiplier
    // (mean detail transfer ~0.05 for this NoiseVolume). Without the gain the
    // multiplier averages 0.57 and the coverage cut removes whole columns.
    bool macroDetailCompensate = false;
    // A03a C3-2: use a coarse (48 km) macro octave as the local mean and
    // threshold the macro field at the coverage quantile. This keeps a local
    // 16 km view from saturating at 100% cloud when the low-frequency octave
    // happens to sit above the fixed 0.42 threshold.
    bool macroCoverageCalibrate = false;
    // F01 W1: weather occupancy before saturation. The continuous macro value
    // is thresholded at the coverage quantile; no post-smoothstep remap.
    bool macroWeatherQuantile = false;
    // F01 W2: local mean from four n1 taps (6 km spacing) as the occupancy
    // threshold centre. Removes the seed-dependent low-frequency saturation.
    bool macroWeatherLocal = false;
    // A03b: separate world-space local occupancy field (3.2/1.4 km) with a
    // fixed global quantile calibration. Weather coverage controls the
    // threshold before saturation; macro placement remains a weak bias.
    bool macroLocalOccupancy = false;
    // A03b Storm: keep the S2new backbone and use the local field only on
    // the low-density edges. The dense core is never gated, so the connected
    // storm front is preserved while the boundary gains 3D structure.
    bool stormLocalOccupancy = false;
    // A03a C4: in Storm the macro mass already defines the large-scale
    // placement. Keep the S2new envelope, but replace the redundant 5 km
    // storm gap instead of multiplying two independent sparse masks.
    bool stormMacroGapReplace = false;
    uint32_t noiseFieldAxis = 0; // 0 XY, 1 XZ, 2 YZ
    float noiseFieldOffset = 0.5f;
};

inline const char* ReferencePermutationName(
    const ReferenceControls& reference)
{
    if (reference.macroLocalOccupancy || reference.stormLocalOccupancy)
        return "A03b";
    const bool a03a = reference.macroCoveragePreserve ||
                      reference.macroSeedHash ||
                      reference.macroDetailCompensate ||
                      reference.stormMacroGapReplace ||
                      reference.macroCoverageCalibrate ||
                      reference.macroWeatherQuantile ||
                      reference.macroWeatherLocal ||
                      reference.densityTestPattern != 0u;
    return a03a ? "A03a" : "A03";
}

inline const char* ShapePrototypeName(ShapePrototype prototype)
{
    switch (prototype)
    {
    case ShapePrototype::A: return "A";
    case ShapePrototype::B: return "B";
    default: return "none";
    }
}

enum class CloudDebugMode : uint32_t
{
    Final = 0,
    Density = 1,
    Transmittance = 2,
    StepCount = 3,
    Occupancy = 4,
    Weather = 5,
    CloudType = 6,
    DensitySlice = 7,
    SunOpticalDepth = 8,
    SunTransmittance = 9,
    ShadowDensityDifference = 10,
    AtmosphereTransmittance = 11,
    ShadowPathLength = 12,
    DensityHorizontal = 13,
    RayProfile = 14,
    DensityVerticalZ = 15,
    MacroShape = 16,
    CrossTypeDifference = 17,
    NoiseField = 18,
    ViewOpticalDepth = 19,
};

inline const char* DebugModeName(CloudDebugMode mode)
{
    switch (mode)
    {
    case CloudDebugMode::Density: return "density";
    case CloudDebugMode::Transmittance: return "transmittance";
    case CloudDebugMode::StepCount: return "step-count";
    case CloudDebugMode::Occupancy: return "occupancy";
    case CloudDebugMode::Weather: return "weather";
    case CloudDebugMode::CloudType: return "cloud-type";
    case CloudDebugMode::DensitySlice: return "density-slice";
    case CloudDebugMode::SunOpticalDepth: return "sun-optical-depth";
    case CloudDebugMode::SunTransmittance: return "sun-transmittance";
    case CloudDebugMode::ShadowDensityDifference: return "shadow-density-diff";
    case CloudDebugMode::AtmosphereTransmittance: return "atmosphere-transmittance";
    case CloudDebugMode::ShadowPathLength: return "shadow-path-length";
    case CloudDebugMode::DensityHorizontal: return "density-horizontal";
    case CloudDebugMode::RayProfile: return "ray-profile";
    case CloudDebugMode::DensityVerticalZ: return "density-vertical-z";
    case CloudDebugMode::MacroShape: return "macro-shape";
    case CloudDebugMode::CrossTypeDifference: return "cross-type-difference";
    case CloudDebugMode::NoiseField: return "noise-field";
    case CloudDebugMode::ViewOpticalDepth: return "view-optical-depth";
    default: return "final";
    }
}

// V2.3 lighting isolation used by the visual A/B matrix. Full is the
// production path; SunOnly/AmbientOnly exist to prove which term owns which
// part of the image without changing cloud geometry.
enum class CloudLightingMode : uint32_t
{
    Full = 0,
    SunOnly = 1,
    AmbientOnly = 2,
};

inline const char* LightingModeName(CloudLightingMode mode)
{
    switch (mode)
    {
    case CloudLightingMode::SunOnly: return "sun-only";
    case CloudLightingMode::AmbientOnly: return "ambient-only";
    default: return "full";
    }
}

// V2.3E. Spatial is the deterministic per-pixel offset used by V2.2b; Off
// samples ray centers and is the reference for temporal comparisons.
enum class CloudJitterMode : uint32_t
{
    Off = 0,
    Spatial = 1,
};

inline const char* JitterModeName(CloudJitterMode mode)
{
    return mode == CloudJitterMode::Off ? "off" : "spatial";
}

// Test/profiling-only switches. They are never used by the visual presets; the
// default None path is the production path. Values are passed through
// WeatherC.z/w to CloudRayMarch.hlsl.
enum class CloudAblationMode : uint32_t
{
    None = 0,
    NoWeather = 1,
    NoMid = 2,
    NoBase = 3,
    NoFine = 4,
    Shadow2 = 5,
    Shadow4 = 6,
    NoType = 7,
    NoWarp = 8,
    MacroOnly = 9,
    NoShadow = 10,
    Base3D = 13,
    NoBaseWarp = 14,
    NoHeightVariation = 15,
    NoAnisotropy = 16,
    FlatBottom = 17,
    NoTypeHeightProfile = 18,
    NoTypeThresholds = 19,
    NoTypeAnisotropy = 20,
    NoTypeLocalBounds = 21,
    NoTypeShapeBlending = 22,
};

inline const char* AblationModeName(CloudAblationMode mode)
{
    switch (mode)
    {
    case CloudAblationMode::NoWeather: return "no-weather";
    case CloudAblationMode::NoMid: return "no-mid";
    case CloudAblationMode::NoBase: return "no-base";
    case CloudAblationMode::NoFine: return "no-fine";
    case CloudAblationMode::Shadow2: return "shadow-2";
    case CloudAblationMode::Shadow4: return "shadow-4";
    case CloudAblationMode::NoType: return "no-type";
    case CloudAblationMode::NoWarp: return "no-warp";
    case CloudAblationMode::MacroOnly: return "macro-only";
    case CloudAblationMode::NoShadow: return "no-shadow";
    case CloudAblationMode::Base3D: return "base-3d";
    case CloudAblationMode::NoBaseWarp: return "no-base-warp";
    case CloudAblationMode::NoHeightVariation: return "no-height-variation";
    case CloudAblationMode::NoAnisotropy: return "no-anisotropy";
    case CloudAblationMode::FlatBottom: return "flat-bottom";
    case CloudAblationMode::NoTypeHeightProfile: return "no-type-height-profile";
    case CloudAblationMode::NoTypeThresholds: return "no-type-thresholds";
    case CloudAblationMode::NoTypeAnisotropy: return "no-type-anisotropy";
    case CloudAblationMode::NoTypeLocalBounds: return "no-type-local-bounds";
    case CloudAblationMode::NoTypeShapeBlending: return "no-type-shape-blending";
    default: return "none";
    }
}

struct Camera
{
    DirectX::XMFLOAT3 position{ 0.0f, 500.0f, -1200.0f };
    float yawDegrees = 0.0f;
    float pitchDegrees = 9.0f;
    float fovYDegrees = 62.0f;
    float nearPlane = 1.0f;
    float farPlane = 30000.0f;

    void Reset()
    {
        position = DirectX::XMFLOAT3(0.0f, 500.0f, -1200.0f);
        yawDegrees = 0.0f;
        pitchDegrees = 9.0f;
        fovYDegrees = 62.0f;
    }

    DirectX::XMFLOAT3 Forward() const
    {
        const float yaw = DirectX::XMConvertToRadians(yawDegrees);
        const float pitch = DirectX::XMConvertToRadians(pitchDegrees);
        const float cp = std::cos(pitch);
        return DirectX::XMFLOAT3(std::sin(yaw) * cp, std::sin(pitch),
                                 std::cos(yaw) * cp);
    }

    DirectX::XMFLOAT3 Right() const
    {
        const float yaw = DirectX::XMConvertToRadians(yawDegrees);
        return DirectX::XMFLOAT3(std::cos(yaw), 0.0f, -std::sin(yaw));
    }

    DirectX::XMFLOAT3 Up() const
    {
        const DirectX::XMFLOAT3 f = Forward();
        const DirectX::XMFLOAT3 r = Right();
        return DirectX::XMFLOAT3(
            f.y * r.z - f.z * r.y,
            f.z * r.x - f.x * r.z,
            f.x * r.y - f.y * r.x);
    }

    void Rotate(float dxPixels, float dyPixels, float sensitivity = 0.12f)
    {
        yawDegrees += dxPixels * sensitivity;
        pitchDegrees = std::clamp(pitchDegrees - dyPixels * sensitivity,
                                  -85.0f, 85.0f);
        if (yawDegrees > 360.0f || yawDegrees < -360.0f)
            yawDegrees = std::fmod(yawDegrees, 360.0f);
    }

    void Move(float forward, float right, float up, float distance)
    {
        const DirectX::XMFLOAT3 f = Forward();
        const DirectX::XMFLOAT3 r = Right();
        position.x += (f.x * forward + r.x * right) * distance;
        position.y += (f.y * forward + up) * distance;
        position.z += (f.z * forward + r.z * right) * distance;
    }
};

struct CloudParams
{
    float layerBottom = 600.0f;
    float layerTop = 1900.0f;
    float coverage = 0.58f;
    float density = 0.92f;
    float extinction = 0.020f;
    float anisotropy = 0.60f;
    float noiseScale = 0.00020f;
    float verticalNoiseScale = 1.20f;
    float densitySliceHeight = 0.50f;
    float densitySliceExtentKm = 2.0f;
    float probeColumn = 0.50f;
    float topFade = 0.06f;
    float formulaBias = 0.00f;

    float windSpeed = 24.0f;
    DirectX::XMFLOAT3 windDirection{ 1.0f, 0.0f, 0.22f };
    float sunAzimuthDegrees = 42.0f;
    float sunElevationDegrees = 34.0f;
    DirectX::XMFLOAT3 sunColor{ 1.00f, 0.93f, 0.82f };
    float sunIntensity = 8.0f;
    DirectX::XMFLOAT3 ambientColor{ 0.30f, 0.39f, 0.50f };
    float ambientIntensity = 0.22f;
    DirectX::XMFLOAT3 skyZenith{ 0.10f, 0.18f, 0.30f };
    DirectX::XMFLOAT3 skyHorizon{ 0.45f, 0.52f, 0.60f };
    float exposure = 1.0f;
    float maxDistance = 14000.0f;
    uint32_t stepCount = 128;

    // V2.3 lighting controls. These are part of the reproducible scene
    // contract: captures record the same values in the log line.
    uint32_t shadowStepCount = 6;
    float sunShadowDistance = 6000.0f;
    float multiScatterStrength = 1.0f;
    float ambientScale = 1.0f;
    bool atmosphereEnabled = true;
    float atmosphereExtinction = 0.00010f;
    float atmosphereHeightFalloff = 6000.0f;
    bool selfShadowEnabled = true;
    CloudLightingMode lightingMode = CloudLightingMode::Full;
    CloudJitterMode jitterMode = CloudJitterMode::Spatial;

    // V2 weather map contract. The map itself is world-space and is sampled
    // after the wind offset, so wind translation invariance is preserved.
    float weatherCoverageBias = 0.0f;
    float weatherTypeBias = 0.0f;
    float weatherDensityBias = 0.0f;
    float weatherStorminess = 0.0f;
    float weatherFrontOffset = 100000.0f;
    float weatherFrontWidth = 4000.0f;
    float weatherFrontStrength = 0.0f;
    float weatherHeightVariation = 100.0f;
    float weatherMapWorldSize = 4096.0f;
    // NOT IMPLEMENTED in V2.2. Reserved for the V2.4 weather timeline; it is
    // intentionally kept at zero and must not be presented as working motion.
    float weatherEvolution = 0.0f;
    CloudAblationMode ablationMode = CloudAblationMode::None;
};

inline DirectX::XMFLOAT3 Normalize(const DirectX::XMFLOAT3& value)
{
    const float length = std::sqrt(value.x * value.x + value.y * value.y +
                                   value.z * value.z);
    if (length <= 1e-8f)
        return DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f);
    return DirectX::XMFLOAT3(value.x / length, value.y / length,
                             value.z / length);
}

inline DirectX::XMFLOAT3 SunDirection(const CloudParams& params)
{
    const float azimuth = DirectX::XMConvertToRadians(params.sunAzimuthDegrees);
    const float elevation =
        DirectX::XMConvertToRadians(params.sunElevationDegrees);
    const float ce = std::cos(elevation);
    return DirectX::XMFLOAT3(std::sin(azimuth) * ce, std::sin(elevation),
                             std::cos(azimuth) * ce);
}

inline void ApplyClearPreset(CloudParams& params)
{
    params = CloudParams{};
    params.layerBottom = 700.0f;
    params.layerTop = 1900.0f;
    params.coverage = 0.42f;
    params.density = 0.90f;
    params.extinction = 0.026f;
    params.anisotropy = 0.62f;
    params.noiseScale = 0.00090f;
    params.verticalNoiseScale = 1.20f;
    params.sunAzimuthDegrees = 42.0f;
    params.sunElevationDegrees = 34.0f;
    params.sunColor = DirectX::XMFLOAT3(1.00f, 0.90f, 0.76f);
    params.sunIntensity = 8.5f;
    params.ambientColor = DirectX::XMFLOAT3(0.32f, 0.43f, 0.56f);
    params.ambientIntensity = 0.42f;
    params.skyZenith = DirectX::XMFLOAT3(0.08f, 0.17f, 0.31f);
    params.skyHorizon = DirectX::XMFLOAT3(0.42f, 0.50f, 0.58f);
    params.exposure = 1.10f;
    params.stepCount = 128;
    params.weatherCoverageBias = -0.22f;
    params.weatherTypeBias = -0.30f;
    params.weatherDensityBias = -0.03f;
    params.weatherStorminess = 0.0f;
    params.weatherHeightVariation = 90.0f;
}

inline void ApplyPartlyPreset(CloudParams& params)
{
    params = CloudParams{};
    params.layerBottom = 650.0f;
    params.layerTop = 2050.0f;
    params.coverage = 0.62f;
    params.density = 0.90f;
    params.extinction = 0.026f;
    params.anisotropy = 0.60f;
    params.noiseScale = 0.00080f;
    params.verticalNoiseScale = 1.20f;
    params.sunAzimuthDegrees = 35.0f;
    params.sunElevationDegrees = 32.0f;
    params.sunColor = DirectX::XMFLOAT3(1.00f, 0.91f, 0.78f);
    params.sunIntensity = 7.5f;
    params.ambientColor = DirectX::XMFLOAT3(0.31f, 0.41f, 0.54f);
    params.ambientIntensity = 0.38f;
    params.skyZenith = DirectX::XMFLOAT3(0.09f, 0.18f, 0.32f);
    params.skyHorizon = DirectX::XMFLOAT3(0.43f, 0.50f, 0.57f);
    params.exposure = 1.05f;
    params.stepCount = 128;
    params.weatherCoverageBias = -0.05f;
    params.weatherTypeBias = -0.02f;
    params.weatherDensityBias = 0.02f;
    params.weatherStorminess = 0.12f;
    params.weatherFrontOffset = 8000.0f;
    params.weatherFrontWidth = 5000.0f;
    params.weatherFrontStrength = 0.05f;
    params.weatherHeightVariation = 120.0f;
}

inline void ApplyOvercastPreset(CloudParams& params)
{
    params = CloudParams{};
    params.layerBottom = 500.0f;
    params.layerTop = 2000.0f;
    params.coverage = 0.68f;
    params.density = 0.80f;
    params.extinction = 0.024f;
    params.anisotropy = 0.42f;
    params.noiseScale = 0.00060f;
    params.verticalNoiseScale = 2.00f;
    params.sunAzimuthDegrees = 65.0f;
    params.sunElevationDegrees = 25.0f;
    params.sunColor = DirectX::XMFLOAT3(0.78f, 0.82f, 0.88f);
    params.sunIntensity = 4.0f;
    params.ambientColor = DirectX::XMFLOAT3(0.32f, 0.36f, 0.42f);
    params.ambientIntensity = 0.78f;
    params.skyZenith = DirectX::XMFLOAT3(0.15f, 0.18f, 0.22f);
    params.skyHorizon = DirectX::XMFLOAT3(0.30f, 0.33f, 0.37f);
    params.exposure = 1.12f;
    params.stepCount = 128;
    params.weatherCoverageBias = 0.02f;
    params.weatherTypeBias = 0.60f;
    params.weatherDensityBias = 0.08f;
    params.weatherStorminess = 0.35f;
    params.weatherFrontOffset = 7000.0f;
    params.weatherFrontWidth = 6000.0f;
    params.weatherFrontStrength = 0.10f;
    params.weatherHeightVariation = 400.0f;
}

inline void ApplyRainPreset(CloudParams& params)
{
    params = CloudParams{};
    params.layerBottom = 450.0f;
    params.layerTop = 2150.0f;
    params.coverage = 0.74f;
    params.density = 1.00f;
    params.extinction = 0.028f;
    params.anisotropy = 0.35f;
    params.noiseScale = 0.00040f;
    params.verticalNoiseScale = 2.20f;
    params.sunAzimuthDegrees = 65.0f;
    params.sunElevationDegrees = 18.0f;
    params.sunColor = DirectX::XMFLOAT3(0.62f, 0.68f, 0.76f);
    params.sunIntensity = 2.6f;
    params.ambientColor = DirectX::XMFLOAT3(0.27f, 0.31f, 0.37f);
    params.ambientIntensity = 0.78f;
    params.skyZenith = DirectX::XMFLOAT3(0.12f, 0.15f, 0.19f);
    params.skyHorizon = DirectX::XMFLOAT3(0.26f, 0.29f, 0.33f);
    params.exposure = 1.0f;
    params.stepCount = 128;
    params.weatherCoverageBias = 0.04f;
    params.weatherTypeBias = 0.64f;
    params.weatherDensityBias = 0.14f;
    params.weatherStorminess = 0.80f;
    params.weatherFrontOffset = 6000.0f;
    params.weatherFrontWidth = 5000.0f;
    params.weatherFrontStrength = 0.85f;
    params.weatherHeightVariation = 460.0f;
}

inline void ApplyStormPreset(CloudParams& params)
{
    params = CloudParams{};
    params.layerBottom = 400.0f;
    params.layerTop = 2300.0f;
    params.coverage = 0.80f;
    params.density = 1.20f;
    params.extinction = 0.030f;
    params.anisotropy = 0.30f;
    params.noiseScale = 0.00035f;
    params.verticalNoiseScale = 2.20f;
    params.sunAzimuthDegrees = 55.0f;
    params.sunElevationDegrees = 15.0f;
    params.sunColor = DirectX::XMFLOAT3(0.58f, 0.65f, 0.74f);
    params.sunIntensity = 2.2f;
    params.ambientColor = DirectX::XMFLOAT3(0.25f, 0.29f, 0.35f);
    params.ambientIntensity = 0.72f;
    params.skyZenith = DirectX::XMFLOAT3(0.10f, 0.13f, 0.17f);
    params.skyHorizon = DirectX::XMFLOAT3(0.22f, 0.25f, 0.29f);
    params.exposure = 1.0f;
    params.stepCount = 128;
    params.weatherCoverageBias = 0.10f;
    params.weatherTypeBias = 0.70f;
    params.weatherDensityBias = 0.18f;
    params.weatherStorminess = 1.0f;
    params.weatherFrontOffset = 3000.0f;
    params.weatherFrontWidth = 6500.0f;
    params.weatherFrontStrength = 1.0f;
    params.weatherHeightVariation = 520.0f;
}

inline void ApplySunsetPreset(CloudParams& params)
{
    params = CloudParams{};
    params.layerBottom = 650.0f;
    params.layerTop = 2000.0f;
    params.coverage = 0.54f;
    params.density = 0.90f;
    params.extinction = 0.021f;
    params.anisotropy = 0.68f;
    params.noiseScale = 0.00040f;
    params.verticalNoiseScale = 1.20f;
    params.sunAzimuthDegrees = 282.0f;
    params.sunElevationDegrees = 5.0f;
    params.sunColor = DirectX::XMFLOAT3(1.00f, 0.55f, 0.30f);
    params.sunIntensity = 5.5f;
    params.ambientColor = DirectX::XMFLOAT3(0.26f, 0.30f, 0.42f);
    params.ambientIntensity = 0.25f;
    params.skyZenith = DirectX::XMFLOAT3(0.12f, 0.16f, 0.28f);
    params.skyHorizon = DirectX::XMFLOAT3(0.48f, 0.38f, 0.32f);
    params.exposure = 1.0f;
    params.stepCount = 128;
    params.weatherCoverageBias = -0.05f;
    params.weatherTypeBias = 0.0f;
    params.weatherDensityBias = 0.0f;
    params.weatherStorminess = 0.05f;
    params.weatherHeightVariation = 110.0f;
}

inline bool ApplyPreset(const std::string& name, CloudParams& params)
{
    if (name == "clear")
    {
        ApplyClearPreset(params);
        return true;
    }
    if (name == "partly")
    {
        ApplyPartlyPreset(params);
        return true;
    }
    if (name == "overcast")
    {
        ApplyOvercastPreset(params);
        return true;
    }
    if (name == "rain")
    {
        ApplyRainPreset(params);
        return true;
    }
    if (name == "storm")
    {
        ApplyStormPreset(params);
        return true;
    }
    if (name == "sunset")
    {
        ApplySunsetPreset(params);
        return true;
    }
    return false;
}

inline CloudParams LerpCloudParams(const CloudParams& a, const CloudParams& b,
                                   float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const auto lerp = [t](float x, float y) { return x + (y - x) * t; };
    const auto lerp3 = [t](const DirectX::XMFLOAT3& x,
                           const DirectX::XMFLOAT3& y)
    {
        return DirectX::XMFLOAT3(x.x + (y.x - x.x) * t,
                                 x.y + (y.y - x.y) * t,
                                 x.z + (y.z - x.z) * t);
    };

    CloudParams result = a;
    result.layerBottom = lerp(a.layerBottom, b.layerBottom);
    result.layerTop = lerp(a.layerTop, b.layerTop);
    result.coverage = lerp(a.coverage, b.coverage);
    result.density = lerp(a.density, b.density);
    result.extinction = lerp(a.extinction, b.extinction);
    result.anisotropy = lerp(a.anisotropy, b.anisotropy);
    result.noiseScale = lerp(a.noiseScale, b.noiseScale);
    result.verticalNoiseScale =
        lerp(a.verticalNoiseScale, b.verticalNoiseScale);
    result.windSpeed = lerp(a.windSpeed, b.windSpeed);
    result.windDirection = lerp3(a.windDirection, b.windDirection);
    result.sunAzimuthDegrees =
        lerp(a.sunAzimuthDegrees, b.sunAzimuthDegrees);
    result.sunElevationDegrees =
        lerp(a.sunElevationDegrees, b.sunElevationDegrees);
    result.sunColor = lerp3(a.sunColor, b.sunColor);
    result.sunIntensity = lerp(a.sunIntensity, b.sunIntensity);
    result.ambientColor = lerp3(a.ambientColor, b.ambientColor);
    result.ambientIntensity = lerp(a.ambientIntensity, b.ambientIntensity);
    result.skyZenith = lerp3(a.skyZenith, b.skyZenith);
    result.skyHorizon = lerp3(a.skyHorizon, b.skyHorizon);
    result.exposure = lerp(a.exposure, b.exposure);
    result.maxDistance = lerp(a.maxDistance, b.maxDistance);
    result.weatherCoverageBias =
        lerp(a.weatherCoverageBias, b.weatherCoverageBias);
    result.weatherTypeBias = lerp(a.weatherTypeBias, b.weatherTypeBias);
    result.weatherDensityBias =
        lerp(a.weatherDensityBias, b.weatherDensityBias);
    result.weatherStorminess =
        lerp(a.weatherStorminess, b.weatherStorminess);
    result.weatherFrontOffset =
        lerp(a.weatherFrontOffset, b.weatherFrontOffset);
    result.weatherFrontWidth =
        lerp(a.weatherFrontWidth, b.weatherFrontWidth);
    result.weatherFrontStrength =
        lerp(a.weatherFrontStrength, b.weatherFrontStrength);
    result.weatherHeightVariation =
        lerp(a.weatherHeightVariation, b.weatherHeightVariation);
    result.weatherMapWorldSize =
        lerp(a.weatherMapWorldSize, b.weatherMapWorldSize);
    result.weatherEvolution = lerp(a.weatherEvolution, b.weatherEvolution);
    result.stepCount = static_cast<uint32_t>(
        std::lround(lerp(static_cast<float>(a.stepCount),
                         static_cast<float>(b.stepCount))));
    return result;
}

// Wind is a horizontal world-space displacement. The shader receives the
// offset separately from the inverse world scale and subtracts it before
// converting world XZ to noise coordinates:
//   noiseXZ = (worldXZ - windOffsetWorld) * noiseScale
// This keeps the units explicit and preserves the wind direction sign.
inline DirectX::XMFLOAT3 WindOffsetWorld(const CloudParams& params,
                                         float seconds)
{
    const DirectX::XMFLOAT3 direction = Normalize(params.windDirection);
    const float distance = seconds * params.windSpeed;
    return DirectX::XMFLOAT3(direction.x * distance, 0.0f,
                             direction.z * distance);
}

// One constant buffer shared by CloudRayMarch.hlsl and CloudComposite.hlsl.
// Every field is a float4 so that C++ and HLSL packing cannot drift.
struct alignas(16) CloudFrameConstants
{
    // xyz camera position, w time in seconds
    DirectX::XMFLOAT4 cameraPositionTime{};
    // xyz camera right, w vertical FOV in radians
    DirectX::XMFLOAT4 cameraRightFov{};
    // xyz camera up, w aspect ratio
    DirectX::XMFLOAT4 cameraUpAspect{};
    // xyz camera forward, w layer bottom
    DirectX::XMFLOAT4 cameraForwardLayerBottom{};
    // x layer top, y coverage, z density multiplier, w extinction
    DirectX::XMFLOAT4 layerShape{};
    // xyz sun direction (towards sun), w sun intensity
    DirectX::XMFLOAT4 sunDirectionIntensity{};
    // xyz sun color, w ambient intensity
    DirectX::XMFLOAT4 sunColorAmbientIntensity{};
    // xyz ambient color, w Henyey-Greenstein anisotropy
    DirectX::XMFLOAT4 ambientColorAnisotropy{};
    // xyz wind offset in world units subtracted from world XZ, w inverse
    // world scale used to convert world XZ to noise coordinates
    DirectX::XMFLOAT4 windNoiseScale{};
    // x vertical noise scale, y max distance, z step count, w debug mode
    DirectX::XMFLOAT4 shaping{};
    // x output width, y output height, z exposure, w noise resolution
    DirectX::XMFLOAT4 output{};
    // xyz zenith color, w unused
    DirectX::XMFLOAT4 skyZenith{};
    // xyz horizon color, w unused
    DirectX::XMFLOAT4 skyHorizon{};
    // x coverage bias, y type bias, z density bias, w storminess
    DirectX::XMFLOAT4 weatherA{};
    // x inverse weather world size, y front offset (world units),
    // z front width (world units), w front strength
    DirectX::XMFLOAT4 weatherB{};
    // x height variation, y evolution phase, z volume margin, w ablation
    DirectX::XMFLOAT4 weatherC{};
    // x sun shadow distance cap (m), y shadow steps, z multiple-scatter
    // strength, w ambient scale
    DirectX::XMFLOAT4 lighting{};
    // x atmosphere enabled, y extinction (1/m), z height falloff (m),
    // w jitter mode (0 off, 1 spatial)
    DirectX::XMFLOAT4 atmosphere{};
    // x lighting mode (0 full, 1 sun-only, 2 ambient-only), y self-shadow
    // enabled (1) / disabled (0), z bounded 3D surface warp amplitude,
    // w bounded medium-frequency boundary erosion strength
    DirectX::XMFLOAT4 lightingFlags{};
    // x probe column, y slice extent km, z top fade width, w slice height
    DirectX::XMFLOAT4 morphology{};
    // V2.3h-A01 x noise mix (0 R, 1 B, 2 R+B), y coordinate mode (0 plain,
    // 1 warped), z shape transfer low, w shape transfer high
    DirectX::XMFLOAT4 referenceShape{};
    // x noise-field axis (0 XY, 1 XZ, 2 YZ), y noise-field offset 0..1,
    // z/w seed-derived phase offsets for A02 candidates
    DirectX::XMFLOAT4 referenceDebug{};
    // V2.3h-A02 x field mode, y multi-frequency scale, z multi-frequency
    // weight, w low-frequency modulation amplitude
    DirectX::XMFLOAT4 referenceField{};
    // V2.3h-A02b x density scale, y storm top variation (m),
    // z storm bottom variation (m), w storm volume flag (0 off, 1 3D S2,
    // 2 A02c corrected XZ-column S2)
    DirectX::XMFLOAT4 referenceStorm{};
    // V2.3h-A02c volume probe: xyz world origin of the cell-centre grid,
    // w uniform cell size in metres.
    DirectX::XMFLOAT4 referenceVolume{};
    // xyz grid resolution, w probe mode (0 volume, 1 boundary columns)
    DirectX::XMFLOAT4 referenceGrid{};
    // x/y boundary sample heights, z/w reserved diagnostic flags
    DirectX::XMFLOAT4 referenceAudit{};
    // V2.3h-A03 macro field: x scale1 (m), y scale2 (m), z threshold,
    // w softness
    DirectX::XMFLOAT4 referenceMacro{};
    // x detail weight, y secondary weight, z edge jitter, w seed phase
    DirectX::XMFLOAT4 referenceMacro2{};
    // x macro height weight, y A03a flags (bit0 coverage preserve, bit1 seed
    // hash), z/w reserved
    DirectX::XMFLOAT4 referenceMacro3{};
    // xyz full-precision macro seed phase (used only with bit1), w reserved
    DirectX::XMFLOAT4 referenceMacro4{};
};
static_assert(sizeof(CloudFrameConstants) == 31 * 16,
              "CloudFrameConstants must remain 31 float4 rows");
static_assert(sizeof(CloudFrameConstants) % 16 == 0,
              "constant buffer alignment");

// A03a: inverse normal CDF used by the coverage calibration. Acklam's
// rational approximation, |error| < 1.15e-9 over (0,1).
inline double NormalQuantile(double p)
{
    static const double a[6] = {
        -3.969683028665376e+01, 2.209460984245205e+02,
        -2.759285104469687e+02, 1.383577518672690e+02,
        -3.066479806614716e+01, 2.506628277459239e+00 };
    static const double b[5] = {
        -5.447609879822406e+01, 1.615858368580409e+02,
        -1.556989798598866e+02, 6.680131188771972e+01,
        -1.328068155288572e+01 };
    static const double c[6] = {
        -7.784894002430293e-03, -3.223964580411365e-01,
        -2.400758277161838e+00, -2.549732539343734e+00,
        4.374664141464968e+00, 2.938163982698783e+00 };
    static const double d[4] = {
        7.784695709041462e-03, 3.224671290700398e-01,
        2.445134137142996e+00, 3.754408661907416e+00 };
    const double p_low = 0.02425;
    double q;
    double r;
    if (p <= 0.0)
        return -1.0e9;
    if (p >= 1.0)
        return 1.0e9;
    if (p < p_low)
    {
        q = std::sqrt(-2.0 * std::log(p));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q +
                 c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }
    if (p <= 1.0 - p_low)
    {
        q = p - 0.5;
        r = q * q;
        return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r +
                 a[4]) * r + a[5]) * q /
               (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r +
                 b[4]) * r + 1.0);
    }
    q = std::sqrt(-2.0 * std::log(1.0 - p));
    return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q +
              c[4]) * q + c[5]) /
           ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
}

inline CloudFrameConstants BuildCloudConstants(
    const Camera& camera,
    const CloudParams& params,
    uint32_t outputWidth,
    uint32_t outputHeight,
    float timeSeconds,
    CloudDebugMode debugMode,
    CloudDensityModel densityModel = CloudDensityModel::Legacy,
    ReferenceStage referenceStage = ReferenceStage::Height,
    const ReferenceControls& reference = ReferenceControls{},
    uint32_t noiseSeed = 1337)
{
    CloudFrameConstants constants{};

    const DirectX::XMFLOAT3 forward = camera.Forward();
    const DirectX::XMFLOAT3 right = camera.Right();
    const DirectX::XMFLOAT3 up = camera.Up();
    const DirectX::XMFLOAT3 sun = SunDirection(params);
    const DirectX::XMFLOAT3 windOffset = WindOffsetWorld(params, timeSeconds);
    const float aspect = outputHeight > 0
        ? static_cast<float>(outputWidth) / static_cast<float>(outputHeight)
        : 1.0f;

    constants.cameraPositionTime =
        DirectX::XMFLOAT4(camera.position.x, camera.position.y,
                          camera.position.z, timeSeconds);
    constants.cameraRightFov =
        DirectX::XMFLOAT4(right.x, right.y, right.z,
                          DirectX::XMConvertToRadians(camera.fovYDegrees));
    constants.cameraUpAspect =
        DirectX::XMFLOAT4(up.x, up.y, up.z, aspect);
    constants.cameraForwardLayerBottom =
        DirectX::XMFLOAT4(forward.x, forward.y, forward.z, params.layerBottom);
    constants.layerShape =
        DirectX::XMFLOAT4(params.layerTop, params.coverage, params.density,
                          params.extinction);
    constants.sunDirectionIntensity =
        DirectX::XMFLOAT4(sun.x, sun.y, sun.z, params.sunIntensity);
    constants.sunColorAmbientIntensity =
        DirectX::XMFLOAT4(params.sunColor.x, params.sunColor.y,
                          params.sunColor.z, params.ambientIntensity);
    constants.ambientColorAnisotropy =
        DirectX::XMFLOAT4(params.ambientColor.x, params.ambientColor.y,
                          params.ambientColor.z, params.anisotropy);
    constants.windNoiseScale =
        DirectX::XMFLOAT4(windOffset.x, windOffset.y, windOffset.z,
                          params.noiseScale);
    constants.shaping =
        DirectX::XMFLOAT4(params.verticalNoiseScale, params.maxDistance,
                          static_cast<float>(params.stepCount),
                          static_cast<float>(static_cast<uint32_t>(debugMode)));
    constants.output =
        DirectX::XMFLOAT4(static_cast<float>(outputWidth),
                          static_cast<float>(outputHeight), params.exposure,
                          static_cast<float>(camera.farPlane));
    constants.skyZenith =
        DirectX::XMFLOAT4(params.skyZenith.x, params.skyZenith.y,
                          params.skyZenith.z, 0.0f);
    constants.skyHorizon =
        DirectX::XMFLOAT4(params.skyHorizon.x, params.skyHorizon.y,
                          params.skyHorizon.z, 0.0f);
    constants.weatherA =
        DirectX::XMFLOAT4(params.weatherCoverageBias, params.weatherTypeBias,
                          params.weatherDensityBias, params.weatherStorminess);
    const float weatherWorldSize = std::max(params.weatherMapWorldSize, 1024.0f);
    constants.weatherB =
        DirectX::XMFLOAT4(1.0f / weatherWorldSize, params.weatherFrontOffset,
                          params.weatherFrontWidth, params.weatherFrontStrength);
    // Conservative bounds for local height warp: half of the weather-driven
    // bottom shift plus the maximum base-noise warp. IntersectLayer uses this
    // margin so a local boundary is never clipped by the nominal slab.
    const float layerThickness = std::max(params.layerTop - params.layerBottom,
                                          1.0f);
    // Reference H1/H2 has no local height warp and uses the exact layer
    // bounds; Storm S2 extends the envelope and needs a matching margin.
    const float referenceMargin = reference.stormVolume
        ? reference.stormHeightVariation + reference.stormBottomVariation +
              100.0f
        : 0.0f;
    const float volumeMargin = densityModel == CloudDensityModel::Reference
        ? referenceMargin
        : (params.weatherHeightVariation * 0.5f + layerThickness * 0.35f);
    constants.weatherC =
        DirectX::XMFLOAT4(params.weatherHeightVariation,
                          params.weatherEvolution * timeSeconds,
                          volumeMargin,
                          static_cast<float>(
                              static_cast<uint32_t>(params.ablationMode)));
    constants.lighting = DirectX::XMFLOAT4(
        std::max(params.sunShadowDistance, 100.0f),
        static_cast<float>(std::clamp<uint32_t>(params.shadowStepCount, 1u,
                                                16u)),
        std::max(params.multiScatterStrength, 0.0f),
        std::max(params.ambientScale, 0.0f));
    constants.atmosphere = DirectX::XMFLOAT4(
        params.atmosphereEnabled ? 1.0f : 0.0f,
        std::max(params.atmosphereExtinction, 0.0f),
        std::max(params.atmosphereHeightFalloff, 1.0f),
        static_cast<float>(static_cast<uint32_t>(params.jitterMode)));
    constants.lightingFlags = DirectX::XMFLOAT4(
        static_cast<float>(static_cast<uint32_t>(params.lightingMode)),
        params.selfShadowEnabled ? 1.0f : 0.0f,
        std::clamp(params.formulaBias, -1.0f, 1.0f),
        densityModel == CloudDensityModel::Reference
            ? static_cast<float>(static_cast<uint32_t>(referenceStage))
            : 0.0f);
    constants.morphology = DirectX::XMFLOAT4(
        std::clamp(params.probeColumn, 0.0f, 1.0f),
        std::max(params.densitySliceExtentKm, 2.0f),
        std::clamp(params.topFade, 0.02f, 0.30f),
        std::clamp(params.densitySliceHeight, 0.0f, 1.0f));
    const float shapeLow = std::clamp(reference.shapeLow, 0.0f, 0.98f);
    const float shapeHigh =
        std::clamp(reference.shapeHigh, shapeLow + 0.01f, 1.5f);
    constants.referenceShape = DirectX::XMFLOAT4(
        static_cast<float>(static_cast<uint32_t>(reference.noiseMix)),
        static_cast<float>(
            static_cast<uint32_t>(reference.coordinateMode)),
        shapeLow, shapeHigh);
    const float seedPhaseX =
        static_cast<float>(noiseSeed & 1023u) / 1023.0f;
    const float seedPhaseY =
        static_cast<float>((noiseSeed >> 10) & 1023u) / 1023.0f;
    constants.referenceDebug = DirectX::XMFLOAT4(
        static_cast<float>(reference.noiseFieldAxis % 3u),
        std::clamp(reference.noiseFieldOffset, 0.0f, 1.0f),
        seedPhaseX, seedPhaseY);
    constants.referenceField = DirectX::XMFLOAT4(
        static_cast<float>(static_cast<uint32_t>(reference.fieldMode)),
        std::clamp(reference.multiFrequencyScale, 0.10f, 4.0f),
        std::clamp(reference.multiFrequencyWeight, 0.0f, 1.0f),
        std::clamp(reference.modulationAmplitude, 0.0f, 0.50f));
    constants.referenceStorm = DirectX::XMFLOAT4(
        std::clamp(reference.densityScale, 0.5f, 2.0f),
        std::clamp(reference.stormHeightVariation, 0.0f, 2000.0f),
        std::clamp(reference.stormBottomVariation, 0.0f, 500.0f),
        reference.stormVolume
            ? (reference.stormColumnEnvelope ? 2.0f : 1.0f)
            : 0.0f);
    // A02c volume probe defaults. The capture path overwrites these rows
    // through its own copy of the constants so the ordinary render path is
    // unaffected (mode 0 with a zero grid is never dispatched).
    constants.referenceVolume = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
    constants.referenceGrid = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    constants.referenceAudit = DirectX::XMFLOAT4(
        1000.0f, 2000.0f, 0.0f, reference.odTestPattern ? 1.0f : 0.0f);
    const float macroSeedPhase =
        static_cast<float>(reference.macroSeed % 8192u) / 8192.0f * 128.0f;
    constants.referenceMacro = DirectX::XMFLOAT4(
        std::clamp(reference.macroScale, 2000.0f, 60000.0f),
        std::clamp(reference.macroSecondaryScale, 800.0f, 30000.0f),
        std::clamp(reference.macroThreshold, 0.05f, 0.95f),
        std::clamp(reference.macroSoftness, 0.01f, 0.50f));
    constants.referenceMacro2 = DirectX::XMFLOAT4(
        std::clamp(reference.macroDetailWeight, 0.0f, 1.0f),
        std::clamp(reference.macroSecondaryWeight, 0.0f, 0.70f),
        std::clamp(reference.macroEdgeJitter, 0.0f, 0.30f),
        macroSeedPhase);
    // A03b: local occupancy is a separate permutation. It implies the
    // macro-coverage preserve path (the density transfer must not cut the
    // occupancy) and full-precision seed phases.
    const bool a03b =
        reference.macroLocalOccupancy || reference.stormLocalOccupancy;
    constants.referenceMacro3 = DirectX::XMFLOAT4(
        std::clamp(reference.macroHeightWeight, 0.0f, 1.0f),
        static_cast<float>(
            ((a03b ||
              reference.macroCoveragePreserve ||
              reference.macroCoverageCalibrate ||
              reference.macroWeatherQuantile ||
              reference.macroWeatherLocal) ? 1u : 0u) |
            ((a03b || reference.macroSeedHash) ? 2u : 0u) |
            (reference.macroDetailCompensate ? 4u : 0u) |
            (reference.stormMacroGapReplace ? 8u : 0u) |
            (reference.macroCoverageCalibrate ? 16u : 0u) |
            (reference.macroWeatherQuantile ? 32u : 0u) |
            (reference.macroWeatherLocal ? 64u : 0u) |
            (a03b ? 128u : 0u) |
            (reference.stormLocalOccupancy ? 256u : 0u)),
        // One fixed global calibration of the A03b local-anomaly gate
        // (mean 0.000, sigma 0.160), measured over six macro seeds and a
        // 60 km grid. Presence is a horizontal-plane decision, so the gate
        // quantile equals the column fraction. The weather control is a
        // single gamma 1.2 curve, global and not per-seed.
        a03b
            ? static_cast<float>(
                  0.160 * NormalQuantile(
                              1.0 - std::pow(
                                        std::clamp(params.coverage, 0.02f, 0.98f),
                                        1.2f)))
            : ((reference.macroCoverageCalibrate ||
                reference.macroWeatherQuantile ||
                reference.macroWeatherLocal)
                   ? static_cast<float>(
                         0.15 * NormalQuantile(
                                    1.0 - std::clamp(params.coverage, 0.02f, 0.98f)) -
                         0.33 * std::clamp(reference.macroSoftness, 0.01f, 0.50f))
                   : 0.0f),
        0.0f);
    // A03a: full-precision seed delta. A03 used (seed % 8192) / 64, so seeds
    // 64 apart produced the same field. The delta is shifted to exactly zero
    // at the reference seed 1337, preserving the accepted A03 layout; it is
    // only consumed when bit1 of ReferenceMacro3.y is set.
    const auto mixSeed = [](uint32_t value)
    {
        value += 0x9E3779B9u;
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        value *= 0x846CA68Bu;
        value ^= value >> 16;
        return value;
    };
    const auto hashPhase = [&](uint32_t seed, float& x, float& y, float& z)
    {
        x = static_cast<float>(mixSeed(seed) & 0xFFFFFFu) /
            16777216.0f * 32.0f;
        y = static_cast<float>(mixSeed(seed ^ 0x85EBCA6Bu) & 0xFFFFFFu) /
            16777216.0f * 32.0f;
        z = static_cast<float>(mixSeed(seed + 0xC2B2AE35u) & 0xFFFFFFu) /
            16777216.0f * 32.0f;
    };
    float hashX = 0.0f;
    float hashY = 0.0f;
    float hashZ = 0.0f;
    float baseX = 0.0f;
    float baseY = 0.0f;
    float baseZ = 0.0f;
    hashPhase(reference.macroSeed, hashX, hashY, hashZ);
    hashPhase(1337u, baseX, baseY, baseZ);
    constants.referenceMacro4 = DirectX::XMFLOAT4(
        hashX - baseX, hashY - baseY, hashZ - baseZ,
        static_cast<float>(reference.densityTestPattern));
    return constants;
}
} // namespace clouds
