#pragma once

#include <cstdint>
#include <filesystem>

// Writes a 32-bit uncompressed BMP. input is tightly packed top-down RGBA8;
// BMP rows are written bottom-up as BGRA with a zero alpha channel.
bool WriteBmp32(
    const std::filesystem::path& path,
    uint32_t width,
    uint32_t height,
    const uint8_t* rgba);
