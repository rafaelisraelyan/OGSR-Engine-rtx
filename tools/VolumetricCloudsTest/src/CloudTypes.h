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

enum class CloudDebugMode : uint32_t
{
    Final = 0,
    Density = 1,
    Transmittance = 2,
    StepCount = 3,
    Occupancy = 4,
};

inline const char* DebugModeName(CloudDebugMode mode)
{
    switch (mode)
    {
    case CloudDebugMode::Density: return "density";
    case CloudDebugMode::Transmittance: return "transmittance";
    case CloudDebugMode::StepCount: return "step-count";
    case CloudDebugMode::Occupancy: return "occupancy";
    default: return "final";
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
    uint32_t stepCount = 48;
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
    params.layerBottom = 600.0f;
    params.layerTop = 1900.0f;
    params.coverage = 0.58f;
    params.density = 0.92f;
    params.extinction = 0.020f;
    params.anisotropy = 0.60f;
    params.noiseScale = 0.00020f;
    params.sunAzimuthDegrees = 42.0f;
    params.sunElevationDegrees = 34.0f;
    params.sunColor = DirectX::XMFLOAT3(1.00f, 0.91f, 0.78f);
    params.sunIntensity = 8.0f;
    params.ambientColor = DirectX::XMFLOAT3(0.32f, 0.42f, 0.55f);
    params.ambientIntensity = 0.22f;
    params.skyZenith = DirectX::XMFLOAT3(0.10f, 0.20f, 0.34f);
    params.skyHorizon = DirectX::XMFLOAT3(0.45f, 0.52f, 0.60f);
    params.exposure = 1.0f;
    params.stepCount = 48;
}

inline void ApplyOvercastPreset(CloudParams& params)
{
    params.layerBottom = 450.0f;
    params.layerTop = 2000.0f;
    params.coverage = 0.66f;
    params.density = 0.72f;
    params.extinction = 0.018f;
    params.anisotropy = 0.38f;
    params.noiseScale = 0.00018f;
    params.sunAzimuthDegrees = 65.0f;
    params.sunElevationDegrees = 18.0f;
    params.sunColor = DirectX::XMFLOAT3(0.78f, 0.82f, 0.88f);
    params.sunIntensity = 2.0f;
    params.ambientColor = DirectX::XMFLOAT3(0.27f, 0.31f, 0.36f);
    params.ambientIntensity = 0.35f;
    params.skyZenith = DirectX::XMFLOAT3(0.17f, 0.20f, 0.24f);
    params.skyHorizon = DirectX::XMFLOAT3(0.32f, 0.35f, 0.38f);
    params.exposure = 0.90f;
    params.stepCount = 56;
}

inline bool ApplyPreset(const std::string& name, CloudParams& params)
{
    if (name == "clear")
    {
        ApplyClearPreset(params);
        return true;
    }
    if (name == "overcast")
    {
        ApplyOvercastPreset(params);
        return true;
    }
    return false;
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
};
static_assert(sizeof(CloudFrameConstants) == 13 * 16,
              "CloudFrameConstants must remain 13 float4 rows");
static_assert(sizeof(CloudFrameConstants) % 16 == 0,
              "constant buffer alignment");

inline CloudFrameConstants BuildCloudConstants(
    const Camera& camera,
    const CloudParams& params,
    uint32_t outputWidth,
    uint32_t outputHeight,
    float timeSeconds,
    CloudDebugMode debugMode)
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
    return constants;
}
} // namespace clouds
