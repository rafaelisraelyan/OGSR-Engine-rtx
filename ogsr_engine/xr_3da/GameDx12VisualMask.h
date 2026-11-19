#pragma once

#include <cstddef>
#include <cstdint>

// F4.0-J2B-M23: per-visual colour identity oracle for the combined readback.
//
// Each of the four staged visuals is drawn with a unique flat tint. The
// combined R8G8B8A8 readback is then scanned for those exact UNORM bytes.
// A non-zero count per colour plus a disjoint bounding box proves that all
// four real GPU draws contributed pixels to the same frame. The clear colour
// is never a valid tint, so an empty draw cannot pass this gate.

struct GameDx12ColorMask12
{
    uint64_t pixels = 0;
    uint32_t minX = 0;
    uint32_t minY = 0;
    uint32_t maxX = 0;
    uint32_t maxY = 0;

    bool HasPixels() const noexcept { return pixels != 0; }
};

inline void GameDx12TintBytes(const float tint[4],
                              uint8_t out[4]) noexcept
{
    if (tint == nullptr || out == nullptr)
        return;
    for (uint32_t c = 0; c < 4; ++c)
    {
        float value = tint[c];
        if (value < 0.0f)
            value = 0.0f;
        if (value > 1.0f)
            value = 1.0f;
        out[c] = static_cast<uint8_t>(value * 255.0f + 0.5f);
    }
}

// tolerance is a per-channel byte tolerance. It exists for driver-side UNORM
// rounding; the four tints differ by much more than two bytes.
inline GameDx12ColorMask12 EvaluateGameDx12ColorMask(
    const uint8_t* pixels, uint32_t width, uint32_t height,
    uint32_t rowPitch, const uint8_t expected[4],
    uint32_t tolerance = 2) noexcept
{
    GameDx12ColorMask12 result;
    if (pixels == nullptr || expected == nullptr || width == 0 ||
        height == 0 ||
        static_cast<uint64_t>(rowPitch) <
            static_cast<uint64_t>(width) * 4u)
        return result;

    result.minX = width;
    result.minY = height;
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t* row =
            pixels + static_cast<size_t>(y) * static_cast<size_t>(rowPitch);
        for (uint32_t x = 0; x < width; ++x)
        {
            const uint8_t* pixel = row + static_cast<size_t>(x) * 4u;
            bool match = true;
            for (uint32_t c = 0; c < 4; ++c)
            {
                const int delta = static_cast<int>(pixel[c]) -
                                  static_cast<int>(expected[c]);
                const int magnitude = delta < 0 ? -delta : delta;
                if (magnitude > static_cast<int>(tolerance))
                {
                    match = false;
                    break;
                }
            }
            if (!match)
                continue;
            ++result.pixels;
            if (x < result.minX) result.minX = x;
            if (y < result.minY) result.minY = y;
            if (x > result.maxX) result.maxX = x;
            if (y > result.maxY) result.maxY = y;
        }
    }

    if (result.pixels == 0)
    {
        result.minX = 0;
        result.minY = 0;
        result.maxX = 0;
        result.maxY = 0;
    }
    return result;
}

inline bool GameDx12ColorMasksDisjoint(const GameDx12ColorMask12& a,
                                       const GameDx12ColorMask12& b) noexcept
{
    if (!a.HasPixels() || !b.HasPixels())
        return false;
    return a.maxX < b.minX || b.maxX < a.minX ||
           a.maxY < b.minY || b.maxY < a.minY;
}
