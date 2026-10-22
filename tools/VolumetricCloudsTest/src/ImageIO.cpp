#include "ImageIO.h"

#include <cstdio>
#include <fstream>
#include <vector>

namespace
{
#pragma pack(push, 1)
struct BmpFileHeader
{
    uint16_t type = 0x4d42; // 'BM'
    uint32_t size = 0;
    uint16_t reserved1 = 0;
    uint16_t reserved2 = 0;
    uint32_t offBits = 0;
};

struct BmpInfoHeader
{
    uint32_t size = 40;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t planes = 1;
    uint16_t bitCount = 32;
    uint32_t compression = 0;
    uint32_t sizeImage = 0;
    int32_t xPelsPerMeter = 0;
    int32_t yPelsPerMeter = 0;
    uint32_t clrUsed = 0;
    uint32_t clrImportant = 0;
};
#pragma pack(pop)
} // namespace

bool WriteBmp32(
    const std::filesystem::path& path,
    uint32_t width,
    uint32_t height,
    const uint8_t* rgba)
{
    if (!rgba || width == 0 || height == 0)
        return false;

    const uint32_t rowBytes = width * 4;
    const uint32_t imageBytes = rowBytes * height;

    BmpFileHeader file{};
    file.size = sizeof(BmpFileHeader) + sizeof(BmpInfoHeader) + imageBytes;
    file.offBits = sizeof(BmpFileHeader) + sizeof(BmpInfoHeader);

    BmpInfoHeader info{};
    info.width = static_cast<int32_t>(width);
    info.height = static_cast<int32_t>(height); // positive: bottom-up
    info.sizeImage = imageBytes;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));

    std::vector<uint8_t> row(rowBytes);
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint8_t* source = rgba + static_cast<size_t>(height - 1 - y) * rowBytes;
        for (uint32_t x = 0; x < width; ++x)
        {
            row[x * 4 + 0] = source[x * 4 + 2]; // B
            row[x * 4 + 1] = source[x * 4 + 1]; // G
            row[x * 4 + 2] = source[x * 4 + 0]; // R
            row[x * 4 + 3] = 0;                 // reserved for BMP alpha
        }
        out.write(reinterpret_cast<const char*>(row.data()), rowBytes);
    }

    return out.good();
}
