// F4.0-J2B-M23 CPU acceptance for the four-visual staging plan.
//
// This test does not create a device and does not read game assets: it proves
// the plan identity/count/tint oracle and the staged transform math that the
// in-game GPU frame depends on. Real geometry/counts are validated again by
// the M23 VFS loader at level ready.

#include "dx12fourvisualplan.h"
#include "GameDx12VisualMask.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace
{
int g_passed = 0;
int g_failed = 0;

void Check(const char* name, bool condition)
{
    std::printf("M23_PLAN_CASE %-52s status=%s\n", name,
                condition ? "PASS" : "FAIL");
    if (condition)
        ++g_passed;
    else
        ++g_failed;
}

bool NearlyEqual(float a, float b, float tolerance = 1e-4f)
{
    return std::fabs(a - b) <= tolerance;
}

struct Bounds
{
    float minimum[3];
    float maximum[3];
};

// Authored Jupiter bounds from the live J2A packet log and the J1 geometry
// bounds log (same payload as the M23 runtime evidence).
constexpr Bounds kBounds[4] = {
    {{-58.794f, -0.685f, 195.446f}, {-48.767f, 2.407f, 202.871f}},
    {{-58.789f, 2.387f, 195.447f}, {-58.696f, 2.531f, 195.540f}},
    {{-58.833f, 2.220f, 195.446f}, {-58.667f, 2.590f, 195.457f}},
    {{-58.878f, 2.172f, 195.446f}, {-58.625f, 2.637f, 195.586f}},
};

DirectX::XMFLOAT4X4 MakeGameViewProjection()
{
    const DirectX::XMVECTOR eye =
        DirectX::XMVectorSet(-44.692f, 5.143f, 196.537f, 1.0f);
    const DirectX::XMVECTOR target =
        DirectX::XMVectorSet(-53.780f, 0.861f, 199.159f, 1.0f);
    const DirectX::XMVECTOR up =
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, target, up);
    const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(60.0f), 1.6f, 0.1f, 20000.0f);
    DirectX::XMFLOAT4X4 result;
    DirectX::XMStoreFloat4x4(&result, view * projection);
    return result;
}

void CameraPosition(float out[3])
{
    out[0] = -44.692f;
    out[1] = 5.143f;
    out[2] = 196.537f;
}

void TestPlanContract()
{
    std::string reason;
    Check("plan contract accepts the four M0 visuals",
          fourvisual12::ValidatePlan12(fourvisual12::kPlan,
                                       fourvisual12::kSlotCount, reason));

    fourvisual12::SlotPlan12 duplicateVisual[4];
    std::memcpy(duplicateVisual, fourvisual12::kPlan,
                sizeof(duplicateVisual));
    duplicateVisual[3].visualId = duplicateVisual[0].visualId;
    Check("plan contract rejects a repeated visual",
          !fourvisual12::ValidatePlan12(duplicateVisual, 4, reason));

    fourvisual12::SlotPlan12 duplicateTint[4];
    std::memcpy(duplicateTint, fourvisual12::kPlan,
                sizeof(duplicateTint));
    std::memcpy(duplicateTint[2].tint, duplicateTint[0].tint,
                sizeof(duplicateTint[2].tint));
    Check("plan contract rejects a repeated tint",
          !fourvisual12::ValidatePlan12(duplicateTint, 4, reason));

    fourvisual12::SlotPlan12 repeatedSlot[4];
    std::memcpy(repeatedSlot, fourvisual12::kPlan, sizeof(repeatedSlot));
    repeatedSlot[1].ndcX = repeatedSlot[0].ndcX;
    repeatedSlot[1].ndcY = repeatedSlot[0].ndcY;
    Check("plan contract rejects a repeated NDC slot",
          !fourvisual12::ValidatePlan12(repeatedSlot, 4, reason));

    fourvisual12::SlotPlan12 badTint[4];
    std::memcpy(badTint, fourvisual12::kPlan, sizeof(badTint));
    badTint[0].tint[0] = 1.5f;
    Check("plan contract rejects an out-of-range tint",
          !fourvisual12::ValidatePlan12(badTint, 4, reason));
}

void TestTintOracle()
{
    // The GPU mask evaluator compares UNORM bytes. These are the expected
    // identity keys for the combined PNG.
    const uint8_t expected[4][4] = {
        {255, 64, 64, 255},
        {64, 255, 64, 255},
        {77, 115, 255, 255},
        {255, 230, 64, 255},
    };
    for (uint32_t i = 0; i < 4; ++i)
    {
        bool matches = true;
        for (uint32_t c = 0; c < 4; ++c)
        {
            const int byte = static_cast<int>(
                std::lround(fourvisual12::kPlan[i].tint[c] * 255.0f));
            matches = matches && byte == expected[i][c];
        }
        Check(i == 0 ? "tint bytes visual 9 are (255,64,64)"
                     : (i == 1 ? "tint bytes visual 105 are (64,255,64)"
                               : (i == 2
                                      ? "tint bytes visual 106 are (77,115,255)"
                                      : "tint bytes visual 107 are (255,230,64)")),
              matches);
    }
}

void TestStaging()
{
    const DirectX::XMFLOAT4X4 vp = MakeGameViewProjection();
    float camera[3];
    CameraPosition(camera);

    fourvisual12::StagedTransform12 staged[4];
    for (uint32_t i = 0; i < 4; ++i)
    {
        std::string reason;
        const bool built = fourvisual12::BuildStagedTransform12(
            vp, camera, kBounds[i].minimum, kBounds[i].maximum,
            fourvisual12::kPlan[i].ndcX, fourvisual12::kPlan[i].ndcY,
            staged[i], reason);

        char name[96];
        std::snprintf(name, sizeof(name),
                      "visual %u stages into its NDC slot",
                      fourvisual12::kPlan[i].visualId);
        Check(name, built);
        if (!built)
        {
            std::printf("  reason='%s'\n", reason.c_str());
            continue;
        }

        std::snprintf(name, sizeof(name),
                      "visual %u projected extent is bounded",
                      fourvisual12::kPlan[i].visualId);
        Check(name,
              staged[i].projected.MaxExtent() >=
                      fourvisual12::kTargetNdcExtent * 0.5f &&
                  staged[i].projected.MaxExtent() <=
                      fourvisual12::kTargetNdcExtent * 1.5f);

        std::snprintf(name, sizeof(name),
                      "visual %u stays inside the NDC viewport",
                      fourvisual12::kPlan[i].visualId);
        Check(name,
              staged[i].projected.minX >= -0.995f &&
                  staged[i].projected.maxX <= 0.995f &&
                  staged[i].projected.minY >= -0.995f &&
                  staged[i].projected.maxY <= 0.995f);

        std::snprintf(name, sizeof(name),
                      "visual %u projected depth is valid",
                      fourvisual12::kPlan[i].visualId);
        Check(name, staged[i].projected.minZ >= -0.001f &&
                        staged[i].projected.maxZ <= 1.001f);

        // The authored AABB centre must land exactly on the slot centre.
        DirectX::XMVECTOR center = DirectX::XMVectorSet(
            staged[i].centerWorld[0], staged[i].centerWorld[1],
            staged[i].centerWorld[2], 1.0f);
        center = DirectX::XMVector4Transform(
            center, DirectX::XMLoadFloat4x4(&vp));
        const float w = DirectX::XMVectorGetW(center);
        const float ndcX = DirectX::XMVectorGetX(center) / w;
        const float ndcY = DirectX::XMVectorGetY(center) / w;
        std::snprintf(name, sizeof(name),
                      "visual %u centre lands on the requested NDC slot",
                      fourvisual12::kPlan[i].visualId);
        Check(name, NearlyEqual(ndcX, fourvisual12::kPlan[i].ndcX, 1e-3f) &&
                        NearlyEqual(ndcY, fourvisual12::kPlan[i].ndcY,
                                    1e-3f));
    }

    for (uint32_t a = 0; a < 4; ++a)
    {
        for (uint32_t b = a + 1; b < 4; ++b)
        {
            if (!staged[a].projected.Valid() ||
                !staged[b].projected.Valid())
                continue;
            char name[96];
            std::snprintf(name, sizeof(name),
                          "visual %u and %u boxes are disjoint",
                          fourvisual12::kPlan[a].visualId,
                          fourvisual12::kPlan[b].visualId);
            Check(name, fourvisual12::BoxesDisjoint(staged[a].projected,
                                                    staged[b].projected));
        }
    }

    float minZ = 1.0f;
    float maxZ = 0.0f;
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (!staged[i].projected.Valid())
            continue;
        if (staged[i].projected.minZ < minZ)
            minZ = staged[i].projected.minZ;
        if (staged[i].projected.maxZ > maxZ)
            maxZ = staged[i].projected.maxZ;
    }
    Check("staged slots share a valid depth slab",
          minZ >= -0.001f && maxZ <= 1.001f);
    Check("staged transforms are finite",
          staged[0].scale > 0.0f && staged[1].scale > 0.0f &&
              staged[2].scale > 0.0f && staged[3].scale > 0.0f);
}

void TestStagingRejections()
{
    std::string reason;
    float camera[3];
    CameraPosition(camera);

    DirectX::XMFLOAT4X4 singular = {};
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            singular.m[row][column] = 1.0f;
    fourvisual12::StagedTransform12 staged;
    Check("singular ViewProjection is rejected",
          !fourvisual12::BuildStagedTransform12(
              singular, camera, kBounds[0].minimum, kBounds[0].maximum,
              -0.46f, 0.46f, staged, reason));

    const DirectX::XMFLOAT4X4 vp = MakeGameViewProjection();
    float degenerateMin[3] = {0.0f, 0.0f, 0.0f};
    float degenerateMax[3] = {0.0f, 0.0f, 0.0f};
    Check("zero-extent bounds are rejected",
          !fourvisual12::BuildStagedTransform12(
              vp, camera, degenerateMin, degenerateMax, -0.46f, 0.46f,
              staged, reason));

    float nanCamera[3] = {camera[0], camera[1],
                          std::numeric_limits<float>::quiet_NaN()};
    Check("non-finite camera is rejected",
          !fourvisual12::BuildStagedTransform12(
              vp, nanCamera, kBounds[0].minimum, kBounds[0].maximum,
              -0.46f, 0.46f, staged, reason));

    // A camera placed exactly on the requested unprojection ray cannot define
    // a staging direction.
    float cameraOnRay[3] = {};
    Check("slot ray can be resolved for the degenerate guard",
          fourvisual12::UnprojectNdc(vp, -0.46f, 0.46f,
                                    fourvisual12::kSlotNdcDepth,
                                    cameraOnRay));
    Check("degenerate camera ray is rejected",
          !fourvisual12::BuildStagedTransform12(
              vp, cameraOnRay, kBounds[0].minimum, kBounds[0].maximum,
              -0.46f, 0.46f, staged, reason));
}
void TestColourMasks()
{
    constexpr uint32_t width = 16;
    constexpr uint32_t height = 16;
    constexpr uint32_t rowPitch = width * 4;
    uint8_t pixels[width * height * 4];
    for (uint32_t i = 0; i < width * height; ++i)
    {
        pixels[i * 4 + 0] = 8;
        pixels[i * 4 + 1] = 13;
        pixels[i * 4 + 2] = 23;
        pixels[i * 4 + 3] = 255;
    }

    uint8_t tints[4][4] = {};
    for (uint32_t i = 0; i < 4; ++i)
        GameDx12TintBytes(fourvisual12::kPlan[i].tint, tints[i]);

    // Four 4x4 blocks, one per colour.
    const uint32_t originX[4] = {0, 12, 0, 12};
    const uint32_t originY[4] = {0, 0, 12, 12};
    for (uint32_t slot = 0; slot < 4; ++slot)
    {
        for (uint32_t y = 0; y < 4; ++y)
        {
            for (uint32_t x = 0; x < 4; ++x)
            {
                uint8_t* pixel = pixels +
                    ((originY[slot] + y) * rowPitch +
                     (originX[slot] + x) * 4);
                for (uint32_t c = 0; c < 4; ++c)
                    pixel[c] = tints[slot][c];
            }
        }
    }

    GameDx12ColorMask12 masks[4];
    for (uint32_t slot = 0; slot < 4; ++slot)
    {
        masks[slot] = EvaluateGameDx12ColorMask(
            pixels, width, height, rowPitch, tints[slot]);
        Check(slot == 0 ? "combined mask visual 9 counts its block"
                        : (slot == 1
                               ? "combined mask visual 105 counts its block"
                               : (slot == 2 ? "combined mask visual 106 counts its block"
                                            : "combined mask visual 107 counts its block")),
              masks[slot].pixels == 16);
        Check(slot == 0 ? "combined mask visual 9 bbox is exact"
                        : (slot == 1 ? "combined mask visual 105 bbox is exact"
                                     : (slot == 2 ? "combined mask visual 106 bbox is exact"
                                                  : "combined mask visual 107 bbox is exact")),
              masks[slot].minX == originX[slot] &&
                  masks[slot].minY == originY[slot] &&
                  masks[slot].maxX == originX[slot] + 3 &&
                  masks[slot].maxY == originY[slot] + 3);
    }
    for (uint32_t a = 0; a < 4; ++a)
        for (uint32_t b = a + 1; b < 4; ++b)
            Check("combined colour masks are pairwise disjoint",
                  GameDx12ColorMasksDisjoint(masks[a], masks[b]));

    uint8_t clearOnly[16 * 16 * 4];
    for (uint32_t i = 0; i < width * height; ++i)
    {
        clearOnly[i * 4 + 0] = 8;
        clearOnly[i * 4 + 1] = 13;
        clearOnly[i * 4 + 2] = 23;
        clearOnly[i * 4 + 3] = 255;
    }
    const GameDx12ColorMask12 empty = EvaluateGameDx12ColorMask(
        clearOnly, width, height, rowPitch, tints[0]);
    Check("clear-only readback fails every colour mask", !empty.HasPixels());

    // Rounding tolerance accepts a one-byte driver delta and rejects a real
    // colour change.
    uint8_t rounded = tints[0][0] == 255 ? 254 : tints[0][0] + 1;
    uint8_t nearPixel[4] = {rounded, tints[0][1], tints[0][2],
                            tints[0][3]};
    Check("one-byte UNORM rounding still matches",
          EvaluateGameDx12ColorMask(nearPixel, 1, 1, 4, tints[0]).pixels == 1);
    uint8_t farValue = tints[0][0] > 10 ? tints[0][0] - 6
                                       : tints[0][0] + 6;
    uint8_t farPixel[4] = {farValue, tints[0][1], tints[0][2],
                           tints[0][3]};
    Check("six-byte colour change does not match",
          EvaluateGameDx12ColorMask(farPixel, 1, 1, 4, tints[0]).pixels == 0);
}
} // namespace

int main()
{
    std::printf("M23_PLAN_CPU_START\n");
    TestPlanContract();
    TestTintOracle();
    TestStaging();
    TestStagingRejections();
    TestColourMasks();
    std::printf("M23_PLAN_CPU_RESULT %s passed=%d failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
