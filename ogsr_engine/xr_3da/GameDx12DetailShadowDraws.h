#pragma once

#include "GameDx12DetailShadowMembership.h"
#include "GameDx12LevelDetails.h"

#include <limits>

namespace gameaux12
{
struct DetailShadowDraw
{
    uint32_t mesh, first, count;
};

inline bool DetailCasterRegion(const DirectX::XMFLOAT4X4& viewProjection, DetailShadowRegion& result) noexcept
{
    using namespace DirectX;
    XMVECTOR determinant;
    const auto inverse = XMMatrixInverse(&determinant, XMLoadFloat4x4(&viewProjection));
    if (!std::isfinite(XMVectorGetX(determinant)) || XMVectorGetX(determinant) == 0.0f)
        return false;
    const float limit = (std::numeric_limits<float>::max)();
    result = {limit, limit, -limit, -limit};
    for (unsigned corner = 0; corner < 8; ++corner)
    {
        const auto clip = XMVectorSet(corner & 1 ? 1.0f : -1.0f, corner & 2 ? 1.0f : -1.0f,
            corner & 4 ? 1.0f : 0.0f, 1.0f);
        const auto point = XMVector3TransformCoord(clip, inverse);
        const float x = XMVectorGetX(point), z = XMVectorGetZ(point);
        if (!std::isfinite(x) || !std::isfinite(z))
            return false;
        result.minX = (std::min)(result.minX, x);
        result.minZ = (std::min)(result.minZ, z);
        result.maxX = (std::max)(result.maxX, x);
        result.maxZ = (std::max)(result.maxZ, z);
    }
    return true;
}

inline std::vector<DetailShadowBound> DetailShadowBounds(std::span<const DetailShadowPlacement> placements)
{
    std::vector<DetailShadowBound> bounds;
    bounds.reserve(placements.size());
    for (const auto& p : placements)
        bounds.push_back({{p.slot, p.ordinal}, {p.world._41, p.world._42, p.world._43}, p.radius});
    return bounds;
}

// Группировка по стабильному индексу модели, внутри группы по identity
// размещения. Каждая проекция читает только свой диапазон общей загрузки.
inline bool AppendDetailShadowDraws(std::span<const DetailShadowPlacement> placements,
    std::span<const DetailShadowBound> bounds, std::span<const DetailShadowPlane> planes,
    std::span<const uint32_t> meshes, size_t maximumBytes,
    std::vector<DirectX::XMFLOAT4X4>& transforms, std::vector<DetailShadowDraw>& draws)
{
    if (placements.size() != bounds.size())
        return false;
    const auto selected = DetailShadowMembership(bounds, planes);
    std::vector<uint32_t> counts(meshes.size(), 0);
    size_t accepted = 0;
    const auto hasMesh = [&](size_t i)
    {
        const auto model = placements[i].model;
        return model < meshes.size() && meshes[model] != kInvalidMesh;
    };
    for (const auto i : selected)
        if (hasMesh(i))
        {
            ++accepted;
            ++counts[placements[i].model];
        }
    const size_t maxCount = (std::min)(size_t{UINT32_MAX}, maximumBytes / sizeof(DirectX::XMFLOAT4X4));
    if (transforms.size() > maxCount || accepted > maxCount - transforms.size())
        return false;
    uint32_t cursor = static_cast<uint32_t>(transforms.size());
    transforms.resize(transforms.size() + accepted);
    std::vector<uint32_t> next(meshes.size());
    for (size_t model = 0; model < meshes.size(); ++model)
        if (counts[model] != 0)
        {
            next[model] = cursor;
            draws.push_back({meshes[model], cursor, counts[model]});
            cursor += counts[model];
        }
    for (const auto i : selected)
        if (hasMesh(i))
        {
            const auto model = placements[i].model;
            transforms[next[model]++] = placements[i].world;
        }
    return true;
}
}
