#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace gameui12
{
enum class Primitive : uint8_t { TriList, TriStrip, LineStrip, LineList };
enum class Domain : uint8_t { Screen, World };
enum class Cull : uint8_t { None, CW, CCW };
struct Vertex { float x, y, z; uint32_t argb; float u, v; };
struct Rect { int32_t left, top, right, bottom; };
struct Material { uint64_t id = 0, generation = 0; };
struct State
{
    Material material;
    Domain domain = Domain::Screen;
    Cull cull = Cull::None;
    bool scissorEnabled = false;
    Rect scissor{};
    uint8_t alphaRef = 0;
    bool depthTest = false;
    std::array<float, 16> transform{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};
struct Draw
{
    State state;
    Primitive primitive;
    uint32_t firstVertex, vertexCount;
};
inline uint32_t CompleteVertices(Primitive p, uint32_t n) noexcept
{
    switch (p)
    {
    case Primitive::TriList: return n - n % 3;
    case Primitive::TriStrip: return n >= 3 ? n : 0;
    case Primitive::LineStrip: return n >= 2 ? n : 0;
    case Primitive::LineList: return n - n % 2;
    }
    return 0;
}
inline Rect ClipRect(const State& s, int32_t w, int32_t h) noexcept
{
    if (w <= 0 || h <= 0) return {};
    if (!s.scissorEnabled) return {0, 0, w, h};
    const auto& r = s.scissor;
    const Rect c{std::clamp(r.left, 0, w), std::clamp(r.top, 0, h),
        std::clamp(r.right, 0, w), std::clamp(r.bottom, 0, h)};
    return c.right > c.left && c.bottom > c.top ? c : Rect{};
}
inline bool ValidState(const State& s) noexcept
{
    if (!s.material.id || !s.material.generation ||
        (s.domain != Domain::Screen && s.domain != Domain::World) ||
        (s.cull != Cull::None && s.cull != Cull::CW && s.cull != Cull::CCW)) return false;
    for (float v : s.transform) if (!std::isfinite(v)) return false;
    return true;
}
inline bool ValidVertex(const Vertex& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
        std::isfinite(v.u) && std::isfinite(v.v);
}
struct Frame
{
    uint64_t deviceEpoch = 0, serial = 0;
    int32_t width = 0, height = 0;
    std::vector<Vertex> vertices;
    std::vector<Draw> draws;
};
enum class AppendResult { Added, Empty, Invalid, AddressOverflow };
// Owner-thread producer; the sealed Frame is consumed read-only. No arbitrary
// scene quotas. 32-bit vertex addressing is an API representation limit.
inline AppendResult Append(Frame& f, const State& s, Primitive p, std::span<const Vertex> input)
{
    if (!f.deviceEpoch || !f.serial || f.width <= 0 || f.height <= 0 || !ValidState(s) ||
        static_cast<unsigned>(p) > static_cast<unsigned>(Primitive::LineList)) return AppendResult::Invalid;
    constexpr auto max = (std::numeric_limits<uint32_t>::max)();
    if (input.size() > max || f.vertices.size() > max) return AppendResult::AddressOverflow;
    const auto count = CompleteVertices(p, static_cast<uint32_t>(input.size()));
    const Rect clip = ClipRect(s, f.width, f.height);
    if (!count || clip.right <= clip.left || clip.bottom <= clip.top) return AppendResult::Empty;
    if (count > max - f.vertices.size()) return AppendResult::AddressOverflow;
    const auto data = input.first(count);
    for (const auto& v : data) if (!ValidVertex(v)) return AppendResult::Invalid;
    const auto first = static_cast<uint32_t>(f.vertices.size());
    // Commit in order; rollback vertices if allocation of the draw fails.
    f.vertices.insert(f.vertices.end(), data.begin(), data.end());
    try { f.draws.push_back({s, p, first, count}); }
    catch (...) { f.vertices.resize(first); throw; }
    return AppendResult::Added;
}
inline bool Validate(const Frame& f, uint64_t epoch, uint64_t serial) noexcept
{
    if (!epoch || !serial || f.deviceEpoch != epoch || f.serial != serial ||
        f.width <= 0 || f.height <= 0 || f.vertices.size() > (std::numeric_limits<uint32_t>::max)()) return false;
    size_t next = 0;
    for (const auto& d : f.draws)
    {
        if (!ValidState(d.state) || d.firstVertex != next || !d.vertexCount ||
            d.vertexCount != CompleteVertices(d.primitive, d.vertexCount) ||
            d.vertexCount > f.vertices.size() - next) return false;
        const auto r = ClipRect(d.state, f.width, f.height);
        if (r.right <= r.left || r.bottom <= r.top) return false;
        next += d.vertexCount;
    }
    if (next != f.vertices.size()) return false;
    for (const auto& v : f.vertices) if (!ValidVertex(v)) return false;
    return true;
}
}
