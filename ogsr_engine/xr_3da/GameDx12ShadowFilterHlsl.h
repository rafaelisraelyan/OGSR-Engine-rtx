#pragma once

// HLSL-фрагменты вставляются после SunPlaneTap и CompareLocalShadow.
// Не объявляют ресурсы и не меняют ABI; lifetime ресурсов остаётся у frame owner.
namespace game_shadow12
{
inline constexpr const char* ContinuousSunFilterHlsl = R"hlsl(
float ContinuousSunFilter(float2 uv, float compareDepth, float2 gradient, uint slice, int radius)
{
    const float2 pixel = uv * (float)SunShadowResolution - 0.5f;
    const int2 base = int2(floor(pixel));
    const float2 fraction = frac(pixel);
    float sum = 0.0f;
    [loop] for (int y = -radius; y <= radius + 1; ++y)
    {
        const float wy = y == -radius ? 1.0f - fraction.y : (y == radius + 1 ? fraction.y : 1.0f);
        [loop] for (int x = -radius; x <= radius + 1; ++x)
        {
            const float wx = x == -radius ? 1.0f - fraction.x : (x == radius + 1 ? fraction.x : 1.0f);
            sum += wx * wy * SunPlaneTap(base + int2(x, y), uv, compareDepth, gradient, slice);
        }
    }
    const float width = (float)(2 * radius + 1);
    return sum / (width * width);
}
)hlsl";

inline constexpr const char* ContinuousLocalFilterHlsl = R"hlsl(
float ContinuousLocalFilter(float3 coords, float2 gradient, uint4 address, int radius)
{
    const float resolution = (float)address.z;
    const float2 pixel = coords.xy * resolution - 0.5f;
    const int2 base = int2(floor(pixel));
    const float2 fraction = frac(pixel);
    float sum = 0.0f;
    [loop] for (int y = -radius; y <= radius + 1; ++y)
    {
        const float wy = y == -radius ? 1.0f - fraction.y : (y == radius + 1 ? fraction.y : 1.0f);
        [loop] for (int x = -radius; x <= radius + 1; ++x)
        {
            const float wx = x == -radius ? 1.0f - fraction.x : (x == radius + 1 ? fraction.x : 1.0f);
            const int2 texel = clamp(base + int2(x, y), int2(0, 0), int2(address.z - 1, address.z - 1));
            const float2 center = (float2(texel) + 0.5f) / resolution;
            const float depth = LOCAL_SHADOW_PAGE(address.x).Load(int4(texel, address.y, 0));
            sum += wx * wy * (coords.z + dot(center - coords.xy, gradient) <= depth ? 1.0f : 0.0f);
        }
    }
    const float width = (float)(2 * radius + 1);
    return sum / (width * width);
}
)hlsl";
}
