#pragma once

#include <cstddef>
#include <cstdint>

// F4.0-J1 R2A: CPU-testable readback coverage and layout contract.
//
// The offscreen color target is cleared to (0.03, 0.05, 0.09, 1.0) and
// copied as R8G8B8A8_UNORM. Any pixel different from the rounded clear value
// is produced by the diagnostic draw. Zero non-clear pixels is a failed J1
// image gate, even when the GPU fence completed successfully.

inline constexpr float kGameDx12DiagnosticClearColor[4] = {
    0.03f, 0.05f, 0.09f, 1.0f};

// 0.03*255 -> 8, 0.05*255 -> 13, 0.09*255 -> 23 for UNORM rounding.
inline constexpr uint8_t kGameDx12DiagnosticClearBytes[4] = {
    8, 13, 23, 255};

struct GameDx12ReadbackCoverage12
{
    uint64_t nonClearPixels = 0;
    uint64_t totalPixels = 0;
    uint32_t minX = 0;
    uint32_t minY = 0;
    uint32_t maxX = 0;
    uint32_t maxY = 0;

    bool HasVisiblePixels() const noexcept { return nonClearPixels != 0; }
};

// Validates the CPU readback layout before Map/memcpy. pixelBytes is the
// actual size of the destination vector.
inline bool ValidateGameDx12ReadbackLayout(
    uint32_t width, uint32_t height, uint32_t rowPitch,
    uint64_t readbackBytes, uint64_t pixelBytes) noexcept
{
    if (width == 0 || height == 0)
        return false;
    const uint64_t minimumRowPitch = static_cast<uint64_t>(width) * 4u;
    if (static_cast<uint64_t>(rowPitch) < minimumRowPitch)
        return false;
    const uint64_t expected =
        static_cast<uint64_t>(rowPitch) * static_cast<uint64_t>(height);
    return readbackBytes == expected && pixelBytes >= expected;
}

inline GameDx12ReadbackCoverage12 EvaluateGameDx12ReadbackCoverage(
    const uint8_t* pixels, uint32_t width, uint32_t height,
    uint32_t rowPitch) noexcept
{
    GameDx12ReadbackCoverage12 result;
    if (pixels == nullptr || width == 0 || height == 0 ||
        static_cast<uint64_t>(rowPitch) <
            static_cast<uint64_t>(width) * 4u)
        return result;

    result.totalPixels =
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    result.minX = width;
    result.minY = height;

    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t* row =
            pixels + static_cast<size_t>(y) * static_cast<size_t>(rowPitch);
        for (uint32_t x = 0; x < width; ++x)
        {
            const uint8_t* pixel =
                row + static_cast<size_t>(x) * 4u;
            if (pixel[0] == kGameDx12DiagnosticClearBytes[0] &&
                pixel[1] == kGameDx12DiagnosticClearBytes[1] &&
                pixel[2] == kGameDx12DiagnosticClearBytes[2] &&
                pixel[3] == kGameDx12DiagnosticClearBytes[3])
                continue;

            ++result.nonClearPixels;
            if (x < result.minX) result.minX = x;
            if (y < result.minY) result.minY = y;
            if (x > result.maxX) result.maxX = x;
            if (y > result.maxY) result.maxY = y;
        }
    }

    if (result.nonClearPixels == 0)
    {
        result.minX = 0;
        result.minY = 0;
        result.maxX = 0;
        result.maxY = 0;
    }
    return result;
}
