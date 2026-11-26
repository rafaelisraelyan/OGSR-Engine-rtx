#include "GameDx12ParticleDrawPlan.h"
#include <cassert>
#include <iostream>
#include <random>

using namespace gameparticle12;
// Build malformed and boundary cases with the actual A1 Prepared type.
struct Fixture : Prepared
{
    Fixture() { session = 1; epoch = 2; frame = 3; }
    void add(uint32_t emitter, uint32_t quads)
    {
        packets.push_back({emitter, uint32_t(indices.size()), quads * 6u});
        for (uint32_t q = 0; q < quads; ++q)
        {
            const uint32_t b = uint32_t(vertices.size());
            vertices.resize(b + 4u);
            for (uint32_t i : {0u, 1u, 2u, 3u, 2u, 1u}) indices.push_back(b + i);
        }
    }
};
int main()
{
    static_assert(DecodeParticleColor(0x12345678u) == std::array<uint8_t, 4>{0x34, 0x56, 0x78, 0x12});
    static_assert(ClassicStates[0].deferred && ClassicStates[0].depthWrite && ClassicStates[0].alphaReference == 200);
    static_assert(ClassicStates[4].src == BlendFactor::DestColor && ClassicStates[4].dst == BlendFactor::SrcColor);
    static_assert(AlphaFactor(ClassicStates[4].src) == BlendFactor::DestAlpha);
    static_assert(AlphaFactor(ClassicStates[4].dst) == BlendFactor::SrcAlpha);
    assert(!FindClassicState(6) && !FindClassicState(UINT32_MAX));
    for (uint32_t i = 1; i < 6; ++i) assert(ClassicStates[i].blend && !ClassicStates[i].depthWrite && !ClassicStates[i].deferred);
    DrawPlan plan;
    const std::vector<DrawMaterial> materials{{0}, {1}, {2}, {3}, {4}, {5}, {UINT32_MAX}};
    Fixture f;
    assert(BuildDrawPlan(f, 1, 2, 3, materials, plan) && plan.batches.empty());
    f.add(0, 4097); f.add(6, 2);
    assert(BuildDrawPlan(f, 1, 2, 3, materials, plan));
    assert(plan.batches.size() == 2 && plan.batches[0].indexCount == 24576 && plan.batches[1].firstIndex == 24576 && plan.batches[1].indexCount == 6 && plan.unresolvedQuads == 2);
    assert(!BuildDrawPlan(f, 1, 2, 4, materials, plan) && plan.batches.empty());
    assert(!BuildDrawPlan(f, 0, 2, 3, materials, plan));
    assert(!BuildDrawPlan(f, 1, 0, 3, materials, plan));
    auto bad = f; bad.indices[0] = 1;
    assert(!BuildDrawPlan(bad, 1, 2, 3, materials, plan));
    bad = f; bad.packets[1].firstIndex--;
    assert(!BuildDrawPlan(bad, 1, 2, 3, materials, plan));
    bad = f; bad.packets[1].emitter = 0;
    assert(!BuildDrawPlan(bad, 1, 2, 3, materials, plan));
    bad = f; bad.packets.pop_back();
    assert(!BuildDrawPlan(bad, 1, 2, 3, materials, plan));
    bad = f; bad.packets[1].indexCount = UINT32_MAX;
    assert(!BuildDrawPlan(bad, 1, 2, 3, materials, plan));
    Fixture full; full.add(1, DrawPlanMaxQuads);
    assert(BuildDrawPlan(full, 1, 2, 3, materials, plan) && plan.batches.size() == 8);
    full.add(2, 1);
    assert(!BuildDrawPlan(full, 1, 2, 3, materials, plan));
    Snapshot snapshot;
    snapshot.session = 1; snapshot.epoch = 2; snapshot.frame = 3;
    snapshot.emitters.push_back({"effect", "shader", "texture", 0, Cull::None, 0, 2});
    snapshot.quads.resize(2);
    std::vector<uint8_t> mask{1, 0};
    Prepared prepared;
    assert(BuildPackets(snapshot, 1, 2, mask, prepared));
    assert(BuildDrawPlan(prepared, 1, 2, 3, materials, plan));
    assert(prepared.invalidQuads == 1 && plan.batches.size() == 1 && plan.batches[0].indexCount == 6);
    std::mt19937 rng(20261126);
    for (unsigned n = 0; n < 200; ++n)
    {
        Fixture sample;
        uint32_t missing = 0, known = 0;
        for (uint32_t e = 0; e < materials.size(); ++e)
        {
            const uint32_t q = rng() % 4500u;
            if (!q) continue;
            sample.add(e, q);
            (e == 6 ? missing : known) += q;
        }
        assert(BuildDrawPlan(sample, 1, 2, 3, materials, plan));
        uint32_t drawn = 0, end = 0;
        for (const auto& batch : plan.batches)
        {
            assert(batch.firstIndex >= end && batch.indexCount > 0 && batch.indexCount <= 24576 && batch.indexCount % 6 == 0);
            assert(batch.blendId == materials[batch.emitter].blendId && batch.emitter < 6);
            drawn += batch.indexCount / 6; end = batch.firstIndex + batch.indexCount;
        }
        assert(drawn == known && plan.unresolvedQuads == missing);
    }
    std::cout << "PASS: classic states, packed color, bounded batches, malformed/stale input, 200 randomized fixtures\n";
}
