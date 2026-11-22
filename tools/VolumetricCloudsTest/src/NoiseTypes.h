#pragma once

#include <cstdint>
#include <string>
#include <sstream>

// Parameters that fully determine the generated 3D noise volume. They are
// shared by the compute dispatch and the self-test, and must be validated
// before they reach the GPU (integer periods prevent fractional lattice seams).
struct NoiseParams
{
    uint32_t resolution = 128;       // volume edge, cubic
    uint32_t seed = 1337;
    uint32_t octaves = 4;            // 1..8
    uint32_t baseFrequency = 4;      // integer lattice periods across the volume
    uint32_t lacunarity = 2;         // integer frequency multiplier per octave
    uint32_t worleyFrequency = 8;    // integer Worley cells across the volume
    float gain = 0.5f;               // fBm amplitude multiplier per octave
    float blend = 0.55f;             // 0 = Perlin, 1 = inverted Worley
    float erosion = 0.35f;           // cellular erosion of the combined field

    std::string Validate() const
    {
        if (resolution < 32 || resolution > 256)
            return "resolution must be in [32, 256]";
        if (octaves < 1 || octaves > 8)
            return "octaves must be in [1, 8]";
        if (baseFrequency < 1 || baseFrequency > 64)
            return "baseFrequency must be in [1, 64]";
        if (lacunarity < 2 || lacunarity > 4)
            return "lacunarity must be in [2, 4]";
        if (worleyFrequency < 1 || worleyFrequency > 128)
            return "worleyFrequency must be in [1, 128]";
        if (gain < 0.1f || gain > 0.9f)
            return "gain must be in [0.1, 0.9]";
        if (blend < 0.0f || blend > 1.0f)
            return "blend must be in [0, 1]";
        if (erosion < 0.0f || erosion > 1.0f)
            return "erosion must be in [0, 1]";

        // The highest lattice frequency must remain exactly representable as
        // an integer and must not exceed the volume resolution by absurd
        // amounts (aliasing / compile-time explosion).
        uint64_t frequency = baseFrequency;
        for (uint32_t i = 1; i < octaves; ++i)
        {
            frequency *= lacunarity;
            if (frequency > 512)
                return "baseFrequency * lacunarity^(octaves-1) must be <= 512";
        }
        return {};
    }

    std::string Describe() const
    {
        std::ostringstream s;
        s << resolution << "^3 seed=" << seed
          << " octaves=" << octaves
          << " baseFreq=" << baseFrequency
          << " lacunarity=" << lacunarity
          << " worleyFreq=" << worleyFrequency
          << " gain=" << gain
          << " blend=" << blend
          << " erosion=" << erosion;
        return s.str();
    }
};

inline bool operator==(const NoiseParams& a, const NoiseParams& b)
{
    return a.resolution == b.resolution && a.seed == b.seed &&
           a.octaves == b.octaves && a.baseFrequency == b.baseFrequency &&
           a.lacunarity == b.lacunarity && a.worleyFrequency == b.worleyFrequency &&
           a.gain == b.gain && a.blend == b.blend && a.erosion == b.erosion;
}

inline bool operator!=(const NoiseParams& a, const NoiseParams& b)
{
    return !(a == b);
}

// Root constants for NoiseGenerate.hlsl. 16 DWORDs = 64 bytes, 16-byte aligned
// so that it can also be uploaded as a constant buffer later without changing
// the shader declaration.
struct alignas(16) NoiseConstants
{
    uint32_t resolution;
    uint32_t seed;
    uint32_t octaves;
    uint32_t baseFrequency;

    uint32_t lacunarity;
    uint32_t worleyFrequency;
    float gain;
    float blend;

    float erosion;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;

    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t reserved5;
    uint32_t reserved6;

    static NoiseConstants From(const NoiseParams& p)
    {
        NoiseConstants c{};
        c.resolution = p.resolution;
        c.seed = p.seed;
        c.octaves = p.octaves;
        c.baseFrequency = p.baseFrequency;
        c.lacunarity = p.lacunarity;
        c.worleyFrequency = p.worleyFrequency;
        c.gain = p.gain;
        c.blend = p.blend;
        c.erosion = p.erosion;
        return c;
    }
};
static_assert(sizeof(NoiseConstants) == 64, "NoiseConstants must stay 16 DWORDs");
static_assert(sizeof(NoiseConstants) % 16 == 0, "constant buffer alignment");

// Root constants for SliceViewer.hlsl. Root constants are just DWORDs, so no
// alignas is required here (and it would only pad the Application object).
struct ViewConstants
{
    float sliceIndex = 64.0f;   // continuous [0, resolution-1]
    uint32_t channel = 2;       // 0 Perlin, 1 Worley, 2 combined
    uint32_t diagnostic = 0;    // 1 = 3x3 tiling seam inspection
    uint32_t resolution = 128;

    float time = 0.0f;
    uint32_t sliceAxis = 0;     // 0 XY, 1 XZ, 2 YZ
    uint32_t reserved1 = 0;
    uint32_t reserved2 = 0;
};
static_assert(sizeof(ViewConstants) == 32, "ViewConstants must stay 8 DWORDs");

enum class NoiseChannel : uint32_t
{
    Perlin = 0,
    Worley = 1,
    Combined = 2,
};

inline const wchar_t* ChannelName(uint32_t channel)
{
    switch (channel)
    {
    case 0: return L"Perlin fBm";
    case 1: return L"Worley F1";
    default:  return L"Perlin-Worley";
    }
}
