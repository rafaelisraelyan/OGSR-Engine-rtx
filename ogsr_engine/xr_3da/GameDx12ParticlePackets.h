#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gameparticle12
{
// Original FVF::LIT semantics: world XYZ, packed X-Ray color, UV.
// A future D3D12 input layout must decode packed color explicitly.
struct Vertex
{
    float x, y, z;
    uint32_t color;
    float u, v;
    bool operator==(const Vertex&) const = default;
};
static_assert(sizeof(Vertex) == 24);
static_assert(offsetof(Vertex, color) == 12 && offsetof(Vertex, u) == 16);
using Quad = std::array<Vertex, 4>;
inline constexpr uint32_t MaxQuads = 32768;
inline constexpr uint32_t MaxEmitters = 1024;
inline constexpr uint32_t MaxVisuals = 4096;
inline constexpr size_t MaxName = 255;
inline constexpr std::array<uint32_t, 6> QuadIndices{0, 1, 2, 3, 2, 1};

enum class Cull : uint8_t { None, CW, CCW };
struct Emitter
{
    std::string effect, shader, texture;
    uint32_t flags = 0;
    Cull cull = Cull::None;
    uint32_t first = 0, count = 0;
};
struct CaptureCounts
{
    uint32_t roots = 0, visited = 0, duplicate = 0, hud = 0, empty = 0;
    uint32_t unsupported = 0, invalid = 0, budget = 0;
    uint64_t sourceQuads = 0, truncatedQuads = 0;
};
struct Snapshot
{
    uint64_t session = 0, epoch = 0, frame = 0;
    CaptureCounts counts;
    std::vector<Emitter> emitters;
    std::vector<Quad> quads;
};

inline bool ValidName(const std::string& value)
{
    return !value.empty() && value.size() <= MaxName && value.find('\0') == std::string::npos;
}
inline bool ValidEmitter(const Emitter& e)
{
    return ValidName(e.effect) && ValidName(e.shader) && ValidName(e.texture) &&
        (e.cull == Cull::None || e.cull == Cull::CW || e.cull == Cull::CCW);
}
inline bool ValidQuad(const Quad& q)
{
    for (const auto& v : q)
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
            !std::isfinite(v.u) || !std::isfinite(v.v))
            return false;
    return true;
}
inline bool ValidLayout(const Snapshot& s)
{
    if (s.quads.size() > MaxQuads || s.emitters.size() > MaxEmitters)
        return false;
    size_t cursor = 0;
    for (const auto& e : s.emitters)
    {
        if (!ValidEmitter(e) || e.first != cursor || e.count == 0 || e.count > s.quads.size() - cursor)
            return false;
        cursor += e.count;
    }
    return cursor == s.quads.size();
}
// Caller validates bounds once before submitting. Each worker writes only its
// own byte range; vector<bool> is deliberately not used.
inline void ClassifyRange(std::span<const Quad> quads, std::span<uint8_t> valid,
    uint32_t first, uint32_t last)
{
    for (uint32_t i = first; i < last; ++i)
        valid[i] = ValidQuad(quads[i]) ? 1u : 0u;
}
struct Packet
{
    uint32_t emitter = 0, firstIndex = 0, indexCount = 0;
    bool operator==(const Packet&) const = default;
};
struct Prepared
{
    uint64_t session = 0, epoch = 0, frame = 0;
    uint32_t invalidQuads = 0;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Packet> packets;
    bool operator==(const Prepared&) const = default;
};
// Owner-thread compaction preserves producer and intra-emitter order. No
// alpha/material sorting until the real R4 pass priorities have been decoded.
inline bool BuildPackets(const Snapshot& s, uint64_t session, uint64_t epoch,
    std::span<const uint8_t> valid, Prepared& output)
{
    output = {};
    if (s.session != session || s.epoch != epoch || !ValidLayout(s) || valid.size() != s.quads.size())
        return false;
    Prepared p;
    p.session = session;
    p.epoch = epoch;
    p.frame = s.frame;
    p.vertices.reserve(s.quads.size() * 4);
    p.indices.reserve(s.quads.size() * 6);
    p.packets.reserve(s.emitters.size());
    for (uint32_t e = 0; e < s.emitters.size(); ++e)
    {
        const auto& emitter = s.emitters[e];
        const uint32_t first = static_cast<uint32_t>(p.indices.size());
        for (uint32_t i = emitter.first; i < emitter.first + emitter.count; ++i)
        {
            if (valid[i] > 1) return false;
            if (!valid[i]) { ++p.invalidQuads; continue; }
            const uint32_t base = static_cast<uint32_t>(p.vertices.size());
            p.vertices.insert(p.vertices.end(), s.quads[i].begin(), s.quads[i].end());
            for (const auto index : QuadIndices) p.indices.push_back(base + index);
        }
        const uint32_t count = static_cast<uint32_t>(p.indices.size()) - first;
        if (count) p.packets.push_back({e, first, count});
    }
    output = std::move(p);
    return true;
}
}
