#include "GameDx12ParticleMaterials.h"
#include <cassert>
#include <iostream>
using namespace gameparticle12;
int main()
{
    assert(ParticleTextureName("FX\\Smoke") == "fx\\smoke");
    for (const char* name : {"", "$user$x", "../x", "a,b", "\\x", "x\\", "a\\\\b", "a.dds", "a/b"})
        assert(ParticleTextureName(name).empty());
    Snapshot s;
    s.session = 1; s.epoch = 2; s.frame = 3;
    auto add = [&](std::string shader, std::string texture) {
        s.emitters.push_back({"effect", std::move(shader), std::move(texture), 0, Cull::None,
            static_cast<uint32_t>(s.quads.size()), 1});
        s.quads.emplace_back();
    };
    add("alpha", "FX\\Smoke"); add("add", "fx\\smoke"); add("unknown", "fx\\missing");
    add("alpha", "fx\\missing"); add("bad", "fx\\bad");
    auto resolve = [](const std::string& name, ClassicMaterialSource& out) {
        if (name == "unknown") return false;
        out = {name == "alpha" ? 1u : name == "add" ? 2u : 99u, 2, true, true};
        return true;
    };
    MaterialInventory inv;
    assert(BuildMaterialInventory(s, resolve, inv));
    assert(inv.names.size() == 2 && inv.bindings.size() == 5);
    assert(inv.bindings[0].texture == inv.bindings[1].texture);
    assert(inv.bindings[0].source.clamp && inv.bindings[0].source.strictSorting);
    assert(inv.bindings[2].texture == UINT32_MAX && inv.bindings[4].texture == UINT32_MAX);
    Prepared p; DrawPlan plan;
    assert(BuildPackets(s, 1, 2, std::vector<uint8_t>(5, 1), p));
    const std::array<uint8_t, 2> decoded{1, 0};
    assert(BuildDecodedDrawPlan(p, inv, decoded, plan));
    assert(plan.batches.size() == 2 && plan.unresolvedQuads == 3);
    assert(plan.batches[0].blendId == 1 && plan.batches[1].blendId == 2);
    ++p.frame; assert(!BuildDecodedDrawPlan(p, inv, decoded, plan) && plan.batches.empty()); --p.frame;
    assert(!BuildDecodedDrawPlan(p, inv, std::array<uint8_t, 2>{1, 2}, plan));
    inv.bindings[0].texture = 99; assert(!BuildDecodedDrawPlan(p, inv, decoded, plan));
    for (uint32_t i = 0; i < MaxParticleTextures + 1; ++i) add("alpha", "fx\\t" + std::to_string(i));
    assert(BuildMaterialInventory(s, resolve, inv) && inv.names.size() == MaxParticleTextures);
    assert(inv.bindings.back().texture == UINT32_MAX);
    ++s.emitters[0].first; assert(!BuildMaterialInventory(s, resolve, inv) && inv.names.empty());
    std::cout << "PASS: particle material inventory, DDS eligibility, identity and bounded names\n";
}
