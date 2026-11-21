#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12AuxGeometry.h"

#include <DirectXMath.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace gameaux12
{
struct DetailStats
{
    uint32_t models = 0;
    uint32_t slots = 0;
    uint32_t occupiedSlots = 0;
    uint32_t invalidEntries = 0;
    uint32_t invalidModels = 0;
    // Недекодированные слоты активного окна в текущем кадре.
    uint32_t pendingSlots = 0;
    // Точки, выбранные по палитре в готовых слотах окна. Eligible — точки,
    // успешно установленные на CForm до отсечения по дистанции и LOD.
    uint32_t source = 0;
    uint32_t eligible = 0;
    uint32_t nearExcluded = 0;
    uint32_t distanceCulled = 0;
    uint32_t lodCulled = 0;
    uint32_t noSurface = 0;
    uint32_t slopeRejected = 0;
    uint32_t candidateBudget = 0;
    uint32_t cacheBudgetSlots = 0;
    uint64_t geometryBytes = 0;
    bool enabled = false;
};

// Один набор level.details на игровую сессию. Заполняется только на потоке
// игрового кадра. На отрисовку передаются модели и матрицы по значению.
class LevelDetails
{
public:
    bool Load(std::string& reason);
    const std::vector<Mesh>& Models() const { return m_models; }
    void Collect(const float eye[3],
                 std::vector<std::vector<DirectX::XMFLOAT4X4>>& instances,
                 DetailStats& stats);

private:
    struct ModelParameters
    {
        float minScale = 0.f;
        float maxScale = 0.f;
        float radius = 0.f;
    };

    struct Slot
    {
        uint16_t yBase = 0;
        uint8_t yHeight = 0;
        bool occupied = false;
        std::array<uint8_t, 4> model{};
        std::array<uint16_t, 4> palette{};
    };

    struct Placement
    {
        float position[3];
        float up[3];
        float yaw;
        float scale;
        uint8_t model;
    };

    struct CachedSlot
    {
        std::vector<Placement> placements;
        uint32_t noSurface = 0;
        uint32_t slopeRejected = 0;
    };

    struct PlacementSettings
    {
        float density = 0.f;
        float scale = 0.f;
        float slope = 0.f;
        bool align = false;

        bool operator==(const PlacementSettings&) const = default;
    };

    struct PendingSlot
    {
        uint32_t index;
        float distanceSquared;
    };

    bool Parse(std::span<const uint8_t> bytes, std::string& reason);
    CachedSlot Decompress(uint32_t index) const;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    int32_t m_offsetX = 0;
    int32_t m_offsetZ = 0;
    DetailStats m_loadStats;
    PlacementSettings m_settings;
    std::vector<Mesh> m_models;
    std::vector<ModelParameters> m_modelParameters;
    std::vector<Slot> m_slots;
    std::unordered_map<uint32_t, CachedSlot> m_cache;
    std::vector<PendingSlot> m_pending;
    std::vector<uint32_t> m_ready;
    size_t m_cachedPlacements = 0;
};
} // namespace gameaux12

#endif
