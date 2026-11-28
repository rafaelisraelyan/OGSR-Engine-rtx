#pragma once

#include "GameDx12DetailShadowMembership.h"
#include "GameDx12LevelDetails.h"

#include <limits>

namespace gameaux12
{
// Зарезервированное значение таблицы моделей; kInvalidMesh означает
// отсутствие GPU mesh, это значение — неподдерживаемый материал/формат.
constexpr uint32_t kUnsupportedDetailShadowMesh = UINT32_MAX - 1u;
enum class DetailShadowDrawStatus { Ready, InvalidInput, MissingResources, UploadBudget };
struct DetailShadowDrawReport
{
    DetailShadowDrawStatus status = DetailShadowDrawStatus::InvalidInput;
    size_t selected = 0, prepared = 0, missingMesh = 0, unsupportedMaterial = 0;
};

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
    std::vector<DirectX::XMFLOAT4X4>& transforms, std::vector<DetailShadowDraw>& draws,
    DetailShadowDrawReport* report = nullptr)
{
    DetailShadowDrawReport result;
    const auto finish = [&](DetailShadowDrawStatus status)
    {
        result.status = status;
        if (report) *report = result;
        return status == DetailShadowDrawStatus::Ready;
    };
    if (placements.size() != bounds.size())
        return finish(DetailShadowDrawStatus::InvalidInput);
    const auto selected = DetailShadowMembership(bounds, planes);
    result.selected = selected.size();
    std::vector<size_t> counts(meshes.size(), 0);
    for (const auto i : selected)
    {
        const auto model = placements[i].model;
        if (model >= meshes.size() || meshes[model] == kInvalidMesh)
            ++result.missingMesh;
        else if (meshes[model] == kUnsupportedDetailShadowMesh)
            ++result.unsupportedMaterial;
        else
            ++counts[model];
    }
    if (result.missingMesh || result.unsupportedMaterial)
        return finish(DetailShadowDrawStatus::MissingResources);
    const size_t accepted = selected.size();
    const size_t maxCount = (std::min)(size_t{UINT32_MAX}, maximumBytes / sizeof(DirectX::XMFLOAT4X4));
    if (transforms.size() > maxCount || accepted > maxCount - transforms.size())
        return finish(DetailShadowDrawStatus::UploadBudget);
    std::vector<uint32_t> next(meshes.size());
    const auto drawCount = std::count_if(counts.begin(), counts.end(), [](size_t count) { return count != 0; });
    draws.reserve(draws.size() + static_cast<size_t>(drawCount));
    uint32_t cursor = static_cast<uint32_t>(transforms.size());
    transforms.resize(transforms.size() + accepted);
    for (size_t model = 0; model < meshes.size(); ++model)
        if (counts[model] != 0)
        {
            next[model] = cursor;
            draws.push_back({meshes[model], cursor, static_cast<uint32_t>(counts[model])});
            cursor += static_cast<uint32_t>(counts[model]);
        }
    for (const auto i : selected)
    {
        const auto model = placements[i].model;
        transforms[next[model]++] = placements[i].world;
    }
    result.prepared = accepted;
    return finish(DetailShadowDrawStatus::Ready);
}
}
