#pragma once
#include "dx12renderprep.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

// Внутренние чистые операции: доступны CPU-тестам без расширения production API.
namespace renderprep12::detail {
inline uint32_t PacketCount(size_t count) {
    if (count>UINT32_MAX) throw std::overflow_error("ordering packet count overflow");
    return static_cast<uint32_t>(count);
}
inline double Depth(double depth) {
    if (!std::isfinite(depth)) throw std::invalid_argument("nonfinite ordering depth");
    return depth==0 ? 0.0 : depth;
}
struct OrderKey {
    DrawCategory12 category{};
    Resource12 pso{}, material{}, geometry{};
    uint64_t object{};
    uint32_t source{}, packet{};
    double depth{};
};
inline bool Less(const OrderKey& a, const OrderKey& b) {
    if (a.category!=b.category) return a.category<b.category;
    if (a.category==DrawCategory12::Hud) return a.source<b.source;
    if (a.category==DrawCategory12::Opaque) {
        const auto ak=std::tie(a.pso.id,a.pso.generation,a.material.id,a.material.generation,a.geometry.id,a.geometry.generation);
        const auto bk=std::tie(b.pso.id,b.pso.generation,b.material.id,b.material.generation,b.geometry.id,b.geometry.generation);
        if (ak!=bk) return ak<bk;
    } else if (a.depth!=b.depth) return a.depth>b.depth;
    return std::tie(a.object,a.source)<std::tie(b.object,b.source);
}
}
