#pragma once

#include "GameDx12ShadowViews.h"

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace game_shadow12
{
constexpr uint32_t DefaultResolution = 2048;
constexpr float DefaultDistance = 160.0f;

struct SunCascade
{
    DirectX::XMFLOAT4X4 viewProjection{};
    float splitNear = 0;
    float splitFar = 0;
    float blendStart = 0;
    float texelWorld = 0;
    float depthSpan = 0;
    float radius = 0;
    DirectX::XMFLOAT3 lightCenter{};
};

struct SunPlan
{
    std::array<SunCascade, MaxSunCascades> cascades{};
    DirectX::XMFLOAT3 cameraForward{};
    uint32_t count = 0;
};

inline bool ValidResolution(uint32_t resolution) noexcept
{
    return resolution == 1024 || resolution == 2048 || resolution == 4096;
}

inline bool FiniteVector(DirectX::FXMVECTOR value) noexcept
{
    return !DirectX::XMVector4IsNaN(value) && !DirectX::XMVector4IsInfinite(value);
}

inline bool MakeSunPlan(const DirectX::XMFLOAT4X4& cameraViewProjection, const float eye[3],
    const DirectX::XMFLOAT3& lightDirection, uint32_t count, uint32_t resolution,
    float distance, SunPlan& result) noexcept
{
    using namespace DirectX;
    result = {};
    if (count == 0 || count > MaxSunCascades || !ValidResolution(resolution) ||
        !std::isfinite(distance) || distance < 10.0f || distance > 1000.0f)
        return false;
    const XMVECTOR camera = XMVectorSet(eye[0], eye[1], eye[2], 1.0f);
    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(cameraViewProjection._14,
        cameraViewProjection._24, cameraViewProjection._34, 0));
    const XMVECTOR direction = XMVector3Normalize(XMLoadFloat3(&lightDirection));
    if (!FiniteVector(camera) || !FiniteVector(forward) || !FiniteVector(direction) ||
        XMVectorGetX(XMVector3LengthSq(forward)) < 0.5f || XMVectorGetX(XMVector3LengthSq(direction)) < 0.5f)
        return false;
    XMVECTOR determinant;
    const auto inverse = XMMatrixInverse(&determinant, XMLoadFloat4x4(&cameraViewProjection));
    const float determinantValue = XMVectorGetX(determinant);
    if (!std::isfinite(determinantValue) || determinantValue == 0.0f)
        return false;

    XMVECTOR rays[4];
    float nearDistance = 0.0f;
    float farDistance = distance;
    for (uint32_t i = 0; i < 4; ++i)
    {
        const float x = (i & 1) ? 1.0f : -1.0f;
        const float y = (i & 2) ? 1.0f : -1.0f;
        const auto atNear = XMVectorSubtract(
            XMVector3TransformCoord(XMVectorSet(x, y, 0, 1), inverse), camera);
        const auto atFar = XMVectorSubtract(
            XMVector3TransformCoord(XMVectorSet(x, y, 1, 1), inverse), camera);
        const float nearDepth = XMVectorGetX(XMVector3Dot(atNear, forward));
        const float farDepth = XMVectorGetX(XMVector3Dot(atFar, forward));
        if (!FiniteVector(atNear) || !FiniteVector(atFar) || nearDepth <= 0.0f || farDepth <= nearDepth)
            return false;
        nearDistance = (std::max)(nearDistance, nearDepth);
        farDistance = (std::min)(farDistance, farDepth);
        rays[i] = XMVectorScale(atFar, 1.0f / farDepth);
    }
    if (farDistance <= nearDistance + 0.01f)
        return false;

    const XMVECTOR up = std::abs(XMVectorGetY(direction)) < 0.95f
        ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(0, 0, 1, 0);
    const auto right = XMVector3Normalize(XMVector3Cross(up, direction));
    const auto vertical = XMVector3Cross(direction, right);
    // Доли соответствуют 25/100, 15/40/160, 15/40/100/160 при штатной дальности.
    constexpr float fractions[MaxSunCascades][MaxSunCascades] = {
        {1.0f}, {0.25f, 1.0f}, {15.0f / 160, 40.0f / 160, 1.0f},
        {15.0f / 160, 40.0f / 160, 100.0f / 160, 1.0f}};
    constexpr float transitionFraction = 0.10f;
    constexpr float guardTexels = 4.0f;
    // Бюджет внекамерных кастеров в сторону источника. В плоскости карты
    // направление солнца сохраняет координаты X/Y у кастера и приёмника.
    constexpr float casterReach = 160.0f;
    constexpr float depthGuard = 1.0f;
    float sliceNear = nearDistance;
    float fittedNear = nearDistance;
    SunPlan plan;
    XMStoreFloat3(&plan.cameraForward, forward);
    for (uint32_t index = 0; index < count; ++index)
    {
        auto& cascade = plan.cascades[index];
        const float sliceFar = nearDistance + (farDistance - nearDistance) * fractions[count - 1][index];
        const auto relativeCenter = XMVectorScale(forward, 0.5f * (fittedNear + sliceFar));
        const auto center = XMVectorAdd(camera, relativeCenter);
        float radius = 0.0f;
        for (const auto& ray : rays)
        {
            for (const float depth : {fittedNear, sliceFar})
                radius = (std::max)(radius, XMVectorGetX(XMVector3Length(
                    XMVectorSubtract(XMVectorScale(ray, depth), relativeCenter))));
        }
        radius = std::ceil(radius / (1.0f - 2.0f * guardTexels / resolution) * 4.0f) * 0.25f;
        const float texel = 2.0f * radius / resolution;
        if (!std::isfinite(texel) || texel <= 0.0f)
            return false;
        const auto snap = [texel, center](FXMVECTOR axis)
        {
            const float value = XMVectorGetX(XMVector3Dot(center, axis));
            return std::round(value / texel) * texel;
        };
        cascade.lightCenter = {snap(right), snap(vertical), snap(direction)};
        const auto snappedCenter = XMVectorAdd(
            XMVectorMultiplyAdd(right, XMVectorReplicate(cascade.lightCenter.x),
                XMVectorScale(vertical, cascade.lightCenter.y)),
            XMVectorScale(direction, cascade.lightCenter.z));
        const auto lightEye = XMVectorSubtract(snappedCenter, XMVectorScale(direction, radius + casterReach + depthGuard));
        const auto view = XMMatrixLookToLH(lightEye, direction, up);
        cascade.depthSpan = 2.0f * radius + casterReach + 2.0f * depthGuard;
        const auto projection = view * XMMatrixOrthographicLH(2.0f * radius, 2.0f * radius, 0.0f, cascade.depthSpan);
        if (!FiniteVector(projection.r[0]) || !FiniteVector(projection.r[1]) ||
            !FiniteVector(projection.r[2]) || !FiniteVector(projection.r[3]))
            return false;
        XMStoreFloat4x4(&cascade.viewProjection, projection);
        cascade.splitNear = sliceNear;
        cascade.splitFar = sliceFar;
        cascade.blendStart = sliceFar - (sliceFar - sliceNear) * transitionFraction;
        cascade.texelWorld = texel;
        cascade.radius = radius;
        fittedNear = cascade.blendStart;
        sliceNear = sliceFar;
    }
    plan.count = count;
    result = plan;
    return true;
}
}
