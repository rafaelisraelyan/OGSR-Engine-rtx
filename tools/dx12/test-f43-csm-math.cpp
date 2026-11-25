#include "GameDx12SunCascades.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace DirectX;
using namespace game_shadow12;

void Require(bool result, const char* message)
{
    if (!result)
        throw std::runtime_error(message);
}

XMFLOAT4X4 Camera(const XMFLOAT3& eye, float fov, float nearPlane, float farPlane, float yaw = 0)
{
    XMFLOAT4X4 matrix;
    XMStoreFloat4x4(&matrix, XMMatrixLookToLH(XMLoadFloat3(&eye),
        XMVectorSet(std::sin(yaw), 0, std::cos(yaw), 0), XMVectorSet(0, 1, 0, 0)) *
        XMMatrixPerspectiveFovLH(fov, 16.0f / 9.0f, nearPlane, farPlane));
    return matrix;
}

bool Covers(const SunCascade& cascade, FXMVECTOR position)
{
    XMFLOAT3 clip;
    XMStoreFloat3(&clip, XMVector3TransformCoord(position, XMLoadFloat4x4(&cascade.viewProjection)));
    return std::abs(clip.x) < 1 && std::abs(clip.y) < 1 && clip.z >= 0 && clip.z <= 1;
}

void CheckSingleMapComparison()
{
    // Мелкая плоская грань. FR3 теряет её нормаль при абсолютном пороге.
    constexpr float pixelWorld = 0.0005f;
    constexpr float slope = 0.125f;
    const auto dx = XMVectorSet(pixelWorld, 0, pixelWorld * slope, 0);
    const auto dy = XMVectorSet(0, pixelWorld, 0, 0);
    Require(XMVectorGetX(XMVector3LengthSq(XMVector3Cross(dx, dy))) < 1.0e-12f, "FR3 threshold reproduced");
    const auto normal = XMVector3Normalize(XMVector3Cross(
        XMVectorScale(dx, 1.0f / pixelWorld), XMVectorScale(dy, 1.0f / pixelWorld)));
    const float gradient = -XMVectorGetX(normal) / XMVectorGetZ(normal);
    Require(std::abs(gradient - slope) < 1e-6f, "scale independent receiver normal");
    uint32_t legacyVisible = 0, correctedVisible = 0, occluderVisible = 0;
    for (const float offset : {-0.5f / 2048, 0.5f / 2048})
    {
        const float stored = 0.4f + offset * slope;
        const float comparison = 0.4f - 0.000015f;
        legacyVisible += comparison <= stored;
        correctedVisible += comparison + offset * gradient <= stored;
        occluderVisible += comparison + offset * gradient <= stored - 0.001f;
    }
    Require(legacyVisible == 1 && correctedVisible == 2 && occluderVisible == 0, "receiver vs occluder comparison");
    std::puts("PASS original FR3 threshold and corrected sloped-plane comparison");
}

int main()
{
    try
    {
        CheckSingleMapComparison();
        const XMFLOAT3 eye{20, 3, 50};
        const float origin[]{eye.x, eye.y, eye.z};
        const XMFLOAT3 sunlight{0.45f, -0.80f, 0.35f};
        const auto light = XMVector3Normalize(XMLoadFloat3(&sunlight));
        constexpr float fov = 1.25f;
        const auto camera = Camera(eye, fov, 0.3f, 500.0f);
        for (uint32_t count = 1; count <= MaxSunCascades; ++count)
        {
            for (const uint32_t resolution : {1024u, 2048u, 4096u})
            {
                SunPlan plan;
                Require(MakeSunPlan(camera, origin, sunlight, count, resolution, 160, plan), "build cascade projections");
                Require(plan.count == count && std::abs(plan.cascades[0].splitNear - 0.3f) < 0.001f, "camera near/depth");
                Require(std::abs(plan.cascades[count - 1].splitFar - 160.0f) < 0.001f, "camera far cap");
                float previousBlend = plan.cascades[0].splitNear;
                float previousFar = previousBlend;
                for (uint32_t slice = 0; slice < count; ++slice)
                {
                    const auto& cascade = plan.cascades[slice];
                    Require(cascade.splitNear == previousFar && cascade.splitNear < cascade.blendStart &&
                        cascade.blendStart < cascade.splitFar, "ordered splits and overlap");
                    Require(std::abs(cascade.texelWorld * resolution - 2.0f * cascade.radius) < 0.001f, "world texel size");
                    for (const float depth : {previousBlend, cascade.splitFar})
                    {
                        const float halfHeight = std::tan(fov / 2) * depth;
                        for (const float x : {-1.0f, 1.0f})
                        {
                            for (const float y : {-1.0f, 1.0f})
                            {
                                const auto point = XMVectorSet(eye.x + x * halfHeight * (16.0f / 9.0f),
                                    eye.y + y * halfHeight, eye.z + depth, 1);
                                Require(Covers(cascade, point), "receiver corner coverage");
                                Require(Covers(cascade, XMVectorSubtract(point, XMVectorScale(light, 120))), "off-camera caster coverage");
                            }
                        }
                    }
                    previousBlend = cascade.blendStart;
                    previousFar = cascade.splitFar;
                }
            }
        }
        const float center[]{0, 0, 0};
        const XMFLOAT3 direction{0, 0, 1};
        SunPlan original, shifted, rotated, limited;
        auto centerCamera = Camera({0, 0, 0}, fov, 0.1f, 500);
        Require(MakeSunPlan(centerCamera, center, direction, 3, 2048, 160, original), "centered plan");
        const float moved[]{0.1f * original.cascades[0].texelWorld, 0, 0};
        Require(MakeSunPlan(Camera({moved[0], 0, 0}, fov, 0.1f, 500), moved, direction, 3, 2048, 160, shifted), "shifted plan");
        Require(MakeSunPlan(Camera({0, 0, 0}, fov, 0.1f, 500, 0.8f), center, direction, 3, 2048, 160, rotated), "rotated plan");
        for (uint32_t i = 0; i < 3; ++i)
        {
            Require(original.cascades[i].lightCenter.x == shifted.cascades[i].lightCenter.x, "subtexel XY stabilization");
            Require(original.cascades[i].radius == rotated.cascades[i].radius, "rotation-stable radius");
        }
        Require(MakeSunPlan(Camera({0, 0, 0}, 2.85f, 0.7f, 42.0f), center, {0.01f, -1, 0}, 4, 2048, 160, limited),
            "wide FOV/vertical sun");
        Require(std::abs(limited.cascades[3].splitFar - 42.0f) < 0.005f, "physical far plane");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Require(!MakeSunPlan(camera, origin, {nan, 0, 0}, 3, 2048, 160, limited) && limited.count == 0, "invalid sun");
        Require(!MakeSunPlan(camera, origin, {}, 3, 2048, 160, limited), "zero sun direction");
        Require(!MakeSunPlan(camera, origin, sunlight, 5, 2048, 160, limited), "capacity guard");
        Require(!MakeSunPlan(camera, origin, sunlight, 3, 1536, 160, limited), "resolution guard");
        Require(!MakeSunPlan({}, origin, sunlight, 3, 2048, 160, limited), "singular camera");
        uint32_t mask = 0;
        for (uint32_t id = 0; id < MaxViews; ++id)
        {
            const View view{id};
            mask |= 1u << id;
            Require(view.IsSun() ? view.Slice() < MaxSunCascades : view.Slice() < LocalLayers, "projection identity");
        }
        Require(mask == 0xfffu, "twelve distinct projection bits");
        std::puts("PASS 1/2/3/4 projections, 1024/2048/4096, camera bounds, overlap, caster reach, snapping and validation");
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "FAIL: %s\n", exception.what());
        return 1;
    }
}
