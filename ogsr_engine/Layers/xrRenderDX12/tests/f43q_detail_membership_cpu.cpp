#define OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
#include "../../../xr_3da/GameDx12DetailShadowDraws.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using namespace gameaux12;

static void Check(bool value, const char* message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

static std::vector<uint32_t> Query(const std::vector<DetailShadowBound>& bounds)
{
    // Cube [-10,10]^3; deliberately non-unit plane normals.
    const std::array<DetailShadowPlane, 6> planes{{
        {{{2, 0, 0}}, 20}, {{{-2, 0, 0}}, 20},
        {{{0, 3, 0}}, 30}, {{{0, -3, 0}}, 30},
        {{{0, 0, 4}}, 40}, {{{0, 0, -4}}, 40}}};
    std::vector<uint32_t> ids;
    for (size_t i : DetailShadowMembership(bounds, planes)) ids.push_back(bounds[i].identity.ordinal);
    return ids;
}

static void ProductionDraws()
{
    using namespace DirectX;
    DetailShadowRegion region{};
    XMFLOAT4X4 projection;
    XMStoreFloat4x4(&projection, XMMatrixOrthographicOffCenterLH(10.f, 30.f, -3.f, 5.f, 2.f, 42.f));
    Check(DetailCasterRegion(projection, region), "orthographic region rejected");
    Check(std::abs(region.minX - 10.f) < 0.001f && std::abs(region.maxX - 30.f) < 0.001f &&
          std::abs(region.minZ - 2.f) < 0.001f && std::abs(region.maxZ - 42.f) < 0.001f,
          "incorrect inverse projection region");
    XMFLOAT4X4 singular{};
    Check(!DetailCasterRegion(singular, region), "singular projection accepted");

    const std::array<DetailShadowPlane, 6> planes{{
        {{{1, 0, 0}}, -10}, {{{-1, 0, 0}}, 30},
        {{{0, 1, 0}}, 3}, {{{0, -1, 0}}, 5},
        {{{0, 0, 1}}, -2}, {{{0, 0, -1}}, 42}}};
    const std::array<uint32_t, 2> meshes{17, 29};
    std::vector<DetailShadowPlacement> placements;
    std::vector<uint32_t> expected[2];
    bool beyond12 = false, offCamera = false;
    for (uint32_t i = 0; i < 2400; ++i)
    {
        const float x = static_cast<float>(static_cast<int>(i % 40));
        const float y = static_cast<float>(static_cast<int>((i / 40) % 8) - 4);
        const float z = static_cast<float>(static_cast<int>(i / 320));
        DetailShadowPlacement p{};
        p.slot = 1; p.ordinal = i; p.model = i % 2; p.radius = 0.25f;
        XMStoreFloat4x4(&p.world, XMMatrixTranslation(x, y, z));
        p.world._11 = static_cast<float>(i + 1); // Test identity, retained by the draw packer.
        placements.push_back(p);
        // Независимый oracle пересечения сферы с осевыми полупространствами.
        if (x + p.radius >= 10 && x - p.radius <= 30 && y + p.radius >= -3 &&
            y - p.radius <= 5 && z + p.radius >= 2 && z - p.radius <= 42)
        {
            expected[p.model].push_back(i + 1);
            beyond12 |= x * x + y * y + z * z > 144;
            offCamera |= x > 10; // Фикстура воображаемой камеры x <= 10.
        }
    }
    Check(expected[0].size() + expected[1].size() > 512 && beyond12 && offCamera,
          "fixture does not exercise required casters");
    const auto verify = [&](const std::vector<DetailShadowPlacement>& source, bool expectMatch, bool mutateSkip = false)
    {
        const auto bounds = DetailShadowBounds(source);
        std::vector<XMFLOAT4X4> transforms(1); // Проверяем ненулевой base offset.
        std::vector<DetailShadowDraw> draws;
        Check(AppendDetailShadowDraws(source, bounds, planes, meshes, 1024 * 1024, transforms, draws),
              "production draw packing failed");
        // Мутант output: пропуск одного caster при записи draw packets.
        if (mutateSkip && !draws.empty() && draws[0].count) --draws[0].count;
        bool matches = draws.size() == 2;
        for (size_t m = 0; m < draws.size() && m < 2; ++m)
        {
            const auto& draw = draws[m];
            matches &= draw.mesh == meshes[m] && draw.count == expected[m].size();
            if (draw.first > transforms.size() || draw.count > transforms.size() - draw.first)
            { matches = false; continue; }
            std::vector<uint32_t> actual;
            for (size_t j = draw.first; j < size_t(draw.first) + draw.count; ++j)
                actual.push_back(static_cast<uint32_t>(transforms[j]._11));
            matches &= actual == expected[m];
        }
        Check(matches == expectMatch, "oracle failed to detect draw membership difference");
    };
    verify(placements, true);
    verify(placements, false, true);
    std::reverse(placements.begin(), placements.end());
    verify(placements, true);
    const auto omitted = std::find_if(placements.begin(), placements.end(), [&](const auto& p)
        { return p.ordinal + 1 == expected[0].front(); });
    Check(omitted != placements.end(), "mutation fixture missing");
    placements.erase(omitted);
    verify(placements, false);

    const auto bounds = DetailShadowBounds(placements);
    std::vector<XMFLOAT4X4> transforms(1);
    std::vector<DetailShadowDraw> draws;
    Check(!AppendDetailShadowDraws(placements, bounds, planes, meshes, sizeof(XMFLOAT4X4), transforms, draws) &&
          transforms.size() == 1 && draws.empty(), "byte budget must fail without partial output");
}

int main()
{
    std::vector<DetailShadowBound> bounds;
    std::vector<uint32_t> expected;
    for (uint32_t i = 0; i < 1800; ++i)
    {
        const float x = static_cast<float>(static_cast<int>(i % 45) - 22);
        const float y = static_cast<float>(static_cast<int>((i / 45) % 8) - 4);
        const float z = static_cast<float>(static_cast<int>(i / 360) - 2);
        const float r = (i % 3 == 0) ? 1.f : 0.25f;
        bounds.push_back({{4, i}, {x, y, z}, r});
        // Independent axis interval oracle; no implementation plane loop.
        if (x + r >= -10.f && x - r <= 10.f && y + r >= -10.f &&
            y - r <= 10.f && z + r >= -10.f && z - r <= 10.f) expected.push_back(i);
    }
    Check(expected.size() > 512, "fixture must exceed legacy limit");
    Check(Query(bounds) == expected, "membership differs from independent oracle");
    std::reverse(bounds.begin(), bounds.end());
    Check(Query(bounds) == expected, "membership changed after input reorder");
    bounds.front().center[0] = std::numeric_limits<float>::quiet_NaN();
    const auto unknown = Query(bounds);
    Check(std::find(unknown.begin(), unknown.end(), bounds.front().identity.ordinal) != unknown.end(),
        "unknown bound must be retained conservatively");
    ProductionDraws();
    std::cout << "f43q detail membership CPU: PASS\n";
}
