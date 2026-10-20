#pragma once

#include <DirectXMath.h>
#include <cmath>
#include <cstdint>

// F3.5.6A: deterministic layout of the per-frame upload buffer used by the
// opt-in per-object transform path.
//
// The existing per-frame constant buffer keeps ViewProjection at offset 0.
// Object constants follow at 256-byte aligned offsets; each object owns one
// 256-byte CBV slice. The layout helper is platform independent and is shared
// with CPU tests, so offset/capacity mistakes are caught before GPU recording.
namespace dx12object12
{
inline constexpr uint32_t kConstantBufferAlignment = 256;
inline constexpr uint32_t kMaxObjects = 256;
inline constexpr uint64_t kFrameConstantsSize = 256;
inline constexpr uint64_t kObjectConstantsOffset = kFrameConstantsSize;
inline constexpr uint64_t kObjectConstantsSize = 256;
inline constexpr uint64_t kUploadBytes =
    kObjectConstantsOffset + static_cast<uint64_t>(kMaxObjects) * kObjectConstantsSize;

struct ObjectConstants12
{
    // Stored in the same transposed representation as the existing
    // ViewProjection CB: HLSL's default column-major load restores the
    // logical row-vector matrix.
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4 tint;
};

static_assert(sizeof(DirectX::XMFLOAT4X4) == 64, "XMFLOAT4X4 must be 64 bytes");
static_assert(sizeof(DirectX::XMFLOAT4) == 16, "XMFLOAT4 must be 16 bytes");
static_assert(sizeof(ObjectConstants12) <= kObjectConstantsSize,
              "object constants exceed one CBV slice");
static_assert(kUploadBytes % kConstantBufferAlignment == 0,
              "per-frame upload size must stay 256-byte aligned");

constexpr bool IsValidObjectIndex(uint32_t index)
{
    return index < kMaxObjects;
}

constexpr uint64_t ObjectConstantsOffset(uint32_t index)
{
    return kObjectConstantsOffset +
           static_cast<uint64_t>(index) * kObjectConstantsSize;
}

// The renderer uses the row-vector convention:
//   p_clip = p_local * World * ViewProjection.
// In XMFLOAT4X4 this means translation lives in row 3 and the last column is
// (0, 0, 0, 1). A singular 3x3 part is rejected because the transform cannot
// produce a meaningful visible object.
inline bool IsValidTransformWorld(const DirectX::XMFLOAT4X4& world)
{
    for (uint32_t row = 0; row < 4; ++row)
    {
        for (uint32_t column = 0; column < 4; ++column)
        {
            if (!std::isfinite(world.m[row][column]))
                return false;
        }
    }

    if (world.m[0][3] != 0.0f || world.m[1][3] != 0.0f ||
        world.m[2][3] != 0.0f || world.m[3][3] != 1.0f)
    {
        return false;
    }

    const double a = world.m[0][0];
    const double b = world.m[0][1];
    const double c = world.m[0][2];
    const double d = world.m[1][0];
    const double e = world.m[1][1];
    const double f = world.m[1][2];
    const double g = world.m[2][0];
    const double h = world.m[2][1];
    const double i = world.m[2][2];

    const double determinant =
        a * (e * i - f * h) -
        b * (d * i - f * g) +
        c * (d * h - e * g);
    return std::isfinite(determinant) && determinant != 0.0;
}
} // namespace dx12object12
