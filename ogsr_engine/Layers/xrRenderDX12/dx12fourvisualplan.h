#pragma once

// F4.0-J2B-M23: CPU-testable four-real-Jupiter-visual diagnostic plan.
//
// The milestone renders exactly four validated Jupiter source visuals
// (9, 105, 106, 107) on the GPU in one owner DX12 frame. The authored level
// placement of 105/106/107 is a tiny cluster next to visual 9, so an
// unmodified authored-world draw cannot prove four distinct contributions.
// M23 therefore stages each authored asset into a fixed 2x2 NDC grid in front
// of the real game camera. This is a diagnostic placement, not a claim about
// the level layout: the geometry, vertex/index buffers, indices, bounds and
// camera come from the real payload.
//
// Everything in this header is platform independent (DirectXMath only) so the
// transform/grid math is covered by a CPU test before it reaches a GPU.

#include <DirectXMath.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace fourvisual12
{
inline constexpr uint32_t kSlotCount = 4;

// R8G8B8A8_UNORM clear value used by GameDx12OffscreenPass12.
inline constexpr float kClearColor[4] = {0.03f, 0.05f, 0.09f, 1.0f};

struct SlotPlan12
{
    uint32_t visualId;
    uint32_t expectedVertexCount;
    uint32_t expectedIndexCount;
    // Diagnostic flat tint. Unique per slot; this is the identity oracle of
    // the combined readback and not a material colour.
    float tint[4];
    // Slot centre in NDC. The four centres form a disjoint 2x2 grid.
    float ndcX;
    float ndcY;
};

// Counts match the M0/M1 real Jupiter evidence and the live J2A packet log
// (visual 9: 545v/1314i, 105: 99v/312i, 106: 120v/192i, 107: 385v/876i).
inline constexpr SlotPlan12 kPlan[kSlotCount] = {
    {9, 545, 1314, {1.00f, 0.25f, 0.25f, 1.0f}, -0.46f, 0.46f},
    {105, 99, 312, {0.25f, 1.00f, 0.25f, 1.0f}, 0.46f, 0.46f},
    {106, 120, 192, {0.30f, 0.45f, 1.00f, 1.0f}, -0.46f, -0.46f},
    {107, 385, 876, {1.00f, 0.90f, 0.25f, 1.0f}, 0.46f, -0.46f},
};

// Largest projected extent (NDC) of every staged asset. The grid pitch is
// 0.92, so a 0.36 extent keeps the four boxes disjoint with a visible gap.
inline constexpr float kTargetNdcExtent = 0.36f;

// NDC depth used only to derive a camera ray. A standard D3D projection maps
// z=0.5 close to the near plane, so the staged centre is placed further along
// that ray at kStagedDistance instead of at the unprojected z=0.5 point.
inline constexpr float kSlotNdcDepth = 0.5f;
inline constexpr float kStagedDistance = 12.0f;

inline bool FiniteFloat(float value)
{
    return std::isfinite(value);
}

inline bool ValidatePlan12(const SlotPlan12* plan, uint32_t count,
                           std::string& reason)
{
    if (plan == nullptr || count != kSlotCount)
    {
        reason = "four-visual plan has the wrong slot count";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        const SlotPlan12& slot = plan[i];
        if (slot.visualId == 0 || slot.expectedIndexCount == 0 ||
            slot.expectedVertexCount == 0)
        {
            reason = "four-visual slot has zero identity/count";
            return false;
        }
        for (uint32_t j = 0; j < i; ++j)
        {
            if (plan[j].visualId == slot.visualId)
            {
                reason = "four-visual plan repeats a visual id";
                return false;
            }
            if (plan[j].ndcX == slot.ndcX && plan[j].ndcY == slot.ndcY)
            {
                reason = "four-visual plan repeats an NDC slot";
                return false;
            }
        }
        for (uint32_t c = 0; c < 4; ++c)
        {
            if (!FiniteFloat(slot.tint[c]) || slot.tint[c] < 0.0f ||
                slot.tint[c] > 1.0f)
            {
                reason = "four-visual slot tint is outside [0,1]";
                return false;
            }
        }
        if (!FiniteFloat(slot.ndcX) || !FiniteFloat(slot.ndcY) ||
            std::fabs(slot.ndcX) > 0.95f || std::fabs(slot.ndcY) > 0.95f)
        {
            reason = "four-visual slot centre is outside the viewport";
            return false;
        }
        // Tint uniqueness is what lets the combined readback identify each
        // visual. Repeating a tint would make the pixel oracle ambiguous.
        for (uint32_t j = 0; j < i; ++j)
        {
            bool same = true;
            for (uint32_t c = 0; c < 4; ++c)
                same = same && plan[j].tint[c] == slot.tint[c];
            if (same)
            {
                reason = "four-visual plan repeats a tint";
                return false;
            }
        }
    }
    return true;
}

inline bool UnprojectNdc(const DirectX::XMFLOAT4X4& logicalViewProjection,
                         float ndcX, float ndcY, float ndcZ,
                         float out[3]) noexcept
{
    if (out == nullptr)
        return false;
    for (uint32_t row = 0; row < 4; ++row)
    {
        for (uint32_t column = 0; column < 4; ++column)
        {
            if (!FiniteFloat(logicalViewProjection.m[row][column]))
                return false;
        }
    }

    const DirectX::XMMATRIX vp =
        DirectX::XMLoadFloat4x4(&logicalViewProjection);
    DirectX::XMVECTOR determinant;
    const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(
        &determinant, vp);
    const float det = DirectX::XMVectorGetX(determinant);
    if (!FiniteFloat(det) || std::fabs(det) < 1e-12f)
        return false;

    // Row-vector convention: p_clip = p_world * ViewProjection, therefore
    // p_world = ndc_clip * inverse(ViewProjection) with a homogeneous divide.
    const DirectX::XMVECTOR clip =
        DirectX::XMVectorSet(ndcX, ndcY, ndcZ, 1.0f);
    const DirectX::XMVECTOR homogeneous =
        DirectX::XMVector4Transform(clip, inverse);
    const float w = DirectX::XMVectorGetW(homogeneous);
    if (!FiniteFloat(w) || std::fabs(w) < 1e-6f)
        return false;
    const float x = DirectX::XMVectorGetX(homogeneous) / w;
    const float y = DirectX::XMVectorGetY(homogeneous) / w;
    const float z = DirectX::XMVectorGetZ(homogeneous) / w;
    if (!FiniteFloat(x) || !FiniteFloat(y) || !FiniteFloat(z))
        return false;
    out[0] = x;
    out[1] = y;
    out[2] = z;
    return true;
}

struct ProjectedBox12
{
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;

    bool Valid() const noexcept
    {
        return FiniteFloat(minX) && FiniteFloat(minY) &&
               FiniteFloat(maxX) && FiniteFloat(maxY) &&
               FiniteFloat(minZ) && FiniteFloat(maxZ) &&
               maxX > minX && maxY > minY;
    }
    float Width() const noexcept { return maxX - minX; }
    float Height() const noexcept { return maxY - minY; }
    float MaxExtent() const noexcept
    {
        return Width() > Height() ? Width() : Height();
    }
};

// Projects the eight authored AABB corners through World * ViewProjection.
// A corner behind the camera (w <= 0) makes the box unusable: the staged
// transform must keep the whole asset in front of the real game camera.
inline bool ProjectLocalBounds(const DirectX::XMFLOAT4X4& logicalViewProjection,
                               const DirectX::XMFLOAT4X4& logicalWorld,
                               const float localMinimum[3],
                               const float localMaximum[3],
                               ProjectedBox12& box) noexcept
{
    box = {};
    if (localMinimum == nullptr || localMaximum == nullptr)
        return false;
    for (uint32_t c = 0; c < 3; ++c)
    {
        if (!FiniteFloat(localMinimum[c]) ||
            !FiniteFloat(localMaximum[c]) ||
            localMaximum[c] < localMinimum[c])
            return false;
    }

    const DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&logicalWorld);
    const DirectX::XMMATRIX vp =
        DirectX::XMLoadFloat4x4(&logicalViewProjection);

    bool first = true;
    for (uint32_t corner = 0; corner < 8; ++corner)
    {
        const float local[3] = {
            (corner & 1u) ? localMaximum[0] : localMinimum[0],
            (corner & 2u) ? localMaximum[1] : localMinimum[1],
            (corner & 4u) ? localMaximum[2] : localMinimum[2]};
        DirectX::XMVECTOR position =
            DirectX::XMVectorSet(local[0], local[1], local[2], 1.0f);
        position = DirectX::XMVector4Transform(position, world);
        position = DirectX::XMVector4Transform(position, vp);
        const float w = DirectX::XMVectorGetW(position);
        if (!FiniteFloat(w) || w <= 1e-6f)
            return false;
        const float x = DirectX::XMVectorGetX(position) / w;
        const float y = DirectX::XMVectorGetY(position) / w;
        const float z = DirectX::XMVectorGetZ(position) / w;
        if (!FiniteFloat(x) || !FiniteFloat(y) || !FiniteFloat(z))
            return false;
        if (first)
        {
            box.minX = box.maxX = x;
            box.minY = box.maxY = y;
            box.minZ = box.maxZ = z;
            first = false;
        }
        else
        {
            if (x < box.minX) box.minX = x;
            if (x > box.maxX) box.maxX = x;
            if (y < box.minY) box.minY = y;
            if (y > box.maxY) box.maxY = y;
            if (z < box.minZ) box.minZ = z;
            if (z > box.maxZ) box.maxZ = z;
        }
    }
    return box.Valid();
}

struct StagedTransform12
{
    DirectX::XMFLOAT4X4 world{};      // logical row-vector world
    float scale = 0.0f;
    float yawDegrees = 0.0f;
    float centerWorld[3] = {};
    float distance = 0.0f;
    ProjectedBox12 projected{};
};

// Builds the diagnostic world transform for one authored asset:
//   - the authored AABB centre is moved to the world point that projects to
//     the requested NDC slot centre at kSlotNdcDepth;
//   - the asset is uniformly scaled so its largest projected AABB extent is
//     kTargetNdcExtent, keeping the four slots disjoint;
//   - the final transform is re-projected and rejected unless it is finite,
//     fully in front of the camera, inside the viewport and close to target.
inline bool BuildStagedTransform12(
    const DirectX::XMFLOAT4X4& logicalViewProjection,
    const float cameraPosition[3],
    const float localMinimum[3],
    const float localMaximum[3],
    float ndcX,
    float ndcY,
    StagedTransform12& out,
    std::string& reason)
{
    out = {};
    if (cameraPosition == nullptr)
    {
        reason = "missing camera position";
        return false;
    }
    for (uint32_t c = 0; c < 3; ++c)
    {
        if (!FiniteFloat(cameraPosition[c]))
        {
            reason = "non-finite camera position";
            return false;
        }
        if (!FiniteFloat(localMinimum[c]) ||
            !FiniteFloat(localMaximum[c]) ||
            localMaximum[c] < localMinimum[c])
        {
            reason = "invalid local bounds";
            return false;
        }
    }

    float rayPoint[3] = {};
    if (!UnprojectNdc(logicalViewProjection, ndcX, ndcY, kSlotNdcDepth,
                      rayPoint))
    {
        reason = "slot NDC centre cannot be unprojected";
        return false;
    }
    // The unprojected point lies on the camera ray through the requested NDC
    // slot. Place the asset at a controlled distance on that same ray so the
    // slot is independent of the projection's (non-linear) depth mapping.
    double dx = static_cast<double>(rayPoint[0]) - cameraPosition[0];
    double dy = static_cast<double>(rayPoint[1]) - cameraPosition[1];
    double dz = static_cast<double>(rayPoint[2]) - cameraPosition[2];
    const double rayLength = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!FiniteFloat(static_cast<float>(rayLength)) || rayLength < 1e-4)
    {
        reason = "slot NDC centre is degenerate for the camera";
        return false;
    }
    dx /= rayLength;
    dy /= rayLength;
    dz /= rayLength;
    const float centerWorld[3] = {
        static_cast<float>(cameraPosition[0] + dx * kStagedDistance),
        static_cast<float>(cameraPosition[1] + dy * kStagedDistance),
        static_cast<float>(cameraPosition[2] + dz * kStagedDistance)};

    {
        DirectX::XMVECTOR clip = DirectX::XMVectorSet(
            centerWorld[0], centerWorld[1], centerWorld[2], 1.0f);
        clip = DirectX::XMVector4Transform(
            clip, DirectX::XMLoadFloat4x4(&logicalViewProjection));
        const float w = DirectX::XMVectorGetW(clip);
        if (!FiniteFloat(w) || w <= 1e-6f)
        {
            reason = "staged slot centre is behind the camera";
            return false;
        }
        const float projectedX = DirectX::XMVectorGetX(clip) / w;
        const float projectedY = DirectX::XMVectorGetY(clip) / w;
        if (std::fabs(projectedX - ndcX) > 0.01f ||
            std::fabs(projectedY - ndcY) > 0.01f)
        {
            reason = "staged slot centre does not preserve its NDC slot";
            return false;
        }
    }

    const float localCenter[3] = {
        (localMinimum[0] + localMaximum[0]) * 0.5f,
        (localMinimum[1] + localMaximum[1]) * 0.5f,
        (localMinimum[2] + localMaximum[2]) * 0.5f};

    // Thin authored plates (for example visual 106) can be edge-on to the
    // real game camera and collapse into a few pixels. Try a small set of
    // deterministic yaw angles and choose the one with the largest projected
    // AABB area so the diagnostic frame shows the authored surface rather
    // than a knife edge. This is a staging rotation only; geometry, indices
    // and bounds are untouched.
    constexpr float kYawCandidates[4] = {0.0f, 45.0f, 90.0f, 135.0f};
    ProjectedBox12 bestBox;
    DirectX::XMFLOAT4X4 bestWorld = {};
    float bestScale = 0.0f;
    float bestYaw = 0.0f;
    float bestScore = -1.0f;

    for (uint32_t candidate = 0; candidate < 4; ++candidate)
    {
        const float yaw = kYawCandidates[candidate];
        const DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationY(
            DirectX::XMConvertToRadians(yaw));
        const DirectX::XMVECTOR localCenterVector = DirectX::XMVectorSet(
            localCenter[0], localCenter[1], localCenter[2], 1.0f);
        const DirectX::XMVECTOR rotatedCenterVector =
            DirectX::XMVector4Transform(localCenterVector, rotation);
        const float rotatedCenter[3] = {
            DirectX::XMVectorGetX(rotatedCenterVector),
            DirectX::XMVectorGetY(rotatedCenterVector),
            DirectX::XMVectorGetZ(rotatedCenterVector)};

        // Unit-scale placement with the rotated centre on the slot centre.
        DirectX::XMFLOAT4X4 unitWorld = {};
        DirectX::XMStoreFloat4x4(
            &unitWorld,
            rotation * DirectX::XMMatrixTranslation(
                           centerWorld[0] - rotatedCenter[0],
                           centerWorld[1] - rotatedCenter[1],
                           centerWorld[2] - rotatedCenter[2]));
        ProjectedBox12 unitBox;
        if (!ProjectLocalBounds(logicalViewProjection, unitWorld,
                                localMinimum, localMaximum, unitBox))
            continue;
        const float unitExtent = unitBox.MaxExtent();
        if (!FiniteFloat(unitExtent) || unitExtent <= 1e-9f)
            continue;
        const float scale = kTargetNdcExtent / unitExtent;
        if (!FiniteFloat(scale) || scale <= 1e-6f || scale > 1.0e6f)
            continue;

        DirectX::XMFLOAT4X4 world = {};
        DirectX::XMStoreFloat4x4(
            &world,
            rotation * DirectX::XMMatrixScaling(scale, scale, scale) *
                DirectX::XMMatrixTranslation(
                    centerWorld[0] - rotatedCenter[0] * scale,
                    centerWorld[1] - rotatedCenter[1] * scale,
                    centerWorld[2] - rotatedCenter[2] * scale));
        ProjectedBox12 box;
        if (!ProjectLocalBounds(logicalViewProjection, world,
                                localMinimum, localMaximum, box))
            continue;
        if (box.minX < -0.995f || box.maxX > 0.995f ||
            box.minY < -0.995f || box.maxY > 0.995f)
            continue;
        if (box.minZ < -0.001f || box.maxZ > 1.001f)
            continue;
        const float extent = box.MaxExtent();
        if (extent < kTargetNdcExtent * 0.5f ||
            extent > kTargetNdcExtent * 1.5f)
            continue;

        const float score = box.Width() * box.Height();
        if (score > bestScore)
        {
            bestScore = score;
            bestBox = box;
            bestWorld = world;
            bestScale = scale;
            bestYaw = yaw;
        }
    }

    if (bestScore < 0.0f)
    {
        reason = "staged asset has no valid yaw candidate";
        return false;
    }

    out.world = bestWorld;
    out.scale = bestScale;
    out.yawDegrees = bestYaw;
    out.centerWorld[0] = centerWorld[0];
    out.centerWorld[1] = centerWorld[1];
    out.centerWorld[2] = centerWorld[2];
    out.distance = kStagedDistance;
    out.projected = bestBox;
    return true;
}

// The two boxes must not touch: an overlap would let one visual hide another
// and invalidate the per-colour contribution oracle.
inline bool BoxesDisjoint(const ProjectedBox12& a, const ProjectedBox12& b)
{
    return a.maxX <= b.minX || b.maxX <= a.minX ||
           a.maxY <= b.minY || b.maxY <= a.minY;
}

} // namespace fourvisual12

// The plan is a compile-time contract; a malformed table must not compile into
// an M23 frame. This is checked at runtime by the CPU test as well, because a
// constexpr string reason is not available here.
static_assert(fourvisual12::kPlan[0].visualId == 9 &&
                  fourvisual12::kPlan[1].visualId == 105 &&
                  fourvisual12::kPlan[2].visualId == 106 &&
                  fourvisual12::kPlan[3].visualId == 107,
              "M23 must render Jupiter visuals 9, 105, 106, 107");
static_assert(fourvisual12::kPlan[0].expectedIndexCount == 1314 &&
                  fourvisual12::kPlan[1].expectedIndexCount == 312 &&
                  fourvisual12::kPlan[2].expectedIndexCount == 192 &&
                  fourvisual12::kPlan[3].expectedIndexCount == 876,
              "M23 M0 index-count oracle changed");
static_assert(fourvisual12::kPlan[0].expectedVertexCount == 545 &&
                  fourvisual12::kPlan[1].expectedVertexCount == 99 &&
                  fourvisual12::kPlan[2].expectedVertexCount == 120 &&
                  fourvisual12::kPlan[3].expectedVertexCount == 385,
              "M23 M0 vertex-count oracle changed");
