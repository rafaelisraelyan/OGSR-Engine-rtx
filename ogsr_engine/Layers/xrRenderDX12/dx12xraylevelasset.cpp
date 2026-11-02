#include "dx12xraylevelasset.h"

#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

static_assert(std::is_nothrow_move_assignable_v<XrayAssetData12>,
              "transactional asset publish must not throw");

namespace
{
bool Fail(const char** reason, const char* text)
{
    if (reason)
        *reason = text;
    return false;
}

float ReadPosition(const xraylevel12::Geometry& geometry, uint32_t vertex,
                   uint32_t component)
{
    float value = 0.0f;
    std::memcpy(&value,
                geometry.vertex_bytes.data() +
                    static_cast<size_t>(vertex) * geometry.vertex_stride +
                    component * sizeof(float),
                sizeof(value));
    return value;
}
} // namespace

bool BuildXrayLevelAsset12(const xraylevel12::Geometry& geometry,
                           XrayAssetData12& asset,
                           const char** reason)
{
    asset = {};
    if (geometry.vertex_stride != 32)
        return Fail(reason, "level visual is not 32-byte stride");
    if (geometry.vertex_count == 0 || geometry.vertex_bytes.size() <
            static_cast<size_t>(geometry.vertex_count) * 32u)
        return Fail(reason, "level visual vertex range is incomplete");
    if (geometry.index_count == 0 || geometry.index_count % 3 != 0 ||
        geometry.indices.size() != geometry.index_count)
        return Fail(reason, "level visual index range is incomplete");

    for (uint32_t i = 0; i < geometry.vertex_count; ++i)
    {
        for (uint32_t component = 0; component < 3; ++component)
        {
            if (!std::isfinite(ReadPosition(geometry, i, component)))
                return Fail(reason, "level visual position is not finite");
        }
    }
    for (uint16_t index : geometry.indices)
    {
        if (index >= geometry.vertex_count)
            return Fail(reason, "level visual index exceeds vertex count");
    }

    for (uint32_t component = 0; component < 3; ++component)
    {
        if (!std::isfinite(geometry.bounds.minimum[component]) ||
            !std::isfinite(geometry.bounds.maximum[component]) ||
            geometry.bounds.minimum[component] >
                geometry.bounds.maximum[component])
            return Fail(reason, "level visual bounds are invalid");
        if (!std::isfinite(geometry.sphere.center[component]))
            return Fail(reason, "level visual sphere center is not finite");
    }
    if (!std::isfinite(geometry.sphere.radius) ||
        !(geometry.sphere.radius > 0.0f))
        return Fail(reason, "level visual sphere radius is invalid");

    // The uploaded positions must match the published bounds and sphere. This
    // catches any future mismatch between the reader's summary and its compact
    // vertex bytes before a GPU buffer is created.
    constexpr float kTolerance = 1.0e-4f;
    for (uint32_t i = 0; i < geometry.vertex_count; ++i)
    {
        float position[3] = {};
        for (uint32_t component = 0; component < 3; ++component)
        {
            position[component] = ReadPosition(geometry, i, component);
            if (position[component] <
                    geometry.bounds.minimum[component] - kTolerance ||
                position[component] >
                    geometry.bounds.maximum[component] + kTolerance)
                return Fail(reason, "level visual position is outside bounds");
        }
        const double dx = static_cast<double>(position[0]) -
                          geometry.sphere.center[0];
        const double dy = static_cast<double>(position[1]) -
                          geometry.sphere.center[1];
        const double dz = static_cast<double>(position[2]) -
                          geometry.sphere.center[2];
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(distance) ||
            distance > static_cast<double>(geometry.sphere.radius) + 1.0e-3)
            return Fail(reason, "level visual position is outside sphere");
    }

    // Transactional publish: all allocations happen in a local candidate.
    // A std::bad_alloc/std::length_error leaves the caller's asset empty and
    // never exposes a partially filled GPU asset description.
    XrayAssetData12 candidate;
    try
    {
        candidate.vertices.resize(geometry.vertex_count);
        for (uint32_t i = 0; i < geometry.vertex_count; ++i)
        {
            XrayVertex12& vertex = candidate.vertices[i];
            vertex.position = {ReadPosition(geometry, i, 0),
                               ReadPosition(geometry, i, 1),
                               ReadPosition(geometry, i, 2)};
            // Flat-fallback material: the existing X-Ray PS samples only the
            // texture, so normal/UV are placeholders, not decoded source data.
            vertex.normal = {0.0f, 0.0f, 1.0f};
            vertex.uv = {0.0f, 0.0f};
        }
        candidate.indices = geometry.indices;
        candidate.minimum = {geometry.bounds.minimum[0],
                             geometry.bounds.minimum[1],
                             geometry.bounds.minimum[2]};
        candidate.maximum = {geometry.bounds.maximum[0],
                             geometry.bounds.maximum[1],
                             geometry.bounds.maximum[2]};
        candidate.sphere.center = {geometry.sphere.center[0],
                                   geometry.sphere.center[1],
                                   geometry.sphere.center[2]};
        candidate.sphere.radius = geometry.sphere.radius;
        candidate.textureName = "jupiter_level_visual";
        candidate.shaderName = "flat_color";
    }
    catch (const std::bad_alloc&)
    {
        asset = {};
        return Fail(reason, "level visual allocation failed");
    }
    catch (const std::length_error&)
    {
        asset = {};
        return Fail(reason, "level visual allocation failed");
    }

    asset = std::move(candidate);
    return true;
}
