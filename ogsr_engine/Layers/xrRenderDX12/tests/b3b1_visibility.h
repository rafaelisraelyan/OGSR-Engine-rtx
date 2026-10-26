#pragma once

// F4.0-B3-B1: shared CPU rasterization model for the standalone replay test
// and the synthetic GPU acceptance test.
//
// The model implements Direct3D's top-left fill rule for pixel centers at
// (x + 0.5, y + 0.5) in a y-down screen space with CullMode = NONE. It is a
// verification tool, not an oracle: the caller must treat only pixels with a
// comfortable interior margin as a guaranteed GPU expectation.

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace b3b1
{
enum class CoverageConfidence : uint8_t
{
    GuaranteedInterior = 0,
    BoundaryOnly,
    NoSampleCoverage,
    OutsideFrustum,
    AnalysisInvalid
};

struct ClipVertex
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
};

struct ScreenTriangle
{
    double x[3] = {};
    double y[3] = {};
    double z[3] = {};
};

struct RasterResult
{
    CoverageConfidence confidence = CoverageConfidence::AnalysisInvalid;
    bool anyCoverage = false;
    bool guaranteedInterior = false;
    bool boundaryOnly = false;
    uint64_t coveredPixels = 0;
    uint64_t interiorPixels = 0;
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    bool boundsValid = false;
    // Optional per-pixel mask (1 = pixel center covered). Only valid when the
    // caller supplies a buffer large enough for width * height.
    uint8_t* mask = nullptr;
};

// A pixel center closer than this to any triangle edge is treated as a
// boundary sample and never used as a strict GPU expectation. Values are in
// screen pixels, so the margin is independent of triangle size.
constexpr double kInteriorMarginPixels = 0.01;

// The CPU model is a verification tool. A hostile or corrupt capture must not
// be able to turn a viewport or triangle into an unbounded integer loop. The
// limit is deliberately far above every real X-Ray viewport (2560x1600).
constexpr uint64_t kMaxRasterPixels = 64ull * 1024ull * 1024ull;

inline bool ValidViewport(uint32_t width, uint32_t height,
                          const uint8_t* mask) noexcept
{
    if (width == 0 || height == 0)
        return false;
    // Loop indices are signed ints. Reject dimensions whose conversion would
    // be implementation-defined/UB before any bbox conversion happens.
    if (width > static_cast<uint32_t>(INT_MAX) ||
        height > static_cast<uint32_t>(INT_MAX))
        return false;
    const uint64_t pixels = static_cast<uint64_t>(width) *
                            static_cast<uint64_t>(height);
    if (pixels > kMaxRasterPixels)
        return false;
    if (mask != nullptr && pixels > static_cast<uint64_t>(SIZE_MAX))
        return false;
    return true;
}

// Converts a finite double bbox to pixel-center range [first,last]. The
// intersection with the viewport is performed in double before any integer
// cast, so 1e50 and INT_MAX-adjacent coordinates are safe. Returns false when
// the primitive cannot touch any pixel center (fully off-screen).
inline bool PixelRange(double minValue, double maxValue, uint32_t limit,
                       int& first, int& last) noexcept
{
    const double limitValue = static_cast<double>(limit);
    if (!std::isfinite(minValue) || !std::isfinite(maxValue))
        return false;
    if (maxValue <= 0.0 || minValue >= limitValue)
        return false;
    double low = std::floor(minValue - 0.5);
    double high = std::ceil(maxValue - 0.5);
    if (!std::isfinite(low) || !std::isfinite(high))
        return false;
    if (low < 0.0)
        low = 0.0;
    if (high > static_cast<double>(limit - 1))
        high = static_cast<double>(limit - 1);
    if (low > high)
        return false;
    // Both values are finite and inside [0, INT_MAX-1].
    first = static_cast<int>(low);
    last = static_cast<int>(high);
    return true;
}

inline double EdgeFunction(double ax, double ay, double bx, double by,
                           double px, double py) noexcept
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// Direct3D top-left rule for a directed edge after the triangle has been
// normalized to positive area: edges going up (dy < 0) are left edges, and a
// horizontal edge going right (dy == 0, dx > 0) is a top edge.
inline bool IsTopLeftEdge(double ax, double ay, double bx, double by) noexcept
{
    const double dx = bx - ax;
    const double dy = by - ay;
    return (dy < 0.0) || (dy == 0.0 && dx > 0.0);
}

inline double ClipPlaneDistance(int plane, const ClipVertex& vertex) noexcept
{
    switch (plane)
    {
    case 0: return vertex.x + vertex.w; // left
    case 1: return vertex.w - vertex.x; // right
    case 2: return vertex.y + vertex.w; // bottom
    case 3: return vertex.w - vertex.y; // top
    case 4: return vertex.z;            // near
    default: return vertex.w - vertex.z; // far
    }
}

inline int ClipPolygonAgainstPlane(const ClipVertex* input, int count,
                                   int plane, ClipVertex* output) noexcept
{
    int outCount = 0;
    for (int i = 0; i < count; ++i)
    {
        const ClipVertex& a = input[i];
        const ClipVertex& b = input[(i + 1) % count];
        const double da = ClipPlaneDistance(plane, a);
        const double db = ClipPlaneDistance(plane, b);
        const bool insideA = da >= 0.0;
        const bool insideB = db >= 0.0;
        if (insideA)
        {
            if (outCount >= 16)
                return -1;
            output[outCount++] = a;
        }
        if (insideA != insideB)
        {
            const double denominator = da - db;
            if (denominator == 0.0 || !std::isfinite(denominator))
                return -1;
            const double t = da / denominator;
            ClipVertex v;
            v.x = a.x + (b.x - a.x) * t;
            v.y = a.y + (b.y - a.y) * t;
            v.z = a.z + (b.z - a.z) * t;
            v.w = a.w + (b.w - a.w) * t;
            if (outCount >= 16)
                return -1;
            output[outCount++] = v;
        }
    }
    return outCount;
}

// Clips a triangle against all six planes. Returns false when the triangle is
// fully outside, and sets invalid=true on numerical failure or buffer
// overflow (the caller must classify that as ANALYSIS_INVALID, never
// OUT_OF_FRUSTUM).
inline bool ClipTriangleToFrustum(const ClipVertex input[3],
                                  ClipVertex output[16], int& outputCount,
                                  bool& invalid) noexcept
{
    invalid = false;
    ClipVertex current[16];
    ClipVertex next[16];
    for (int i = 0; i < 3; ++i)
        current[i] = input[i];
    int count = 3;
    for (int plane = 0; plane < 6; ++plane)
    {
        count = ClipPolygonAgainstPlane(current, count, plane, next);
        if (count < 0)
        {
            invalid = true;
            return false;
        }
        if (count < 3)
            return false;
        for (int i = 0; i < count; ++i)
            current[i] = next[i];
    }
    for (int i = 0; i < count; ++i)
        output[i] = current[i];
    outputCount = count;
    return true;
}

inline void AccumulateBounds(RasterResult& result, double x, double y) noexcept
{
    if (!result.boundsValid)
    {
        result.boundsValid = true;
        result.minX = x;
        result.maxX = x;
        result.minY = y;
        result.maxY = y;
    }
    else
    {
        result.minX = std::min(result.minX, x);
        result.maxX = std::max(result.maxX, x);
        result.minY = std::min(result.minY, y);
        result.maxY = std::max(result.maxY, y);
    }
}

// Rasterizes screen-space triangles with pixel centers and the top-left rule.
inline RasterResult RasterizeTriangles(const ScreenTriangle* triangles,
                                       size_t count, uint32_t width,
                                       uint32_t height,
                                       uint8_t* mask = nullptr) noexcept
{
    RasterResult result;
    result.mask = mask;
    if (triangles == nullptr || count == 0 || !ValidViewport(width, height, mask))
        return result;

    bool sawFinitePrimitive = false;
    bool sawOnScreenPrimitive = false;
    bool analysisValid = true;
    for (size_t t = 0; t < count; ++t)
    {
        const ScreenTriangle& triangle = triangles[t];
        double ax = triangle.x[0], ay = triangle.y[0];
        double bx = triangle.x[1], by = triangle.y[1];
        double cx = triangle.x[2], cy = triangle.y[2];
        for (double value : {ax, ay, bx, by, cx, cy})
            if (!std::isfinite(value))
                analysisValid = false;
        if (!analysisValid)
            break;

        sawFinitePrimitive = true;
        const double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (!std::isfinite(area))
        {
            analysisValid = false;
            break;
        }
        if (std::fabs(area) < 1.0e-12)
            continue; // degenerate; contributes no samples

        if (area < 0.0)
        {
            std::swap(bx, cx);
            std::swap(by, cy);
        }

        const double minX = std::min({ax, bx, cx});
        const double maxX = std::max({ax, bx, cx});
        const double minY = std::min({ay, by, cy});
        const double maxY = std::max({ay, by, cy});
        int xStart = 0;
        int xEnd = 0;
        int yStart = 0;
        int yEnd = 0;
        if (!PixelRange(minX, maxX, width, xStart, xEnd) ||
            !PixelRange(minY, maxY, height, yStart, yEnd))
            continue; // fully outside the viewport; distinct from invalid data
        sawOnScreenPrimitive = true;

        const double abx = bx - ax;
        const double aby = by - ay;
        const double bcx = cx - bx;
        const double bcy = cy - by;
        const double cax = ax - cx;
        const double cay = ay - cy;
        const double lengthAB = std::sqrt(abx * abx + aby * aby);
        const double lengthBC = std::sqrt(bcx * bcx + bcy * bcy);
        const double lengthCA = std::sqrt(cax * cax + cay * cay);
        if (!std::isfinite(lengthAB) || !std::isfinite(lengthBC) ||
            !std::isfinite(lengthCA))
        {
            analysisValid = false;
            break;
        }
        const bool topLeftAB = IsTopLeftEdge(ax, ay, bx, by);
        const bool topLeftBC = IsTopLeftEdge(bx, by, cx, cy);
        const bool topLeftCA = IsTopLeftEdge(cx, cy, ax, ay);

        for (int py = yStart; py <= yEnd; ++py)
        {
            const double sampleY = static_cast<double>(py) + 0.5;
            for (int px = xStart; px <= xEnd; ++px)
            {
                const double sampleX = static_cast<double>(px) + 0.5;
                const double e0 = EdgeFunction(ax, ay, bx, by, sampleX, sampleY);
                const double e1 = EdgeFunction(bx, by, cx, cy, sampleX, sampleY);
                const double e2 = EdgeFunction(cx, cy, ax, ay, sampleX, sampleY);
                if (!std::isfinite(e0) || !std::isfinite(e1) ||
                    !std::isfinite(e2))
                {
                    analysisValid = false;
                    break;
                }
                if (e0 < 0.0 || e1 < 0.0 || e2 < 0.0)
                    continue;
                // Boundary ownership: a zero edge belongs to the triangle only
                // when it is a top or left edge.
                if (e0 == 0.0 && !topLeftAB)
                    continue;
                if (e1 == 0.0 && !topLeftBC)
                    continue;
                if (e2 == 0.0 && !topLeftCA)
                    continue;

                const double d0 = lengthAB > 0.0 ? e0 / lengthAB : 0.0;
                const double d1 = lengthBC > 0.0 ? e1 / lengthBC : 0.0;
                const double d2 = lengthCA > 0.0 ? e2 / lengthCA : 0.0;
                const double margin = std::min({d0, d1, d2});
                const bool interior = margin > kInteriorMarginPixels;

                result.anyCoverage = true;
                ++result.coveredPixels;
                if (interior)
                {
                    result.guaranteedInterior = true;
                    ++result.interiorPixels;
                }
                else
                {
                    result.boundaryOnly = true;
                }
                AccumulateBounds(result, static_cast<double>(px),
                                 static_cast<double>(py));
                if (mask != nullptr)
                    mask[static_cast<size_t>(py) * width +
                         static_cast<size_t>(px)] = 1;
            }
            if (!analysisValid)
                break;
        }
        if (!analysisValid)
            break;
    }

    if (!analysisValid)
    {
        result.confidence = CoverageConfidence::AnalysisInvalid;
        return result;
    }
    if (!sawFinitePrimitive || !sawOnScreenPrimitive)
    {
        result.confidence = CoverageConfidence::OutsideFrustum;
        return result;
    }
    if (result.guaranteedInterior)
        result.confidence = CoverageConfidence::GuaranteedInterior;
    else if (result.anyCoverage)
        result.confidence = CoverageConfidence::BoundaryOnly;
    else
        result.confidence = CoverageConfidence::NoSampleCoverage;
    return result;
}
} // namespace b3b1
