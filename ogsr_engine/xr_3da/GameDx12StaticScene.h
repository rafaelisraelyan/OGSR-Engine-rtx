#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12AuxGeometry.h"
#include "GameDx12J2ABatch.h"
#include "dx12xrayasset.h"
#include "dx12xraylevelgeometry.h"

#include <DirectXMath.h>

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// F4.1: полная статическая геометрия уровня в native DX12.
//
// Архитектура повторяет уже принятый вертикальный срез и не вводит второго
// загрузчика:
//   - байты уровня читает штатный VFS-адаптер GameDx12J2ABatch_ReadSnapshot;
//   - иерархию и листья обходит levelhierarchy12 (CollectLeaves по всем
//     секторам), то есть R4-совместимое дерево level-контейнера;
//   - геометрию описывает xraylevel12::ReadLevelPools: пулы VB/IB остаются
//     такими, какими их запекает level-компилятор, вершины не копируются и не
//     переупаковываются;
//   - GPU-слой (этот класс) загружает пулы один раз на уровень/сессию и
//     рисует поддиапазоны ровно так, как R4: FVisual::Render -> vBase/vCount,
//     iBase/iCount поверх общих VB/IB.
//
// Мировые трансформации для статики уровня не применяются: level.geom уже в
// мировых координатах, а R4 ставит identity (r__dsgraph_render.cpp:18).
// Поэтому per-instance матрицы не нужны, а инстансинг выражается повторным
// использованием одних и тех же пулов без повторной загрузки.
struct GameDx12StaticSceneOwner12
{
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE fenceEvent = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    uint64_t* fenceValue = nullptr;
};

// Разрешённый материал одного визуала. textureIndex — индекс в переданном
// списке текстур; kNoTexture означает «материал без базовой текстуры».
struct GameDx12StaticMaterial12
{
    static constexpr uint32_t kNoTexture = 0xFFFFFFFFu;
    uint32_t textureIndex = kNoTexture;
    bool alphaTest = false;
    bool haveBinding = false;
};

class GameDx12StaticScene12
{
public:
    static constexpr DWORD kFenceTimeoutMs = 30000;
    // Дескриптор 0 занят null-SRV (материал без текстуры).
    static constexpr uint32_t kNullDescriptor = 0;

    struct LoadStats12
    {
        uint32_t visuals_total = 0;
        uint32_t visuals_drawable = 0;
        uint32_t visuals_rejected = 0;
        uint32_t vertex_pools = 0;
        uint32_t index_pools = 0;
        uint32_t textures_requested = 0;
        uint32_t textures_uploaded = 0;
        uint32_t textures_failed = 0;
        uint32_t draws_without_texture = 0;
        uint32_t drawing_alpha_test = 0;
        uint64_t geometry_bytes = 0;
        uint64_t texture_bytes = 0;
        double pools_ms = 0.0;
        double textures_ms = 0.0;
    };

    struct FrameStats12
    {
        uint32_t draws = 0;
        uint32_t culled = 0;
        uint32_t total = 0;
        double cull_ms = 0.0;
        double record_ms = 0.0;
    };

    struct AuxStats
    {
        uint32_t rigidVisible = 0;
        uint32_t rigidCulled = 0;
        uint32_t rigidSubmitted = 0;
        uint32_t rigidDraws = 0;
        uint32_t detailVisible = 0;
        uint32_t detailSubmitted = 0;
        uint32_t detailDraws = 0;
        uint32_t detailCulled = 0;
        uint32_t detailMissingMaterial = 0;
        uint32_t detailBudget = 0;
        uint32_t meshesUploaded = 0;
        uint32_t texturesUploaded = 0;
        uint64_t geometryBytes = 0;
        uint64_t textureBytes = 0;
    };

    GameDx12StaticScene12() = default;
    ~GameDx12StaticScene12();
    GameDx12StaticScene12(const GameDx12StaticScene12&) = delete;
    GameDx12StaticScene12& operator=(const GameDx12StaticScene12&) = delete;

    // Транзакционная загрузка. При отказе опубликованное состояние остаётся
    // пустым; при неизвестном завершении GPU ресурсы уходят в карантин.
    bool Load(const GameDx12StaticSceneOwner12& owner, const char* levelName,
              uint64_t levelSession, uint64_t levelEpoch,
              const xraylevel12::LevelPools& pools,
              std::span<const uint8_t> geomBytes,
              const std::vector<GameDx12SceneTexture12>& textures,
              const std::vector<GameDx12StaticMaterial12>& materials,
              std::string& reason);

    bool RecordNativeFrame(ID3D12GraphicsCommandList* list, uint32_t frameSlot,
                           const DirectX::XMFLOAT4X4& logicalViewProjection,
                           const float cameraPosition[3], const gameaux12::Frame& extras,
                           FrameStats12& stats, AuxStats& auxStats,
                           std::string& reason);

    bool RegisterMeshes(const GameDx12StaticSceneOwner12& owner,
                        std::span<const gameaux12::Mesh* const> sources,
                        std::vector<uint32_t>& handles, std::string& reason);
    const AuxStats& AuxResources() const noexcept { return m_auxResources; }

    void OnLevelTeardown(uint64_t nextEpoch);
    void OnDeviceReset(uint64_t nextEpoch);
    void Shutdown(bool gpuCompletionConfirmed);

    bool GpuCompletionUnknown() const noexcept
    {
        return m_gpuUnknown || m_quarantined || m_passRecorded || m_passSubmitted;
    }

    bool IsLoaded() const noexcept { return m_loaded; }
    uint64_t LevelSession() const noexcept { return m_levelSession; }
    uint64_t LevelEpoch() const noexcept { return m_levelEpoch; }
    uint64_t Generation() const noexcept { return m_generation; }
    const std::string& LastError() const noexcept { return m_lastError; }
    const LoadStats12& Stats() const noexcept { return m_stats; }
    uint32_t SourceCount() const noexcept
    {
        return static_cast<uint32_t>(m_draws.size());
    }

private:
    struct DrawSource12
    {
        uint32_t visualId = 0;
        uint32_t vbPool = 0;
        uint32_t ibPool = 0;
        uint32_t vbase = 0;
        uint32_t vcount = 0;
        uint32_t ibase = 0;
        uint32_t icount = 0;
        uint32_t textureIndex = 0;
        bool alphaTest = false;
        // 1 = TEXCOORD0 @28 (classic), 2 = TEXCOORD0 @24, 3 = flora с мировой
        // матрицей из OGF_TREEDEF2.
        uint8_t layout = 1;
        float world[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                           0.0f, 1.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 1.0f, 0.0f,
                           0.0f, 0.0f, 0.0f, 1.0f};
        float center[3] = {0.0f, 0.0f, 0.0f};
        float radius = 0.0f;
        // Предвычисленный VBV/IBV; адреса валидны после публикации пулов.
        D3D12_VERTEX_BUFFER_VIEW vertexView{};
        D3D12_INDEX_BUFFER_VIEW indexView{};
    };

    struct Buffer12
    {
        ComPtr<ID3D12Resource> resource;
        uint64_t bytes = 0;
    };

    struct TextureGpu12
    {
        ComPtr<ID3D12Resource> image;
        ComPtr<ID3D12Resource> upload;
        bool alphaTest = false;
    };

    struct AuxMesh
    {
        D3D12_VERTEX_BUFFER_VIEW vb{};
        D3D12_INDEX_BUFFER_VIEW ib{};
        uint32_t texture = 0;
        uint32_t indices = 0;
        float radius = 0.0f;
        bool cutout = false;
    };

    bool Fail(std::string& reason, const char* text);
    bool Fail(std::string& reason, const std::string& text);
    bool CreatePipeline(ID3D12Device* device, std::string& reason);
    bool CreateFrameConstants(ID3D12Device* device, std::string& reason);
    bool CreateInstanceBuffer(ID3D12Device* device, std::string& reason);
    bool PrepareUpload(const GameDx12StaticSceneOwner12& owner,
                       std::span<const uint8_t> geomBytes,
                       const std::vector<GameDx12SceneTexture12>& textures,
                       std::string& reason);
    bool CreatePools(const GameDx12StaticSceneOwner12& owner,
                     std::span<const uint8_t> geomBytes, std::string& reason);
    bool CreateTextures(const GameDx12StaticSceneOwner12& owner,
                        const std::vector<GameDx12SceneTexture12>& textures,
                        bool append, std::string& reason);
    bool SubmitAndWait(const GameDx12StaticSceneOwner12& owner, std::string& reason);
    void ReleaseGpu();
    void QuarantineGpu() noexcept;

    std::vector<Buffer12> m_vertexPools;
    std::vector<Buffer12> m_indexPools;
    std::vector<TextureGpu12> m_textures;
    std::vector<DrawSource12> m_draws;
    std::unordered_map<std::string, uint32_t> m_textureKeys;
    std::vector<ComPtr<ID3D12Resource>> m_auxGeometry;
    std::vector<AuxMesh> m_auxMeshes;
    AuxStats m_auxResources;
    // Копия метаданных пулов нужна для stride/смещений на всё время жизни
    // сцены; байты level.geom после загрузки не удерживаются.
    xraylevel12::LevelPools m_poolRefs;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    // Вариант для раскладки, где TEXCOORD0 лежит по смещению 24.
    ComPtr<ID3D12PipelineState> m_pipelineUv24;
    ComPtr<ID3D12PipelineState> m_pipelineRigid;
    ComPtr<ID3D12PipelineState> m_pipelineDetail;
    ComPtr<ID3D12DescriptorHeap> m_textureHeap;
    ComPtr<ID3D12Resource> m_frameConstants;
    ComPtr<ID3D12Resource> m_frameTransforms;
    // Держим staging даже при раннем выходе из загрузки с неизвестным fence.
    ComPtr<ID3D12Resource> m_copyStaging;
    void* m_frameConstantsMapped = nullptr;
    void* m_frameTransformsMapped = nullptr;
    UINT m_textureDescriptorSize = 0;

    LoadStats12 m_stats{};
    uint64_t m_levelSession = 0;
    uint64_t m_levelEpoch = 0;
    uint64_t m_generation = 0;
    uint64_t m_uploadFenceValue = 0;
    bool m_loaded = false;
    bool m_uploadSubmitted = false;
    bool m_uploadConfirmed = false;
    bool m_passRecorded = false;
    bool m_passSubmitted = false;
    bool m_gpuUnknown = false;
    bool m_quarantined = false;
    bool m_cullEnabled = true;
    bool m_limitLogged = false;
    std::string m_levelName;
    std::string m_lastError;
};

// F4.1: связывает shader_id каждого визуала с первой текстурой записи
// level-шейдера (chunk 2). Возвращает уникальные имена в порядке первого
// появления и параллельный pools.visuals материал (индекс текстуры + aref).
void GameDx12StaticScene12_ResolveMaterials(
    std::span<const uint8_t> levelBytes, const xraylevel12::LevelPools& pools,
    std::vector<std::string>& uniqueTextureNames,
    std::vector<GameDx12StaticMaterial12>& materials);

// Инвентарь объявлений вершин по фактическим данным уровня. Печатается один
// раз на уровень, чтобы неподдержанные раскладки были видны, а не скрыты.
void GameDx12StaticScene12_LogDeclarationInventory(
    const xraylevel12::LevelPools& pools);

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
