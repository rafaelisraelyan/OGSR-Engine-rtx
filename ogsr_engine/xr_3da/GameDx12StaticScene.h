#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12AuxGeometry.h"
#include "GameDx12J2ABatch.h"
#include "GameDx12Lighting.h"
#include "GameDx12LocalShadowAllocator.h"
#include "GameDx12NativeFrame.h"
#include "GameDx12Surface.h"
#include "GameDx12Water.h"
#include "dx12xrayasset.h"
#include "dx12xraylevelgeometry.h"

#include <DirectXMath.h>

#include <array>
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
    uint32_t hemiTextureIndex = kNoTexture;
    std::array<uint32_t, 5> terrainTextures{}; // Маска и каналы R/G/B/A.
    float terrainScale = 0.0f;
    bool terrainBaseHemi = false;
    gameaux12::SurfaceMode surface = gameaux12::SurfaceMode::Auto;
    bool wallmarkClamp = false;
    gameaux12::TransparentMaterial transparency = gameaux12::TransparentMaterial::None;
    bool hasOverride = false;
    bool usesHemiLightmap = false;
    bool checkAlphaCoverage = false;
};

class GameDx12StaticScene12
{
public:
    static constexpr DWORD kFenceTimeoutMs = 30000;
    // Дескриптор 0 занят null-SRV (материал без текстуры).
    static constexpr uint32_t kNullDescriptor = 0;

    struct SurfaceStats
    {
        uint32_t opaque = 0;
        uint32_t cutout = 0;
        uint32_t overlayOpaque = 0;
        uint32_t overlayCutout = 0;
        uint32_t overlayBlend = 0;
        uint32_t overlayMultiply = 0;
        uint32_t translucent = 0;
    };

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
        uint32_t material_overrides = 0;
        uint32_t material_missing_srv = 0;
        uint32_t unsupported_materials = 0;
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
        uint32_t sunRigidDraws = 0;
        uint32_t sunSkinnedDraws = 0;
        uint32_t localShadowMaps = 0;
        uint32_t localShadowDraws = 0;
        SurfaceStats surfaces;
        uint32_t waterDraws = 0;
        uint32_t puddleDraws = 0;
        uint32_t staticShadowDraws = 0;
        uint32_t dynamicShadowDraws = 0;
        uint32_t spotShadowMaps = 0;
        uint32_t pointShadowFaces = 0;
        uint32_t shadowedLights = 0;
        uint32_t sceneCopies = 0;
        uint32_t commandLists = 1;
        std::array<uint64_t, game_shadow12::MaxLocalSlots> shadowLightIds{};
        std::array<uint32_t, game_shadow12::MaxLocalSlots> shadowLightFirstSlices{};
        double shadowCpuMs = 0.0;
        double sceneCpuMs = 0.0;
        double waterCpuMs = 0.0;
        double cull_ms = 0.0;
        double record_ms = 0.0;
        const char* grMode = "static-serial";
        double grSerialMs = 0.0;
        double grParallelMs = 0.0;
    };

    struct AuxStats
    {
        uint32_t rigidVisible = 0;
        uint32_t rigidCulled = 0;
        uint32_t rigidSubmitted = 0;
        uint32_t rigidDraws = 0;
        uint32_t animatedVisible = 0;
        uint32_t animatedCulled = 0;
        uint32_t animatedSubmitted = 0;
        uint32_t animatedDraws = 0;
        uint32_t tracedDraws = 0;
        uint32_t detailVisible = 0;
        uint32_t detailSubmitted = 0;
        uint32_t detailDraws = 0;
        uint32_t detailCulled = 0;
        uint32_t detailMissingMaterial = 0;
        uint32_t detailBudget = 0;
        uint32_t meshesUploaded = 0;
        uint32_t texturesUploaded = 0;
        uint32_t missingMaterial = 0;
        uint32_t unsupportedMaterial = 0;
        SurfaceStats surfaces;
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

    bool RecordNativeFrame(GameDx12NativeFrame12::Recording recording,
                           const DirectX::XMFLOAT4X4& logicalViewProjection,
                           const float cameraPosition[3], const gameaux12::Frame& extras,
                           const gamelight12::FrameSnapshot& lighting,
                           const gamewater12::Frame& water,
                           FrameStats12& stats, AuxStats& auxStats,
                           std::string& reason);

    bool RegisterMeshes(const GameDx12StaticSceneOwner12& owner,
                        std::span<const gameaux12::Mesh* const> sources,
                        std::vector<uint32_t>& handles, std::string& reason);
    void SetPuddles(std::vector<gamewater12::Puddle> puddles) { m_puddles.swap(puddles); }
    const AuxStats& AuxResources() const noexcept { return m_auxResources; }
    uint64_t TextureFootprintBytes(std::span<const uint32_t> meshes) const;

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
        uint32_t shaderId = 0;
        uint32_t descriptor = kNullDescriptor;
        gameaux12::SurfaceMode surface = gameaux12::SurfaceMode::Opaque;
        bool wallmarkClamp = false;
        gameaux12::TransparentMaterial transparency = gameaux12::TransparentMaterial::None;
        // 1 = TEXCOORD0 @28 (classic), 2 = TEXCOORD0 @24, 3 = flora с мировой
        // матрицей из OGF_TREEDEF2.
        uint8_t layout = 1;
        uint8_t traceSlot = UINT8_MAX;
        bool vertexHemi = false;
        uint32_t hemiDescriptor = kNullDescriptor;
        uint32_t terrainSet = 0;
        float terrainScale = 0.0f;
        bool terrainBaseHemi = false;
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

    struct ShadowCasterNode
    {
        float minimum[3]{};
        float maximum[3]{};
        uint32_t first = 0;
        uint32_t count = 0;
        uint32_t left = 0;
        uint32_t right = 0;
    };

    struct ShadowCasterTree
    {
        std::vector<uint32_t> draws;
        std::vector<ShadowCasterNode> nodes;
    };

    struct ShadowObjectPacket
    {
        uint32_t objectIndex;
        uint32_t meshIndex;
        bool operator==(const ShadowObjectPacket&) const = default;
    };

    struct ShadowPreparation
    {
        std::array<std::vector<uint32_t>, 2> statics;
        std::vector<ShadowObjectPacket> objects;
        bool operator==(const ShadowPreparation&) const = default;
    };

    struct FramePreparation
    {
        std::vector<uint8_t> statics;
        std::vector<uint8_t> objects;
        // Sun, две карты spot и шесть граней point light.
        std::array<ShadowPreparation, game_shadow12::MaxViews> shadows;
        bool operator==(const FramePreparation&) const = default;
        void Clear();
    };

    FramePreparation m_preparedFrame;
    FramePreparation m_referenceFrame;
    std::vector<uint32_t> m_preparationParticipants;

    struct Buffer12
    {
        ComPtr<ID3D12Resource> resource;
        uint64_t bytes = 0;
    };

    struct TextureGpu12
    {
        ComPtr<ID3D12Resource> image;
        ComPtr<ID3D12Resource> upload;
        uint64_t footprintBytes = 0;
        DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
        bool alphaTest = false;
    };

    struct AuxMesh
    {
        D3D12_VERTEX_BUFFER_VIEW vb{};
        D3D12_INDEX_BUFFER_VIEW ib{};
        uint32_t descriptor = kNullDescriptor;
        uint32_t indices = 0;
        uint32_t allocation = 0;
        uint32_t vbOffset = 0;
        uint32_t ibOffset = 0;
        uint32_t requiredBones = 0;
        float radius = 0.0f;
        gameaux12::SurfaceMode surface = gameaux12::SurfaceMode::Opaque;
        bool wallmarkClamp = false;
        gameaux12::TransparentMaterial transparency = gameaux12::TransparentMaterial::None;
        bool skinned = false;
    };

    struct DeferredPipelines
    {
        enum Slot : uint32_t { Solid, Blend, Multiply, Translucent, Count };
        std::array<ComPtr<ID3D12PipelineState>, Count> states;
    };

    enum class VisualTraceState : uint8_t { Initial, Culled, Submitted };
    struct VisualTrace
    {
        VisualTraceState state = VisualTraceState::Initial;
        uint8_t messages = 0;
    };

    bool Fail(std::string& reason, const char* text);
    bool Fail(std::string& reason, const std::string& text);
    void BuildShadowCasterTrees();
    uint32_t BuildShadowCasterNode(ShadowCasterTree& tree, uint32_t first, uint32_t count);
    bool CreatePipeline(ID3D12Device* device, std::string& reason);
    bool CreateSunShadowPipeline(ID3D12Device* device, std::string& reason);
    bool CreateFrameConstants(ID3D12Device* device, std::string& reason);
    bool CreateSunShadow(ID3D12Device* device, std::string& reason);
    bool CreateLocalShadow(ID3D12Device* device, std::string& reason);
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
    std::array<ShadowCasterTree, 2> m_shadowCasters;
    std::unordered_map<std::string, uint32_t> m_textureKeys;
    std::vector<ComPtr<ID3D12Resource>> m_auxGeometry;
    std::vector<AuxMesh> m_auxMeshes;
    AuxStats m_auxResources;
    std::array<VisualTrace, 2> m_visualTrace;
    uint64_t m_visualTraceFrame = 0;
    uint8_t m_auxTraceMessages = 0;
    // Копия метаданных пулов нужна для stride/смещений на всё время жизни
    // сцены; байты level.geom после загрузки не удерживаются.
    xraylevel12::LevelPools m_poolRefs;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    // Вариант для раскладки, где TEXCOORD0 лежит по смещению 24.
    ComPtr<ID3D12PipelineState> m_pipelineUv24;
    ComPtr<ID3D12PipelineState> m_pipelineSkeletal;
    ComPtr<ID3D12PipelineState> m_pipelineDetail;
    ComPtr<ID3D12PipelineState> m_pipelinePuddle;
    std::vector<gamewater12::Puddle> m_puddles;
    // classic, uv@24 (включая flora), skeletal
    std::array<DeferredPipelines, 3> m_deferredPipelines;
    std::array<ComPtr<ID3D12PipelineState>, 2> m_sunShadowPipelines;
    ComPtr<ID3D12PipelineState> m_sunShadowAuxPipeline;
    ComPtr<ID3D12Resource> m_sunShadow;
    ComPtr<ID3D12DescriptorHeap> m_sunShadowDsv;
    uint32_t m_sunShadowResolution = 0;
    uint32_t m_sunShadowSlices = 0;
    UINT m_sunShadowDsvStride = 0;
    uint64_t m_sunShadowBytes = 0;
    ComPtr<ID3D12Resource> m_localShadow;
    ComPtr<ID3D12DescriptorHeap> m_localShadowDsv;
    UINT m_localShadowDsvStride = 0;
    uint32_t m_localShadowSlices = 0;
    uint64_t m_localShadowBytes = 0;
    game_shadow12::LocalAllocationState m_localShadowAllocation{};
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
// появления и параллельный pools.visuals материал (текстура и режим поверхности).
void GameDx12StaticScene12_ResolveMaterials(
    std::span<const uint8_t> levelBytes, const xraylevel12::LevelPools& pools,
    std::vector<std::string>& uniqueTextureNames,
    std::vector<GameDx12StaticMaterial12>& materials);

// Инвентарь объявлений вершин по фактическим данным уровня. Печатается один
// раз на уровень, чтобы неподдержанные раскладки были видны, а не скрыты.
void GameDx12StaticScene12_LogDeclarationInventory(
    const xraylevel12::LevelPools& pools);

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
