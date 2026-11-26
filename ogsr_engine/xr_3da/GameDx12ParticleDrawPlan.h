#pragma once

#include "GameDx12ParticlePackets.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <utility>

namespace gameparticle12
{
// CPU-side contract for the classic normal-pass blend IDs. Not a GPU PSO.
enum class ClassicBlend : uint8_t { Set, Blend, Add, Mul, Mul2X, AlphaAdd };
enum class BlendFactor : uint8_t { Zero, One, SrcAlpha, InvSrcAlpha, DestColor, SrcColor, DestAlpha };
struct ClassicState
{
    BlendFactor src, dst;
    bool blend, depthWrite, deferred;
    uint8_t alphaReference;
};
inline constexpr std::array<ClassicState, 6> ClassicStates{{
    {BlendFactor::One, BlendFactor::Zero, false, true, true, 200},
    {BlendFactor::SrcAlpha, BlendFactor::InvSrcAlpha, true, false, false, 0},
    {BlendFactor::One, BlendFactor::One, true, false, false, 0},
    {BlendFactor::DestColor, BlendFactor::Zero, true, false, false, 0},
    {BlendFactor::DestColor, BlendFactor::SrcColor, true, false, false, 0},
    {BlendFactor::SrcAlpha, BlendFactor::One, true, false, false, 0}
}};
inline constexpr const ClassicState* FindClassicState(uint32_t id) noexcept
{
    return id < ClassicStates.size() ? &ClassicStates[id] : nullptr;
}
// R4 dx10StateUtils::ValidateState converts color factors for alpha.
inline constexpr BlendFactor AlphaFactor(BlendFactor factor) noexcept
{
    return factor == BlendFactor::DestColor ? BlendFactor::DestAlpha :
        factor == BlendFactor::SrcColor ? BlendFactor::SrcAlpha : factor;
}
// X-Ray color_argb layout: 0xAARRGGBB. Use R32_UINT + explicit shader decode,
// or convert to RGBA bytes before binding an R8G8B8A8_UNORM input.
inline constexpr std::array<uint8_t, 4> DecodeParticleColor(uint32_t color) noexcept
{
    return {uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color), uint8_t(color >> 24)};
}
inline constexpr uint32_t DrawBatchMaxQuads = 4096;
inline constexpr uint32_t DrawPlanMaxQuads = MaxQuads;
inline constexpr uint32_t DrawPlanMaxEmitters = MaxEmitters;
// One entry per emitter, resolved by the owner from actual material metadata.
// A missing/unknown blend ID must remain unresolved (UINT32_MAX); never guess.
struct DrawMaterial
{
    uint32_t blendId = UINT32_MAX;
};
struct DrawBatch
{
    uint32_t emitter, firstIndex, indexCount, blendId;
};
struct DrawPlan
{
    uint64_t session = 0, epoch = 0, frame = 0;
    uint32_t unresolvedQuads = 0;
    std::vector<DrawBatch> batches;
};
// Input is the original A1 Prepared (session/epoch/frame, vertices, indices, packets).
// Keep source order within the plan; production pass/visibility ordering is
// deliberately outside this contract. A false return never exposes a partial plan.
inline bool BuildDrawPlan(const Prepared& p, uint64_t session, uint64_t epoch,
    uint64_t frame, std::span<const DrawMaterial> materials, DrawPlan& output)
{
    output = {};
    if (p.session != session || p.epoch != epoch || p.frame != frame ||
        materials.size() > DrawPlanMaxEmitters || p.packets.size() > materials.size() ||
        p.vertices.size() > DrawPlanMaxQuads * 4u || p.indices.size() > DrawPlanMaxQuads * 6u ||
        p.vertices.size() % 4u || p.indices.size() % 6u ||
        p.vertices.size() / 4u != p.indices.size() / 6u)
        return false;
    constexpr auto winding = QuadIndices;
    for (std::size_t q = 0; q < p.indices.size() / 6u; ++q)
        for (std::size_t i = 0; i < winding.size(); ++i)
            if (p.indices[q * 6u + i] != q * 4u + winding[i]) return false;
    DrawPlan plan;
    plan.session = session; plan.epoch = epoch; plan.frame = frame;
    uint32_t cursor = 0, previous = 0;
    bool first = true;
    for (const auto& packet : p.packets)
    {
        if (packet.emitter >= materials.size() || (!first && packet.emitter <= previous) ||
            packet.firstIndex != cursor || !packet.indexCount || packet.indexCount % 6u ||
            packet.indexCount > p.indices.size() - cursor)
            return false;
        first = false;
        previous = packet.emitter;
        const uint32_t mode = materials[packet.emitter].blendId;
        if (!FindClassicState(mode))
            plan.unresolvedQuads += packet.indexCount / 6u;
        else
            for (uint32_t offset = 0; offset < packet.indexCount;)
            {
                const uint32_t left = packet.indexCount - offset;
                const uint32_t count = left < DrawBatchMaxQuads * 6u ? left : DrawBatchMaxQuads * 6u;
                plan.batches.push_back({packet.emitter, cursor + offset, count, mode});
                offset += count;
            }
        cursor += packet.indexCount;
    }
    if (cursor != p.indices.size()) return false;
    output = std::move(plan);
    return true;
}
}
