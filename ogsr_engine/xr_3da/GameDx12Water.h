#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include <DirectXMath.h>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace gamewater12
{
inline bool Enabled(const char* name) noexcept
{
    const char* value = std::getenv(name);
    return value != nullptr && std::strcmp(value, "1") == 0;
}

inline bool WaterEnabled() noexcept
{
    static const bool enabled = Enabled("OGSR_DX12_GAME_WATER");
    return enabled;
}

inline bool PuddlesEnabled() noexcept
{
    static const bool enabled = Enabled("OGSR_DX12_GAME_PUDDLES");
    return enabled;
}

inline constexpr char WaterNormal[] = "fx\\water_normal";

struct Frame
{
    DirectX::XMFLOAT4 weather{}; // Время, смещение ветра по XZ, плотность дождя.
    DirectX::XMFLOAT3 sky{}; // Линейный цвет текущей погоды.
    float wetness = 0.0f;
    bool valid = false;
};

struct Puddle
{
    DirectX::XMFLOAT4X4 world;
    float height;
    float radius;
};

// Копия уже разобранных R4 данных активного уровня на потоке владельца.
std::vector<Puddle> CaptureLevelPuddles();
}

#endif
