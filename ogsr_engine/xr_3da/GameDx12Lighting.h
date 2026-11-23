#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>

class IRender_Light;

namespace gamelight12
{
inline constexpr std::size_t MaxLocalLights = 16;
inline constexpr std::size_t MaxLocalLightTrace = 3;

enum class LocalType : uint32_t
{
    Point,
    Spot,
};

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct LocalLight
{
    Vec3 position;
    Vec3 direction; // От источника к сцене в мировом пространстве.
    Vec3 color; // Линейная яркость после декодирования цвета X-Ray.
    float range = 0.0f;
    float outerCos = 0.0f;
    float innerCos = 0.0f;
    LocalType type = LocalType::Point;
    uint64_t id = 0;
    float shadowNear = 0.1f;
    float shadowFar = 0.0f;
    double selectionScore = 0.0;
    bool nearVolume = false;
    bool sourceShadow = false;
    bool castsShadow = false;
    bool active = false;
};

// Та же степень, которую SRGBToLinear использует в R4 при накоплении света.
inline bool DecodeLightColor(float r, float g, float b, Vec3& output) noexcept
{
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b))
        return false;

    output = { std::pow(std::fmax(0.f, r), 2.2f),
               std::pow(std::fmax(0.f, g), 2.2f),
               std::pow(std::fmax(0.f, b), 2.2f) };
    return std::isfinite(output.x) && std::isfinite(output.y) && std::isfinite(output.z);
}

inline bool NormalizeDirection(float x, float y, float z, Vec3& output) noexcept
{
    const double lengthSquared = double{x} * x + double{y} * y + double{z} * z;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.e-12)
        return false;
    const double inverseLength = 1.0 / std::sqrt(lengthSquared);
    output = {static_cast<float>(x * inverseLength),
              static_cast<float>(y * inverseLength),
              static_cast<float>(z * inverseLength)};
    return true;
}

enum class LocalRejection : uint8_t
{
    None,
    UnsupportedSource,
    Inactive,
    NotWorld,
    UnsupportedType,
    InvalidData,
    NoEnergy,
    LightingBudget,
    NotInSpatialQuery,
    OutsideNearSphere,
    Disabled,
};

inline const char* LocalTraceReasonName(LocalRejection reason) noexcept
{
    switch (reason)
    {
    case LocalRejection::None: return "none";
    case LocalRejection::UnsupportedSource: return "unsupported_source";
    case LocalRejection::Inactive: return "inactive";
    case LocalRejection::NotWorld: return "not_world";
    case LocalRejection::UnsupportedType: return "unsupported_type";
    case LocalRejection::InvalidData: return "invalid_data";
    case LocalRejection::NoEnergy: return "no_energy";
    case LocalRejection::LightingBudget: return "lighting_budget";
    case LocalRejection::NotInSpatialQuery: return "not_in_spatial_query";
    case LocalRejection::OutsideNearSphere: return "outside_near_sphere";
    case LocalRejection::Disabled: return "disabled";
    }
    return "unknown";
}

struct LocalLightTrace
{
    uint64_t id = 0;
    float distance = 0.0f;
    float radius = 0.0f;
    Vec3 position;
    LocalType type = LocalType::Point;
    int32_t gpuSlot = -1;
    int32_t productionSlot = -1;
    Vec3 direction;
    Vec3 color;
    float outerCos = 0.0f;
    float innerCos = 0.0f;
    LocalRejection rejection = LocalRejection::None;
    bool active = false;
    bool candidate = false;
    bool inFrustumQuery = false;
    bool inNearQuery = false;
    bool nearVolume = false;
    bool cameraInside = false;
    bool sourceShadow = false;
    bool shadowEligible = false;
};

// Снимок текущих параметров источника R4 на потоке владельца.
LocalRejection ReadWorldLight(const IRender_Light& source, LocalLight& output) noexcept;

struct FrameSnapshot
{
    uint64_t levelSession = 0;
    uint64_t levelEpoch = 0;
    uint64_t frame = 0;
    bool enabled = false;
    bool sunEnabled = false;
    bool localEnabled = false;
    bool shadowsEnabled = false;
    Vec3 sunDirection; // Направление солнечных лучей в мировом пространстве.
    Vec3 sunColor;
    Vec3 ambient;
    Vec3 hemi; // Оттенок среды из R4 phase_combine, до декодирования.
    bool hemiEnabled = false;
    std::array<LocalLight, MaxLocalLights> localLights{};
    uint32_t localCount = 0;
    uint32_t localCandidateCount = 0;
    uint32_t localShadowCandidateCount = 0;
    uint64_t tracedLightId = 0;
    Vec3 diagnosticOrigin;
    bool diagnosticBurst = false;
    enum class ProbeMode : uint8_t { Live, FixedSelection, FrozenRecord };
    ProbeMode probeMode = ProbeMode::Live;
    std::array<LocalLightTrace, MaxLocalLightTrace> localTrace{};
    uint32_t localTraceCount = 0;
};
}
