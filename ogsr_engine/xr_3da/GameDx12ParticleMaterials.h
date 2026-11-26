#pragma once
#include "GameDx12ParticleDrawPlan.h"
#include <algorithm>

namespace gameparticle12
{
struct ClassicMaterialSource
{
    uint32_t blendId = UINT32_MAX;
    uint32_t priority = UINT32_MAX;
    bool clamp = false;
    bool strictSorting = false;
};
inline constexpr uint32_t MaxParticleTextures = 128;
inline constexpr uint64_t MaxParticleTextureBytes = 32ull * 1024 * 1024;
struct MaterialBinding
{
    ClassicMaterialSource source;
    uint32_t texture = UINT32_MAX;
};
struct MaterialInventory
{
    uint64_t session = 0, epoch = 0, frame = 0;
    std::vector<MaterialBinding> bindings;
    std::vector<std::string> names;
};
// Conservative subset of the existing VFS DDS loader's path grammar.
// Do not guess aliases, multi-texture lists, or virtual textures.
inline std::string ParticleTextureName(const std::string& name)
{
    if (!ValidName(name) || name.size() >= 128 || name.front() == '\\' ||
        name.back() == '\\' || name.find("..") != std::string::npos ||
        name.find("\\\\") != std::string::npos)
        return {};
    std::string result;
    for (char c : name)
    {
        if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '#' || c == '\\')) return {};
        result.push_back(c);
    }
    return result;
}
// Resolver reads original material metadata on the owner. This function owns
// its results; no renderer pointer crosses into a future GPU draw packet.
template<class Resolve>
inline bool BuildMaterialInventory(const Snapshot& s, Resolve&& resolve, MaterialInventory& out)
{
    out = {};
    if (!s.session || !s.epoch || !ValidLayout(s)) return false;
    MaterialInventory inv;
    inv.session = s.session; inv.epoch = s.epoch; inv.frame = s.frame;
    inv.bindings.resize(s.emitters.size());
    for (size_t i = 0; i < s.emitters.size(); ++i)
    {
        const auto& e = s.emitters[i];
        ClassicMaterialSource source;
        auto name = ParticleTextureName(e.texture);
        if (name.empty() || !resolve(e.shader, source) ||
            !FindClassicState(source.blendId) || source.priority > 3) continue;
        auto found = std::find(inv.names.begin(), inv.names.end(), name);
        uint32_t index = static_cast<uint32_t>(found - inv.names.begin());
        if (found == inv.names.end())
        {
            if (inv.names.size() >= MaxParticleTextures) continue;
            inv.names.push_back(std::move(name));
        }
        inv.bindings[i] = {source, index};
    }
    out = std::move(inv);
    return true;
}
// Decoded mask is indexed by the inventory names, not by emitter. Only a
// real decoded DDS is eligible; GPU residency still needs a separate gate.
inline bool BuildDecodedDrawPlan(const Prepared& p, const MaterialInventory& inv,
    std::span<const uint8_t> decoded, DrawPlan& out)
{
    out = {};
    if (inv.names.size() > MaxParticleTextures || inv.bindings.size() > MaxEmitters ||
        decoded.size() != inv.names.size()) return false;
    for (auto d : decoded) if (d > 1) return false;
    std::vector<DrawMaterial> materials(inv.bindings.size());
    for (size_t i = 0; i < inv.bindings.size(); ++i)
    {
        const auto& b = inv.bindings[i];
        if (b.texture == UINT32_MAX) continue;
        if (b.texture >= decoded.size() || !FindClassicState(b.source.blendId) ||
            b.source.priority > 3) return false;
        if (decoded[b.texture]) materials[i].blendId = b.source.blendId;
    }
    return BuildDrawPlan(p, inv.session, inv.epoch, inv.frame, materials, out);
}
}
