#pragma once

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

// CPU-таблица для SM 5.1: t13 остаётся исходным массивом источников.
// Olsson, Billeter, Assarsson, Clustered Deferred and Forward Shading (2012):
// https://diglib.eg.org/bitstream/handle/10.2312/EGGH.HPG12.087-096/087-096.pdf?sequence=1
namespace gamecluster12
{
struct Config
{
    uint32_t width = 0, height = 0, tileWidth = 0, tileHeight = 0, slices = 0;
    float nearDepth = 0, farDepth = 0;
    // row-major, вектор слева; clip.w равен положительной view-depth в мировых единицах.
    float viewProjection[16]{};
    // Лимит памяти задаёт владелец кадра; ноль означает отсутствие дополнительного лимита.
    uint64_t maxBytes = 0;
};
struct Light { float x, y, z, radius; };
struct Entry { uint32_t offset = 0, count = 0; };
struct Result
{
    uint32_t tilesX = 0, tilesY = 0;
    std::vector<Entry> grid;
    std::vector<uint32_t> indices;
};
static_assert(sizeof(Entry) == 8);

inline bool Enabled() noexcept
{
    const char* value = std::getenv("OGSR_DX12_GAME_CLUSTERED_LIGHTING");
    return value && std::strcmp(value, "1") == 0;
}

inline Config PerspectiveConfig(uint32_t width, uint32_t height, const float* matrix) noexcept
{
    const auto setting = [](const char* name, uint32_t fallback)
    {
        const auto* value = std::getenv(name);
        if (!value)
            return fallback;
        const auto end = value + std::strlen(value);
        uint32_t result = 0;
        const auto parsed = std::from_chars(value, end, result);
        return parsed.ec == std::errc{} && parsed.ptr == end && result ? result : fallback;
    };
    static const auto tile = setting("OGSR_DX12_GAME_CLUSTER_TILE", 64);
    static const auto slices = setting("OGSR_DX12_GAME_CLUSTER_SLICES", 24);
    Config config{width, height, tile, tile, slices};
    static const auto budgetMiB = setting("OGSR_DX12_GAME_CLUSTER_MEMORY_MIB", 16);
    config.maxBytes = uint64_t(budgetMiB) * 1024 * 1024;
    std::copy_n(matrix, 16, config.viewProjection);
    // Стандартная перспективная проекция D3D: z_clip = A*w_clip+B.
    const double norm = double(matrix[3])*matrix[3] + double(matrix[7])*matrix[7] + double(matrix[11])*matrix[11];
    if (!(std::abs(norm - 1) < 1.e-4))
        return config;
    const double a = (double(matrix[2])*matrix[3] + double(matrix[6])*matrix[7] + double(matrix[10])*matrix[11]) / norm;
    for (uint32_t row = 0; row < 3; ++row)
        if (std::abs(matrix[row * 4 + 2] - a * matrix[row * 4 + 3]) > 1.e-5)
            return config;
    const double b = matrix[14] - a * matrix[15];
    if (a > 1 && b < 0)
    {
        config.nearDepth = static_cast<float>(-b / a);
        config.farDepth = static_cast<float>(-b / (a - 1));
    }
    return config;
}

namespace detail
{
struct Bounds { uint32_t x0, x1, y0, y1, z0, z1; bool visible; };
inline double Dot(const float* m, unsigned col, const Light& l)
{ return double(l.x)*m[col] + double(l.y)*m[4+col] + double(l.z)*m[8+col] + m[12+col]; }
inline double Norm(const float* m, unsigned col)
{ return std::hypot(double(m[col]), double(m[4+col]), double(m[8+col])); }
inline uint32_t Slice(double depth, const Config& c)
{
    double v = std::log(depth / c.nearDepth) * double(c.slices) / std::log(double(c.farDepth) / c.nearDepth);
    return static_cast<uint32_t>(std::clamp(std::floor(v), 0.0, double(c.slices - 1)));
}
inline bool GetBounds(const Config& c, uint32_t nx, uint32_t ny, const Light& l, Bounds& b)
{
    if (!std::isfinite(l.x) || !std::isfinite(l.y) || !std::isfinite(l.z) ||
        !std::isfinite(l.radius) || l.radius < 0) return false;
    const auto* m = c.viewProjection;
    const auto interval = [&](unsigned col)
    {
        const double center = Dot(m, col, l);
        const double extent = l.radius * Norm(m, col);
        const double magnitude = (std::abs(double(l.x)) + l.radius) * std::abs(double(m[col])) +
            (std::abs(double(l.y)) + l.radius) * std::abs(double(m[4 + col])) +
            (std::abs(double(l.z)) + l.radius) * std::abs(double(m[8 + col])) + std::abs(double(m[12 + col]));
        // Ошибка clip dot от абсолютных слагаемых покрывает cancellation
        // при больших мировых координатах. Shader перепроверяет XY тайл
        // преобразованием той же world position, что используется в LocalFalloff.
        const double error = magnitude * (32.0 * std::numeric_limits<float>::epsilon()) +
            32.0 * std::numeric_limits<float>::min();
        return std::pair(center - extent - error, center + extent + error);
    };
    const auto [w0, w1] = interval(3);
    if (w1 < c.nearDepth || w0 > c.farDepth)
    {
        b.visible = false;
        return true;
    }
    b.visible = true;
    const auto first = Slice((std::max)(w0, double(c.nearDepth)), c);
    const auto last = Slice((std::min)(w1, double(c.farDepth)), c);
    b.z0 = first == 0 ? 0 : first - 1;
    b.z1 = (std::min)(last + 1, c.slices - 1);
    b.x0 = b.y0 = 0;
    b.x1 = nx - 1;
    b.y1 = ny - 1;
    if (w0 <= c.nearDepth)
        return true;

    const auto axis = [&](unsigned col, double sign, uint32_t pixels, uint32_t tile,
        uint32_t tiles, uint32_t& low, uint32_t& high)
    {
        const auto [a, d] = interval(col);
        const double ratios[] = {sign * a / w0, sign * d / w0, sign * a / w1, sign * d / w1};
        const auto [minIt, maxIt] = std::minmax_element(std::begin(ratios), std::end(ratios));
        const double lo = (*minIt * 0.5 + 0.5) * pixels - 2.0;
        const double hi = (*maxIt * 0.5 + 0.5) * pixels + 2.0;
        if (hi < 0 || lo >= pixels)
            return false;
        const auto index = [tile, tiles](double p)
        {
            return static_cast<uint32_t>(std::clamp(std::floor(p / tile), 0.0, double(tiles - 1)));
        };
        low = index(lo);
        high = index(hi);
        return true;
    };
    b.visible = axis(0, 1, c.width, c.tileWidth, nx, b.x0, b.x1) &&
        axis(1, -1, c.height, c.tileHeight, ny, b.y0, b.y1);
    return true;
}
}

// Транзакционный результат: при любой ошибке таблица пуста, вызывающий включает полный цикл.
inline bool Build(const Config& c, std::span<const Light> lights, Result& output) noexcept
{
    output = {};
    if (!c.width || !c.height || !c.tileWidth || !c.tileHeight || !c.slices ||
        !std::isfinite(c.nearDepth) || !std::isfinite(c.farDepth) ||
        c.nearDepth <= 0 || c.farDepth <= c.nearDepth || lights.size() > UINT32_MAX) return false;
    for (float v : c.viewProjection) if (!std::isfinite(v)) return false;
    const double wn = detail::Norm(c.viewProjection, 3);
    const double depthRatio = double(c.farDepth) / c.nearDepth;
    // Один соседний Z-срез покрывает округление log2 для этого диапазона.
    if (std::abs(wn - 1.0) > 1.e-4 || c.slices > 1024 || depthRatio < 2.0 ||
        depthRatio > 0x1p40 || c.nearDepth < std::numeric_limits<float>::min()) return false;
    const uint64_t nx = (uint64_t(c.width)+c.tileWidth-1)/c.tileWidth;
    const uint64_t ny = (uint64_t(c.height)+c.tileHeight-1)/c.tileHeight;
    if (nx*ny > UINT32_MAX || nx*ny*c.slices > UINT32_MAX) return false;
    const uint64_t total = nx*ny*c.slices;
    if (c.maxBytes && total * sizeof(Entry) > c.maxBytes) return false;
    try
    {
        Result next; next.tilesX = uint32_t(nx); next.tilesY = uint32_t(ny);
        next.grid.resize(size_t(total));
        std::vector<detail::Bounds> bounds(lights.size());
        for (size_t i = 0; i < lights.size(); ++i)
        {
            auto& b = bounds[i];
            if (!detail::GetBounds(c, next.tilesX, next.tilesY, lights[i], b)) return false;
            if (!b.visible) continue;
            for (uint32_t z=b.z0; z<=b.z1; ++z)
                for (uint32_t y=b.y0; y<=b.y1; ++y)
                    for (uint32_t x=b.x0; x<=b.x1; ++x)
                    {
                        auto& e = next.grid[(uint64_t(z)*ny+y)*nx+x];
                        if (e.count == UINT32_MAX) return false;
                        ++e.count;
                    }
        }
        uint64_t sum = 0;
        for (auto& e : next.grid)
        {
            e.offset = uint32_t(sum); sum += e.count;
            if (sum > UINT32_MAX || (c.maxBytes && total*sizeof(Entry)+sum*sizeof(uint32_t) > c.maxBytes)) return false;
            e.count = 0;
        }
        next.indices.resize(size_t(sum));
        for (size_t i=0; i<bounds.size(); ++i)
        {
            const auto& b = bounds[i]; if (!b.visible) continue;
            for (uint32_t z=b.z0; z<=b.z1; ++z)
                for (uint32_t y=b.y0; y<=b.y1; ++y)
                    for (uint32_t x=b.x0; x<=b.x1; ++x)
                    {
                        auto& e = next.grid[(uint64_t(z)*ny+y)*nx+x];
                        next.indices[size_t(e.offset)+e.count++] = uint32_t(i);
                    }
        }
        output = std::move(next); return true;
    }
    catch (...) { output = {}; return false; }
}
}
