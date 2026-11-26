#include "stdafx.h"
#include "GameDx12StaticScene.h"
#include "GameDx12J2APreparation.h"
#include "GameDx12SunCascades.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "../Layers/xrRender/xrRender_console.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>

bool GameDx12ReadR4DeferredAlphaSource(LPCSTR name, bool& blended);
bool GameDx12ReadR4TerrainSource(LPCSTR shader, LPCSTR baseTexture,
                               std::array<std::string, 4>& layerNames, float& detailScale);

namespace
{
using Clock = std::chrono::steady_clock;
using gameaux12::SurfaceMode;

constexpr uint32_t kPointShadowFaces = game_shadow12::PointFaces;
constexpr uint32_t kLocalShadowLayers = game_shadow12::LocalLayers;
constexpr uint32_t kLocalShadowSize = 1024;
static_assert(kLocalShadowLayers == 24); // LOCAL_SHADOW_LAYERS в FrameCB.

struct LocalLightGpu
{
    DirectX::XMFLOAT4 positionInverseRangeSquared;
    DirectX::XMFLOAT4 directionOuterCos;
    DirectX::XMFLOAT4 colorInnerCos;
    uint32_t type[4];
};

// Порядок полей и шаг массива повторяются в FrameCB.
struct SceneFrameConstants
{
    DirectX::XMFLOAT4X4 viewProjection;
    DirectX::XMFLOAT4 cameraPosition;
    std::array<DirectX::XMFLOAT4X4, game_shadow12::MaxSunCascades> sunShadowViewProjection;
    DirectX::XMFLOAT4 ambient;
    DirectX::XMFLOAT4 sunDirection;
    DirectX::XMFLOAT4 sunColor;
    uint32_t lightingFlags[4];
    std::array<LocalLightGpu, gamelight12::MaxLocalLights> localLights;
    DirectX::XMFLOAT4 hemiColor;
    DirectX::XMFLOAT4X4 inverseViewProjection;
    DirectX::XMFLOAT4 waterWeather;
    DirectX::XMFLOAT4 waterSky;
    std::array<DirectX::XMFLOAT4X4, kLocalShadowLayers> localShadowViewProjection;
    uint32_t diagnosticLight[4]; // Целевой GPU slot + 1, явный ID, frame slot, номер кадра.
    DirectX::XMFLOAT4 diagnosticOrigin;
    uint32_t legacyWaterSsr;
    uint32_t sunFilter;
    uint32_t sunReceiverBiasMask;
    uint32_t sunShadowResolution;
    DirectX::XMFLOAT4 sunCameraForward; // w: 1 для выбора по camera-space depth.
    std::array<DirectX::XMFLOAT4, game_shadow12::MaxSunCascades> sunCascadeData; // near, far, blend, world texel
    std::array<float, game_shadow12::MaxSunCascades> sunDepthBias; // Нормализованный receiver bias по срезам.
    uint32_t sunCasterCounts[4];
    uint32_t localShadowConfig[4]; // x: actual allocated local slice count.
};
static_assert(sizeof(LocalLightGpu) == 64);
static_assert(offsetof(LocalLightGpu, positionInverseRangeSquared) == 0);
static_assert(offsetof(LocalLightGpu, directionOuterCos) == 16);
static_assert(offsetof(LocalLightGpu, colorInnerCos) == 32);
static_assert(offsetof(LocalLightGpu, type) == 48);
static_assert(offsetof(SceneFrameConstants, sunShadowViewProjection) == 80);
static_assert(offsetof(SceneFrameConstants, lightingFlags) == 384);
static_assert(offsetof(SceneFrameConstants, localLights) == 400);
static_assert(offsetof(SceneFrameConstants, hemiColor) == 400 + sizeof(SceneFrameConstants::localLights));
static_assert(offsetof(SceneFrameConstants, localShadowViewProjection) ==
    offsetof(SceneFrameConstants, waterSky) + sizeof(DirectX::XMFLOAT4));
static_assert(offsetof(SceneFrameConstants, diagnosticLight) ==
    offsetof(SceneFrameConstants, localShadowViewProjection) + sizeof(SceneFrameConstants::localShadowViewProjection));
static_assert(offsetof(SceneFrameConstants, diagnosticOrigin) ==
    offsetof(SceneFrameConstants, diagnosticLight) + 16);
static_assert(offsetof(SceneFrameConstants, legacyWaterSsr) ==
    offsetof(SceneFrameConstants, diagnosticOrigin) + sizeof(DirectX::XMFLOAT4));
static_assert(offsetof(SceneFrameConstants, sunCameraForward) == offsetof(SceneFrameConstants, legacyWaterSsr) + 16);
static_assert(offsetof(SceneFrameConstants, sunCascadeData) == offsetof(SceneFrameConstants, sunCameraForward) + 16);
static_assert(offsetof(SceneFrameConstants, sunDepthBias) == offsetof(SceneFrameConstants, sunCascadeData) + 64);
constexpr uint64_t kConstantAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
constexpr uint64_t kFrameConstantsSize = (sizeof(SceneFrameConstants) + kConstantAlignment - 1) &
    ~(kConstantAlignment - 1);
static_assert(offsetof(SceneFrameConstants, localShadowConfig) == offsetof(SceneFrameConstants, sunCasterCounts) + 16);
static_assert(kFrameConstantsSize == 3328);
constexpr uint64_t kFrameConstantsSlots = GameDx12NativeFrame12::kFrameCount;
constexpr uint64_t kStagingBytes = 128ull * 1024ull * 1024ull;
constexpr uint64_t kStagingAlignment = 512ull;
constexpr uint32_t kMaxTextures = 16384;
constexpr uint32_t kSunShadowDescriptor = kMaxTextures + 1;
// Слой используемого света передаётся как root constant при записи карты.
constexpr uint32_t kShadowLayerRoot = 8;
constexpr uint32_t kLocalShadowSrvRoot = 9;
constexpr uint32_t kNullWaterDescriptor = kSunShadowDescriptor + 1;
constexpr uint32_t kWaterSrvCount = 3;
constexpr uint32_t kWaterDescriptorEnd = kNullWaterDescriptor +
    kWaterSrvCount * (1 + GameDx12NativeFrame12::kFrameCount);
constexpr uint32_t kLocalShadowDescriptor = kWaterDescriptorEnd;
constexpr uint32_t kHemiSrvRoot = 10;
constexpr uint32_t kTerrainSrvRoot = 11;
constexpr uint32_t kTerrainScaleRoot = 12;
constexpr uint32_t kTerrainDescriptor = kLocalShadowDescriptor + 1;
constexpr uint32_t kTerrainTextureCount = 5;
constexpr uint32_t kMaxTerrainSets = 128;
constexpr uint32_t kSceneDescriptorCount = kTerrainDescriptor +
    kTerrainTextureCount * (1 + kMaxTerrainSets);
static_assert(kSunShadowDescriptor == kMaxTextures + 1);
static_assert(kNullWaterDescriptor == kSunShadowDescriptor + 1);
static_assert(kLocalShadowDescriptor == kNullWaterDescriptor +
    kWaterSrvCount * (1 + GameDx12NativeFrame12::kFrameCount));
static_assert(kTerrainDescriptor == kLocalShadowDescriptor + 1);
static_assert(kSceneDescriptorCount > kTerrainDescriptor);
constexpr uint32_t kLightmapHemi = 1024u;
constexpr uint32_t kTerrainDetail = 2048u;
constexpr uint32_t kTerrainBaseHemi = 4096u;
constexpr uint32_t kObjectSkyHemi = 8192u;
constexpr float kSunShadowRadius = 48.0f;
// Центр ближней области приёмников в 20 м перед камерой. Глубина сохраняет
// запас для удалённых от камеры, но расположенных выше по свету caster'ов.
constexpr float kSunShadowForward = 20.0f;
constexpr uint64_t kMaxAuxGeometryBytes = 64ull * 1024 * 1024;
constexpr uint64_t kMaxAuxTextureBytes = 256ull * 1024 * 1024;
constexpr uint32_t kDetailShadowInstances = 512;
constexpr uint64_t kTransformSlotBytes =
    (gameaux12::kMaxFrameTransforms + kDetailShadowInstances) * sizeof(DirectX::XMFLOAT4X4);
static_assert(sizeof(DirectX::XMFLOAT4X4) == 4 * 4 * sizeof(float));
constexpr uint64_t kMaxTextureBytes = 16ull * 1024ull * 1024ull;
constexpr uint64_t kMaxSceneTextureBytes = 4ull * 1024ull * 1024ull * 1024ull;
constexpr const char* kFallbackTextureName = "";
constexpr uint8_t kMaxVisualTraceMessages = 12;

// Use the very same vertex range and affine transform as the draw. OGF header
// bounds may already be world-space; never transform them a second time.
bool BuildTreeWorldBounds(const xraylevel12::PoolVisual& visual,
    const xraylevel12::LevelPools& pools, std::span<const uint8_t> bytes,
    float (&center)[3], float& radius)
{
    if (visual.vb_id >= pools.vertex_pools.size() || visual.vcount == 0)
        return false;
    const auto& pool = pools.vertex_pools[visual.vb_id];
    if (!pool.supported || pool.stride < 3 * sizeof(float) ||
        visual.vbase > pool.count || visual.vcount > pool.count - visual.vbase ||
        pool.data_offset > bytes.size())
        return false;
    const uint64_t start = uint64_t{visual.vbase} * pool.stride;
    const uint64_t size = uint64_t{visual.vcount} * pool.stride;
    const uint64_t available = bytes.size() - pool.data_offset;
    if (start > available || size > available - start)
        return false;
    double minimum[3] = {(std::numeric_limits<double>::max)(), (std::numeric_limits<double>::max)(), (std::numeric_limits<double>::max)()};
    double maximum[3] = {-(std::numeric_limits<double>::max)(), -(std::numeric_limits<double>::max)(), -(std::numeric_limits<double>::max)()};
    for (uint32_t i = 0; i < visual.vcount; ++i)
    {
        float p[3];
        std::memcpy(p, bytes.data() + pool.data_offset + start + uint64_t{i} * pool.stride, sizeof(p));
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const double v = double{p[0]} * visual.world[axis] +
                double{p[1]} * visual.world[4 + axis] +
                double{p[2]} * visual.world[8 + axis] + visual.world[12 + axis];
            if (!std::isfinite(v) || std::abs(v) > (std::numeric_limits<float>::max)() / 4.0)
                return false;
            minimum[axis] = (std::min)(minimum[axis], v);
            maximum[axis] = (std::max)(maximum[axis], v);
        }
    }
    double extent[3];
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        center[axis] = static_cast<float>((minimum[axis] + maximum[axis]) * 0.5);
        extent[axis] = (std::max)(maximum[axis] - center[axis], center[axis] - minimum[axis]);
    }
    const double r = std::hypot(extent[0], extent[1], extent[2]);
    // Small allowance for float GPU transform and BVH arithmetic.
    const double scale = (std::max)({1.0, r, std::abs(double{center[0]}),
        std::abs(double{center[1]}), std::abs(double{center[2]})});
    radius = static_cast<float>(r + 1.0e-5 * scale);
    return std::isfinite(radius);
}

bool HasPackedLightmapUv(const xraylevel12::PoolVisual& visual, const xraylevel12::LevelPools& pools)
{
    if (visual.layout != xraylevel12::VertexLayout12::UvAt24 || visual.vb_id >= pools.vertex_pools.size())
        return false;
    constexpr uint8_t short2 = 6, short4 = 7, texcoord = 5;
    const auto& pool = pools.vertex_pools[visual.vb_id];
    for (uint32_t index = 0; index < pool.declaration_count; ++index)
    {
        const auto& element = pool.declaration[index];
        if (element.stream == 0 && element.method == 0 && element.usage == texcoord &&
            ((element.offset == 28 && element.type == short2 && element.usage_index == 1) ||
             (element.offset == 24 && element.type == short4 && element.usage_index == 0)))
            return true;
    }
    return false;
}

bool HasCutoutCoverage(const XrayTextureData12& dds)
{
    const bool bc1 = dds.format == DXGI_FORMAT_BC1_UNORM;
    if ((!bc1 && dds.format != DXGI_FORMAT_BC3_UNORM) || dds.mips.empty())
        return false;

    const auto& mip = dds.mips.front();
    const size_t blockBytes = bc1 ? 8u : 16u;
    const size_t blocksX = (mip.width + size_t{3}) / 4;
    const size_t blocksY = (mip.height + size_t{3}) / 4;
    if (blocksX == 0 || blocksY == 0 || blocksX > mip.rowPitch / blockBytes ||
        blocksY > mip.dataSize / mip.rowPitch ||
        mip.dataOffset > dds.data.size() || mip.dataSize > dds.data.size() - mip.dataOffset)
        return false;

    // Достаточно равномерной выборки одного mip, даже для больших DDS.
    // Нужны обе выраженные группы покрытия: нулевые и непрозрачные пиксели.
    const size_t blockCount = blocksX * blocksY;
    const size_t step = (blockCount + 4095u) / 4096u;
    size_t transparent = 0;
    size_t opaque = 0;
    size_t sampled = 0;
    for (size_t index = 0; index < blockCount; index += step)
    {
        const size_t x = index % blocksX;
        const size_t y = index / blocksX;
        const uint8_t* block = dds.data.data() + mip.dataOffset +
            y * mip.rowPitch + x * blockBytes;
        uint32_t alpha[8] = {};
        uint64_t selectors = 0;
        uint32_t selectorBits = 2;
        if (bc1)
        {
            const uint32_t a = block[0] | (uint32_t{block[1]} << 8);
            const uint32_t b = block[2] | (uint32_t{block[3]} << 8);
            alpha[0] = alpha[1] = alpha[2] = 255;
            alpha[3] = a > b ? 255 : 0;
            for (uint32_t byte = 0; byte < 4; ++byte)
                selectors |= uint64_t{block[4 + byte]} << (8 * byte);
        }
        else
        {
            alpha[0] = block[0];
            alpha[1] = block[1];
            const uint32_t steps = alpha[0] > alpha[1] ? 7 : 5;
            for (uint32_t i = 1; i < steps; ++i)
                alpha[i + 1] = ((steps - i) * alpha[0] + i * alpha[1]) / steps;
            alpha[7] = steps == 5 ? 255 : alpha[7];
            selectorBits = 3;
            for (uint32_t byte = 0; byte < 6; ++byte)
                selectors |= uint64_t{block[2 + byte]} << (8 * byte);
        }
        for (uint32_t pixel = 0; pixel < 16; ++pixel)
        {
            if (x * 4 + pixel % 4 >= mip.width || y * 4 + pixel / 4 >= mip.height)
                continue;
            const uint32_t selector = static_cast<uint32_t>(selectors >> (pixel * selectorBits)) &
                ((1u << selectorBits) - 1u);
            const uint32_t value = alpha[selector];
            transparent += value <= 16;
            opaque += value >= 200;
            ++sampled;
        }
    }
    return sampled != 0 && transparent * 20 >= sampled && opaque * 64 >= sampled &&
        (transparent + opaque) * 10 >= sampled * 9;
}

constexpr bool IsOverlay(SurfaceMode surface)
{
    return surface == SurfaceMode::OverlayOpaque || surface == SurfaceMode::OverlayCutout ||
        surface == SurfaceMode::OverlayBlend || surface == SurfaceMode::OverlayMultiply;
}

constexpr bool IsBlended(SurfaceMode surface)
{
    return surface == SurfaceMode::OverlayBlend || surface == SurfaceMode::OverlayMultiply ||
        surface == SurfaceMode::Translucent;
}

constexpr bool IsCutout(SurfaceMode surface)
{
    return surface == SurfaceMode::Cutout || surface == SurfaceMode::OverlayCutout;
}

constexpr bool CastsSunShadow(SurfaceMode surface)
{
    return surface == SurfaceMode::Opaque || surface == SurfaceMode::Cutout;
}

constexpr SurfaceMode ResolveSurface(SurfaceMode surface, bool binaryAlpha)
{
    if (surface == SurfaceMode::Auto)
        return binaryAlpha ? SurfaceMode::Cutout : SurfaceMode::Opaque;
    return surface;
}

void CountSurfaceDraw(GameDx12StaticScene12::SurfaceStats& stats, SurfaceMode surface)
{
    switch (surface)
    {
    case SurfaceMode::Opaque: ++stats.opaque; break;
    case SurfaceMode::Cutout: ++stats.cutout; break;
    case SurfaceMode::OverlayOpaque: ++stats.overlayOpaque; break;
    case SurfaceMode::OverlayCutout: ++stats.overlayCutout; break;
    case SurfaceMode::OverlayBlend: ++stats.overlayBlend; break;
    case SurfaceMode::OverlayMultiply: ++stats.overlayMultiply; break;
    case SurfaceMode::Translucent: ++stats.translucent; break;
    default: break;
    }
}

// Эти флаги передаются в MaterialCB обоих путей.
constexpr uint32_t kSampleBase = 1u;
constexpr uint32_t kClipAlpha = 2u;
constexpr uint32_t kUnlit = 4u;
constexpr uint32_t kOutputAlpha = 8u;
constexpr uint32_t kWallmarkClamp = 16u;
constexpr uint32_t kTwoSidedNormal = 32u;
constexpr uint32_t kWaterAlpha = 64u;
constexpr uint32_t kAdvancedWater = 128u;
constexpr uint32_t kPuddleMaterial = 256u;
constexpr uint32_t kVertexHemi = 512u;
// R4 WallmarksEngine вычитает этот сдвиг из проекционного _43.
constexpr float kWallmarkClipShift = 0.0001f;

double Ms(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

DirectX::XMFLOAT4X4 Transpose(const DirectX::XMFLOAT4X4& matrix)
{
    DirectX::XMFLOAT4X4 result;
    DirectX::XMStoreFloat4x4(
        &result, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&matrix)));
    return result;
}

bool SceneSunShadowsRequested()
{
    static const bool enabled = []
    {
        const char* lighting = std::getenv("OGSR_DX12_GAME_LIGHTING");
        const char* sun = std::getenv("OGSR_DX12_GAME_SUN");
        const char* shadows = std::getenv("OGSR_DX12_GAME_SHADOWS");
        const char* sunShadows = std::getenv("OGSR_DX12_GAME_SUN_SHADOWS");
        return lighting && sun && shadows && std::strcmp(lighting, "1") == 0 &&
            std::strcmp(sun, "1") == 0 && std::strcmp(shadows, "1") == 0 &&
            (!sunShadows || std::strcmp(sunShadows, "0") != 0);
    }();
    return enabled;
}

bool SceneLitDecalsEnabled()
{
    static const bool enabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_LIT_DECALS");
        return value == nullptr || std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

bool SceneDynamicSunShadowsRequested()
{
    static const bool enabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_DYNAMIC_SUN_SHADOWS");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

bool SceneDetailSunShadowsRequested()
{
    static const bool enabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_DETAIL_SUN_SHADOWS");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

bool SceneLocalShadowsRequested()
{
    static const bool enabled = []
    {
        const char* lighting = std::getenv("OGSR_DX12_GAME_LIGHTING");
        const char* lights = std::getenv("OGSR_DX12_GAME_LOCAL_LIGHTS");
        const char* shadows = std::getenv("OGSR_DX12_GAME_LOCAL_SHADOWS");
        return lighting && lights && shadows && std::strcmp(lighting, "1") == 0 &&
            std::strcmp(lights, "1") == 0 && std::strcmp(shadows, "1") == 0;
    }();
    return enabled;
}

// Experimental policies are opt-in at the engine boundary; the FR3 launcher
// enables them explicitly and exposes legacy A/B switches.
bool SceneReceiverPlaneSunRequested()
{
    static const bool enabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_RECEIVER_PLANE_SUN");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

uint32_t SceneSunFilter()
{
    static const uint32_t mode = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_SUN_FILTER");
        if (!value)
            return 0u;
        constexpr const char* names[] = {"auto", "point", "bilinear", "legacy",
            "receiverplane", "pcf3", "pcf5", "fr3"};
        for (uint32_t i = 0; i < std::size(names); ++i)
            if (std::strcmp(value, names[i]) == 0)
                return i;
        return 0u;
    }();
    return mode;
}

struct SunSettings
{
    uint32_t count;
    uint32_t resolution;
    uint32_t distance;
    uint32_t biasMask;
    const char* profile;
};

const SunSettings& SceneSunSettings()
{
    static const auto settings = []
    {
        const auto read = [](const char* name, uint32_t fallback, uint32_t min, uint32_t max)
        {
            const char* value = std::getenv(name);
            if (!value)
                return fallback;
            const auto end = value + std::strlen(value);
            uint32_t number = 0;
            const auto result = std::from_chars(value, end, number);
            return result.ec == std::errc{} && result.ptr == end && number >= min && number <= max ? number : fallback;
        };
        SunSettings result{1, game_shadow12::DefaultResolution, 0, 15, "legacy"};
        if (const char* profile = std::getenv("OGSR_DX12_GAME_SUN_PROFILE"))
        {
            if (std::strcmp(profile, "performance") == 0)
                result = {2, 1024, 100, 15, "performance"};
            else if (std::strcmp(profile, "balanced") == 0)
                result = {3, 2048, 160, 15, "balanced"};
            else if (std::strcmp(profile, "quality") == 0)
                result = {4, 2048, 160, 15, "quality"};
        }
        result.count = read("OGSR_DX12_GAME_SUN_CASCADES", result.count, 1, game_shadow12::MaxSunCascades);
        const auto resolution = read("OGSR_DX12_GAME_SUN_RESOLUTION", result.resolution, 1024, 4096);
        if (game_shadow12::ValidResolution(resolution))
            result.resolution = resolution;
        if (result.count > 1 && result.distance == 0)
            result.distance = result.count == 2 ? 100u : 160u;
        result.distance = read("OGSR_DX12_GAME_SUN_DISTANCE", result.distance, 10, 1000);
        result.biasMask = read("OGSR_DX12_GAME_SUN_BIAS_MASK", result.biasMask, 0, 15);
        return result;
    }();
    return settings;
}

uint32_t SceneLocalShadowBanksRequested()
{
    static const uint32_t banks = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_LOCAL_SHADOW_BANKS");
        if (value && value[0] >= '1' && value[0] <= '3' && value[1] == '\0')
            return static_cast<uint32_t>(value[0] - '0');
        return 1u;
    }();
    return banks;
}

bool SceneSuppressUnmappedLocalLightsRequested()
{
    static const bool enabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_SUPPRESS_UNMAPPED_LOCAL_LIGHTS");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

bool SceneStableLocalPriorityRequested()
{
    static const bool enabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_STABLE_LOCAL_PRIORITY");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

DirectX::XMFLOAT4X4 SunShadowViewProjection(const float camera[3],
    const DirectX::XMFLOAT4X4& viewProjection, const gamelight12::Vec3& sunDirection, uint32_t resolution)
{
    using namespace DirectX;
    const XMVECTOR direction = XMVector3Normalize(XMVectorSet(sunDirection.x, sunDirection.y, sunDirection.z, 0.0f));
    const XMVECTOR up = std::abs(XMVectorGetY(direction)) < 0.95f
        ? XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) : XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, direction));
    const XMVECTOR vertical = XMVector3Cross(direction, right);
    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(viewProjection._14, viewProjection._24,
        viewProjection._34, 0.0f));
    XMVECTOR center = XMVectorMultiplyAdd(forward, XMVectorReplicate(kSunShadowForward),
        XMVectorSet(camera[0], camera[1], camera[2], 1.0f));
    constexpr float diameter = 2.0f * kSunShadowRadius;
    const float texel = diameter / static_cast<float>(resolution);
    const float centerX = XMVectorGetX(XMVector3Dot(center, right));
    const float centerY = XMVectorGetX(XMVector3Dot(center, vertical));
    center = XMVectorMultiplyAdd(right, XMVectorReplicate(std::round(centerX / texel) * texel - centerX), center);
    center = XMVectorMultiplyAdd(vertical, XMVectorReplicate(std::round(centerY / texel) * texel - centerY), center);
    const XMVECTOR eye = XMVectorSubtract(center, XMVectorScale(direction, 96.0f));
    const XMMATRIX view = XMMatrixLookToLH(eye, direction, up);
    DirectX::XMFLOAT4X4 result;
    XMStoreFloat4x4(&result, view * XMMatrixOrthographicLH(diameter, diameter, 0.1f, 192.0f));
    return result;
}

bool FiniteMatrix(const DirectX::XMFLOAT4X4& matrix)
{
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            if (!std::isfinite(matrix.m[row][column]))
                return false;
    return true;
}

uint32_t RequiredAuxBones(const gameaux12::Mesh& mesh)
{
    uint32_t required = 0;
    for (const uint16_t index : mesh.indices)
    {
        if (index >= mesh.vertices.size())
            return 0;
        const auto& vertex = mesh.vertices[index];
        float total = 0.f;
        for (uint32_t influence = 0; influence < std::size(vertex.bones); ++influence)
        {
            const float weight = vertex.weights[influence];
            const uint32_t bone = vertex.bones[influence];
            if (!std::isfinite(weight) || weight < 0.f || weight > 1.0f + 1.0e-6f)
                return 0;
            if (bone >= gameaux12::kMaxBones || (weight == 0.f && bone != 0))
                return 0;
            required = (std::max)(required, bone + 1);
            total += weight;
        }
        if (std::abs(total - 1.f) > 1.e-4f)
            return 0;
    }
    return required;
}

D3D12_RESOURCE_DESC BufferDesc(uint64_t size)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

constexpr const char* kFrameShaderConstants = R"(
#define LOCAL_SHADOW_LAYERS 24
#define LOCAL_SHADOW_BANK_SIZE 8
struct LocalLightData
{
    float4 positionInverseRangeSquared;
    float4 directionOuterCos;
    float4 colorInnerCos;
    uint4 type;
};

cbuffer FrameCB : register(b0)
{
    float4x4 ViewProjection;
    float4 CameraPosition;
    float4x4 SunShadowViewProjection[4];
    float4 Ambient;
    float4 SunDirection;
    float4 SunColor;
    uint4 LightingFlags;
    LocalLightData LocalLights[MAX_LOCAL_LIGHTS];
    float4 HemiColor;
    float4x4 InverseViewProjection;
    float4 WaterWeather;
    float4 WaterSky;
    float4x4 LocalShadowViewProjection[LOCAL_SHADOW_LAYERS];
    uint4 DiagnosticLight;
    float4 DiagnosticOrigin;
    uint LegacyWaterSsr;
    uint SunFilter;
    uint SunReceiverBiasMask;
    uint SunShadowResolution;
    float4 SunCameraForward;
    float4 SunCascadeData[4];
    float4 SunDepthBias;
    uint4 SunCasterCounts;
    uint4 LocalShadowConfig;
};

// Смещение поверхности использует фазу watermove из R4/IX-Ray.
float WaterDisplacement(float3 position)
{
    return sin(WaterWeather.x + dot(position, float3(0.11f, 0.13f, 0.07f) * 25.0f)) / 60.0f;
}

cbuffer ShadowLayerCB : register(b6)
{
    uint ShadowLayer;
};

float4 ProjectShadow(float4 world)
{
    const uint slice = ShadowLayer & 3u;
    const uint localSlice = ShadowLayer - 4u;
    if (ShadowLayer < 4u)
        return mul(world, SunShadowViewProjection[slice]);
    if (localSlice < LOCAL_SHADOW_LAYERS && localSlice < LocalShadowConfig.x)
        return mul(world, LocalShadowViewProjection[localSlice]);
    return float4(0, 0, 2, 1);
}

// FXC размещает поля по порядку. VS и PS используют одну декларацию,
// чтобы TEXCOORD0 оставался в одном регистре при компоновке PSO.
struct ShadowInterpolants
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
)";

bool CompileShader(const char* source, const char* entryPoint,
                   const char* target, ID3D10Blob** outBlob,
                   std::string& reason)
{
    ComPtr<ID3D10Blob> error;
    const auto maxLocalLights = std::to_string(gamelight12::MaxLocalLights);
    const D3D_SHADER_MACRO definitions[] = {{"MAX_LOCAL_LIGHTS", maxLocalLights.c_str()}, {nullptr, nullptr}};
    const std::string shader = std::string(kFrameShaderConstants) + source;
    const HRESULT hr = D3DCompile(shader.data(), shader.size(), nullptr, definitions,
                                  nullptr, entryPoint, target, 0, 0, outBlob,
                                  &error);
    if (FAILED(hr))
    {
        char stage[160]{};
        std::snprintf(stage, sizeof(stage), "HLSL compile failed entry=%s profile=%s HRESULT=0x%08X",
                      entryPoint, target, static_cast<unsigned>(hr));
        reason = stage;
        if (error && error->GetBufferSize() != 0)
        {
            const auto* message = static_cast<const char*>(error->GetBufferPointer());
            const size_t length = error->GetBufferSize();
            reason += " compiler=";
            reason.append(message, message[length - 1] == '\0' ? length - 1 : length);
        }
        return false;
    }
    return true;
}

constexpr D3D12_INPUT_ELEMENT_DESC kStaticLayout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TANGENT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BINORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R16G16_SINT, 0, 28,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

// F4.1: вторая фактическая раскладка level.geom — TEXCOORD0 по смещению
// 24 (вместо COLOR@24 + TEXCOORD@28). Шейдеры и состояния те же, поэтому
// отличается только input layout.
constexpr D3D12_INPUT_ELEMENT_DESC kStaticLayoutUv24[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TANGENT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BINORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R16G16B16A16_SINT, 0, 24,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

constexpr D3D12_INPUT_LAYOUT_DESC kStaticInputLayouts[] = {
    {kStaticLayout, static_cast<UINT>(std::size(kStaticLayout))},
    {kStaticLayoutUv24, static_cast<UINT>(std::size(kStaticLayoutUv24))},
};

using AuxVertex = gameaux12::Vertex;
static_assert(sizeof(AuxVertex) == 64);
constexpr D3D12_INPUT_ELEMENT_DESC kAuxLayout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(AuxVertex, position),
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(AuxVertex, normal),
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(AuxVertex, uv),
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, offsetof(AuxVertex, bones),
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(AuxVertex, weights),
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};
constexpr D3D12_INPUT_LAYOUT_DESC kAuxInputLayout = {kAuxLayout, static_cast<UINT>(std::size(kAuxLayout))};

// Входная раскладка ровно та, что запекает level-компилятор (FVF 0x112).
// D3D12 R8G8B8A8_UNORM раскладывает байты в те же компоненты, что D3D9
// D3DCOLOR, поэтому unpack_normal(x).zyx остаётся эквивалентным R4.
constexpr const char* kSceneVertexShader = R"(
struct VSInput
{
    float3 position : POSITION;
    float4 nh       : NORMAL;
    float4 tangent  : TANGENT;
    float4 binormal : BINORMAL;
    int4   tc       : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float3 world    : TEXCOORD1;
    float hemi      : TEXCOORD2;
    float2 lmapUv   : TEXCOORD3;
};

// F4.1: мировая матрица объекта. Для запечённой статики уровня — identity,
// для flora (MT_TREE_*) — xform из OGF_TREEDEF2.
cbuffer ObjectCB : register(b3)
{
    row_major float4x4 World;
};

cbuffer StaticGeometryCB : register(b4)
{
    uint TreeGeometry;
    float WallmarkClipShift;
    uint AdvancedWaterGeometry;
    float PuddleHeight;
};

float2 MaterialUv(VSInput input)
{
    // FTreeVisual и tree_s используют короткие UV без дробной добавки и масштаб 16.
    return TreeGeometry != 0 ? float2(input.tc.xy) * (16.0f / 32768.0f) :
        (float2(input.tc.xy) + float2(input.tangent.w, input.binormal.w)) * (32.0f / 32768.0f);
}

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 worldPosition = mul(float4(input.position, 1.0f), World);
    if (AdvancedWaterGeometry != 0)
        worldPosition.y += WaterDisplacement(worldPosition.xyz);
    output.position = mul(worldPosition, ViewProjection);
    if (output.position.z > 0.0f)
        output.position.z = max(0.0f, output.position.z - WallmarkClipShift);
    const float3 modelNormal = 2.0f * input.nh.zyx - 1.0f;
    output.normal = mul(modelNormal, (float3x3)World);
    if (LightingFlags.x != 0)
    {
        const float3 row0 = World[0].xyz;
        const float3 row1 = World[1].xyz;
        const float3 row2 = World[2].xyz;
        const float3 cofactor0 = cross(row1, row2);
        const float determinant = dot(row0, cofactor0);
        if (abs(determinant) > 1.0e-8f)
        {
            const float3 transformed = modelNormal.x * cofactor0 +
                modelNormal.y * cross(row2, row0) + modelNormal.z * cross(row0, row1);
            output.normal = transformed * sign(determinant);
        }
    }
    output.uv = MaterialUv(input);
    output.world = worldPosition.xyz;
    output.hemi = input.nh.w;
    output.lmapUv = float2(input.tc.zw) * (1.0f / 32768.0f);
    return output;
}

VSOutput puddleMain(uint id : SV_VertexID)
{
    const float2 quad[6] = {
        float2(-1, -1), float2(-1, 1), float2(1, -1),
        float2(-1, 1), float2(1, 1), float2(1, -1)
    };
    const float2 corner = quad[id];
    VSOutput output = (VSOutput)0;
    const float4 worldPosition = mul(float4(corner.x, PuddleHeight, corner.y, 1), World);
    output.world = worldPosition.xyz;
    output.position = mul(worldPosition, ViewProjection);
    output.normal = normalize(World[1].xyz);
    output.uv = corner;
    return output;
}

ShadowInterpolants shadowMain(VSInput input)
{
    ShadowInterpolants output;
    const float4 worldPosition = mul(float4(input.position, 1.0f), World);
    output.position = ProjectShadow(worldPosition);
    output.uv = MaterialUv(input);
    return output;
}
)";

constexpr const char* kAuxVertexShader = R"(
cbuffer DrawCB : register(b3)
{
    uint MatrixBase;
};

cbuffer AuxGeometryCB : register(b4)
{
    uint ReservedGeometry;
    float WallmarkClipShift;
    uint AdvancedWaterGeometry;
    float ObjectSkyHemi;
};

struct Transform
{
    float4 row0, row1, row2, row3;
};
StructuredBuffer<Transform> Transforms : register(t1);

float4x4 LoadWorldTransform(uint index)
{
    const Transform transform = Transforms[index];
    return float4x4(transform.row0, transform.row1, transform.row2, transform.row3);
}

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 world : TEXCOORD1;
    float hemi   : TEXCOORD2;
    float2 lmapUv : TEXCOORD3;
};

void SkinVertex(float3 position, float3 normal, uint4 bones, float4 weights, uint instance,
                out float3 worldPosition, out float3 worldNormal)
{
    // Матрицы позы уже включают object-to-world. Для деталей offset
    // адресует последовательность инстансов, у каждого одна матрица.
    const uint baseIndex = MatrixBase + instance;
    worldPosition = 0.0f;
    worldNormal = 0.0f;
    [unroll]
    for (uint influence = 0; influence < 4; ++influence)
    {
        const float weight = weights[influence];
        if (weight > 0.0f)
        {
            const float4x4 world = LoadWorldTransform(baseIndex + bones[influence]);
            worldPosition += weight * mul(float4(position, 1.0f), world).xyz;
            // Как skinning_dir в R4, берём только линейную часть.
            worldNormal += weight * mul(normal, (float3x3)world);
        }
    }
}

VSOutput main(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD0,
              uint4 bones : BLENDINDICES, float4 weights : BLENDWEIGHT,
              uint instance : SV_InstanceID)
{
    float3 worldPosition, worldNormal;
    SkinVertex(position, normal, bones, weights, instance, worldPosition, worldNormal);
    VSOutput output;
    if (AdvancedWaterGeometry != 0)
        worldPosition.y += WaterDisplacement(worldPosition);
    output.position = mul(float4(worldPosition, 1.0f), ViewProjection);
    if (output.position.z > 0.0f)
        output.position.z = max(0.0f, output.position.z - WallmarkClipShift);
    const float normalSquared = dot(worldNormal, worldNormal);
    output.normal = normalSquared > 0.0f ? normalize(worldNormal) : float3(0, 1, 0);
    output.uv = uv;
    output.world = worldPosition;
    output.hemi = ObjectSkyHemi;
    output.lmapUv = 0.0f;
    return output;
}

ShadowInterpolants shadowMain(float3 position : POSITION, float3 normal : NORMAL,
                              float2 uv : TEXCOORD0, uint4 bones : BLENDINDICES,
                              float4 weights : BLENDWEIGHT, uint instance : SV_InstanceID)
{
    float3 worldPosition, worldNormal;
    SkinVertex(position, normal, bones, weights, instance, worldPosition, worldNormal);
    ShadowInterpolants output;
    output.position = ProjectShadow(float4(worldPosition, 1.0f));
    output.uv = uv;
    return output;
}
)";

constexpr const char* kScenePixelShader = R"(
Texture2D BaseColor : register(t0);
Texture2DArray<float> SunShadow : register(t2);
Texture2DArray<float> LocalShadow : register(t6);
Texture2D HemiLightmap : register(t7);
Texture2D TerrainMask : register(t8);
Texture2D TerrainRed : register(t9);
Texture2D TerrainGreen : register(t10);
Texture2D TerrainBlue : register(t11);
Texture2D TerrainAlpha : register(t12);

cbuffer TerrainCB : register(b7)
{
    float TerrainScale;
};
Texture2D WaterNormal : register(t3);
Texture2D OpaqueColor : register(t4);
Texture2D<float> OpaqueDepth : register(t5);
SamplerState BaseSampler : register(s0);
SamplerState WallmarkSampler : register(s1);
SamplerComparisonState SunShadowSampler : register(s2);
SamplerComparisonState SunPointSampler : register(s3);

cbuffer MaterialCB : register(b1)
{
    uint TextureMode;
    uint MaterialId;
    uint ViewMode;
    float AlphaThreshold;
};

struct PSInput
{
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float3 world    : TEXCOORD1;
    float hemi      : TEXCOORD2;
    float2 lmapUv   : TEXCOORD3;
};

float3 SafeDirection(float3 value)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-12f ? value * rsqrt(lengthSquared) : 0.0f;
}

float3 SunPlaneNormal(float3 dx, float3 dy)
{
    // Сначала убираем масштаб производных, затем измеряем угол между ними.
    const float sx = max(max(abs(dx.x), abs(dx.y)), abs(dx.z));
    const float sy = max(max(abs(dy.x), abs(dy.y)), abs(dy.z));
    if (!all(isfinite(float2(sx, sy))) || min(sx, sy) <= 0.0f)
        return 0.0f;
    return SafeDirection(cross(dx / sx, dy / sy));
}

float SunNormalOffset(uint slice, float cosine)
{
    const float offset = SunCameraForward.w != 0.0f ? min(0.025f, 0.5f * SunCascadeData[slice].w) : 0.025f;
    return (SunReceiverBiasMask & 1u) != 0 ? offset * (1.0f - cosine) : 0.0f;
}

bool ProjectSunReceiver(float3 position, float3 normal, float cosine, uint slice, out float3 coords)
{
    const float3 receiver = position + normal * SunNormalOffset(slice, cosine);
    const float4 clipPosition = mul(float4(receiver, 1.0f), SunShadowViewProjection[slice]);
    coords = 0.0f;
    if (!all(isfinite(clipPosition)) || clipPosition.w <= 0.0f)
        return false;
    const float3 projected = clipPosition.xyz / clipPosition.w;
    coords = float3(projected.xy * float2(0.5f, -0.5f) + 0.5f, projected.z);
    return all(isfinite(coords)) && all(coords > 0.0f) && all(coords < 1.0f);
}

// Orthographic receiver-plane correction: each comparison uses the depth
// of the same geometric plane at the sampled texel center. The shading
// normal is not a reliable plane normal on a smooth skinned surface.
bool SunReceiverGradient(float3 planeNormal, uint slice, out float2 gradient)
{
    gradient = 0.0f;
    const float4x4 projection = SunShadowViewProjection[slice];
    const float3 x = float3(projection[0][0], projection[1][0], projection[2][0]);
    const float3 y = float3(projection[0][1], projection[1][1], projection[2][1]);
    const float3 z = float3(projection[0][2], projection[1][2], projection[2][2]);
    const float x2 = dot(x, x), y2 = dot(y, y), z2 = dot(z, z);
    const float nz = dot(planeNormal, z);
    // Near-grazing or degenerate planes use the existing comparison sampler.
    if (min(x2, min(y2, z2)) <= 1.0e-12f ||
        abs(nz) <= 0.1f * sqrt(z2))
        return false;
    gradient = (-z2 / nz) * float2(2.0f * dot(planeNormal, x) / x2,
                                  -2.0f * dot(planeNormal, y) / y2);
    return all(isfinite(gradient));
}

float SunStoredDepth(int2 texel, uint slice)
{
    // Белая граница совпадает с аппаратным comparison sampler.
    return any(texel < 0) || any(texel >= (int)SunShadowResolution) ? 1.0f : SunShadow.Load(int4(texel, slice, 0));
}

float SunPlaneTap(int2 texel, float2 uv, float compareDepth, float2 gradient, uint slice)
{
    const float2 center = (float2(texel) + 0.5f) / (float)SunShadowResolution;
    return compareDepth + dot(center - uv, gradient) <= SunStoredDepth(texel, slice) ? 1.0f : 0.0f;
}

float SunPlaneVisibility(float2 uv, float compareDepth, float2 gradient, uint slice)
{
    const float2 pixel = uv * (float)SunShadowResolution - 0.5f;
    const int2 base = int2(floor(pixel));
    const float2 weight = frac(pixel);
    const float a = SunPlaneTap(base, uv, compareDepth, gradient, slice);
    const float b = SunPlaneTap(base + int2(1, 0), uv, compareDepth, gradient, slice);
    const float c = SunPlaneTap(base + int2(0, 1), uv, compareDepth, gradient, slice);
    const float d = SunPlaneTap(base + int2(1, 1), uv, compareDepth, gradient, slice);
    return lerp(lerp(a, b, weight.x), lerp(c, d, weight.x), weight.y);
}

float SunFilterVisibility(float3 coords, float3 planeNormal, uint slice)
{
    const float2 uv = coords.xy;
    const float compareDepth = coords.z - SunDepthBias[slice];
    float2 gradient = 0.0f;
    if ((SunFilter == 4 || SunFilter == 7) && SunReceiverGradient(planeNormal, slice, gradient))
        return SunPlaneVisibility(uv, compareDepth, gradient, slice);
    if (SunFilter == 1)
        return SunShadow.SampleCmpLevelZero(SunPointSampler, float3(uv, slice), compareDepth);
    if (SunFilter == 5 || SunFilter == 6)
    {
        const int radius = SunFilter == 5 ? 1 : 2;
        const float resolution = (float)SunShadowResolution;
        const float2 center = floor(uv * resolution) + 0.5f;
        float sum = 0;
        [loop]
        for (int y = -radius; y <= radius; ++y)
            [loop]
            for (int x = -radius; x <= radius; ++x)
                sum += SunShadow.SampleCmpLevelZero(SunPointSampler,
                    float3((center + float2(x, y)) / resolution, slice), compareDepth);
        const int width = 2 * radius + 1;
        return sum / (float)(width * width);
    }
    float visibility = SunShadow.SampleCmpLevelZero(SunShadowSampler, float3(uv, slice), compareDepth);
    if (SunFilter == 2 || SunFilter == 4)
        return visibility;
    // Дополнительные две билинейные выборки только на границе тени.
    [branch]
    if (visibility > 0.0f && visibility < 1.0f)
    {
        const float2 offset = float2(0.75f, 0.5f) / (float)SunShadowResolution;
        visibility = (visibility +
            SunShadow.SampleCmpLevelZero(SunShadowSampler, float3(uv + offset, slice), compareDepth) +
            SunShadow.SampleCmpLevelZero(SunShadowSampler, float3(uv - offset, slice), compareDepth)) / 3.0f;
    }
    return visibility;
}
)"
R"(
struct SunSelection
{
    uint slice;
    uint nextSlice;
    float3 coords;
    float3 nextCoords;
    float blend;
    float fade;
};

SunSelection SelectSun(float3 position, float3 normal, float cosine)
{
    SunSelection selected = (SunSelection)0;
    selected.slice = selected.nextSlice = 4u;
    const uint count = min(LightingFlags.w, 4u);
    if (count == 0)
        return selected;
    const bool useDepth = SunCameraForward.w != 0.0f;
    const float depth = dot(position - CameraPosition.xyz, SunCameraForward.xyz);
    if (useDepth && (depth < SunCascadeData[0].x || depth > SunCascadeData[count - 1].y))
        return selected;
    [loop]
    for (uint index = 0; index < count; ++index)
    {
        if (useDepth && depth > SunCascadeData[index].y)
            continue;
        if (ProjectSunReceiver(position, normal, cosine, index, selected.coords))
        {
            selected.slice = index;
            break;
        }
    }
    if (selected.slice >= count)
        return selected;
    if (!useDepth)
    {
        const float2 edge = min(selected.coords.xy, 1.0f - selected.coords.xy);
        selected.fade = 1.0f - saturate(min(edge.x, edge.y) * 16.0f);
        return selected;
    }
    const float4 primary = SunCascadeData[selected.slice];
    const float4 farCascade = SunCascadeData[count - 1];
    selected.fade = smoothstep(farCascade.z, farCascade.y, depth);
    const uint next = selected.slice + 1;
    if (next < count && depth > primary.z &&
        ProjectSunReceiver(position, normal, cosine, next, selected.nextCoords))
    {
        selected.nextSlice = next;
        selected.blend = smoothstep(primary.z, primary.y, depth);
    }
    return selected;
}

float SunSelectedVisibility(SunSelection selected, float3 planeNormal)
{
    if (selected.slice >= 4)
        return 1.0f;
    float visibility = SunFilterVisibility(selected.coords, planeNormal, selected.slice);
    if (selected.nextSlice < 4)
        visibility = lerp(visibility, SunFilterVisibility(selected.nextCoords, planeNormal, selected.nextSlice), selected.blend);
    return lerp(visibility, 1.0f, selected.fade);
}

float SunVisibility(float3 position, float3 normal, float cosine, float3 planeNormal)
{
    return SunSelectedVisibility(SelectSun(position, normal, cosine), planeNormal);
}

float3 DebugSun(uint mode, SunSelection selected, float3 planeNormal, float cosine)
{
    const uint slice = selected.slice;
    if (slice >= 4)
        return float3(1, 0, 1);
    const float3 coords = selected.coords;
    const float resolution = (float)SunShadowResolution;
    const float stored = SunStoredDepth(int2(coords.xy * resolution), slice);
    const float bias = SunDepthBias[slice];
    if (mode == 19)
        return SunSelectedVisibility(selected, planeNormal).xxx;
    if (mode == 20)
    {
        const float delta = coords.z - bias - stored;
        return float3(saturate(delta * 1024.0f), saturate(-delta * 1024.0f), 0);
    }
    if (mode == 26) return float3(coords.xy, 0);
    if (mode == 27 || mode == 37) return coords.zzz;
    if (mode == 28) return stored.xxx;
    if (mode == 29) return float3(saturate(SunNormalOffset(slice, cosine) * 40.0f), saturate(bias * 10000), 0);
    if (mode == 30)
    {
        float2 gradient;
        if (!SunReceiverGradient(planeNormal, slice, gradient))
            return float3(1, 0, 1);
        return float3(saturate(0.5f + gradient * 0.25f), 0);
    }
    const float2 weights = frac(coords.xy * resolution - 0.5f);
    if (mode == 31) return float3(weights, weights.x * weights.y);
    if (mode == 32)
    {
        const float2 texel = frac(coords.xy * resolution);
        const float2 edge = min(texel, 1.0f - texel);
        return step(0.1f, min(edge.x, edge.y)).xxx;
    }
    if (mode == 33)
    {
        const float3 colors[4] = {float3(1,0,0), float3(0,1,0), float3(0,0,1), float3(1,1,0)};
        return selected.nextSlice < 4 ? lerp(colors[slice], colors[selected.nextSlice], selected.blend) : colors[slice];
    }
    if (mode == 34) return float3(0, 1.0f - selected.fade, selected.fade);
    if (mode == 35) return float3(selected.blend, selected.fade, 0);
    if (mode == 36) return saturate(SunCascadeData[slice].w * 2.0f).xxx;
    return saturate(log2(1.0f + SunCasterCounts[slice]) / 16.0f).xxx;
}

)"
R"(// Explicit reasons: no map is expected; a bad layer/projection is not.
static const uint LOCAL_NO_MAP = 0;
static const uint LOCAL_VALID = 1;
static const uint LOCAL_OUTSIDE = 2;
static const uint LOCAL_BAD_LAYER = 3;
static const uint LOCAL_BAD_PROJECTION = 4;

uint ProjectLocalShadow(LocalLightData source, float3 position, float3 normal, float3 lightDirection,
                        out float3 coords, out uint layer)
{
    coords = 0.0f;
    layer = 0;
    if (source.type.y == 0)
        return LOCAL_NO_MAP;
    layer = source.type.y - 1;
    // Each bank contains two spot slices followed by a contiguous six-face point map.
    const uint bankOffset = layer % LOCAL_SHADOW_BANK_SIZE;
    if (layer >= min(LocalShadowConfig.x, (uint)LOCAL_SHADOW_LAYERS) ||
        (source.type.x == 0 && (bankOffset != 2u || layer + 5u >= LocalShadowConfig.x)) ||
        (source.type.x == 1 && bankOffset >= 2u) || source.type.x > 1)
        return LOCAL_BAD_LAYER;
    // Смещение задаётся в метрах. Грань point-карты выбирается уже
    // по смещённому приёмнику, который будет спроецирован в эту грань.
    const float3 lightPosition = source.positionInverseRangeSquared.xyz;
    const float footprint = clamp(length(position - lightPosition) * (1.0f / 1024.0f), 0.004f, 0.05f);
    const float3 receiver = position + lightDirection * 0.003f +
        normal * (footprint * (1.0f - saturate(dot(normal, lightDirection))));
    if (source.type.x == 0)
    {
        const float3 delta = receiver - lightPosition;
        const float3 axis = abs(delta);
        if (axis.x >= max(axis.y, axis.z))
            layer += delta.x >= 0.0f ? 0 : 1;
        else if (axis.y >= axis.z)
            layer += delta.y >= 0.0f ? 2 : 3;
        else
            layer += delta.z >= 0.0f ? 4 : 5;
    }
    if (layer >= min(LocalShadowConfig.x, (uint)LOCAL_SHADOW_LAYERS))
        return LOCAL_BAD_LAYER;
    const float4 clipPosition = mul(float4(receiver, 1.0f), LocalShadowViewProjection[layer]);
    if (!all(isfinite(clipPosition)) || clipPosition.w <= 0.0f)
        return LOCAL_BAD_PROJECTION;
    const float3 projected = clipPosition.xyz / clipPosition.w;
    coords = float3(projected.xy * float2(0.5f, -0.5f) + 0.5f, projected.z);
    if (!all(isfinite(coords)))
        return LOCAL_BAD_PROJECTION;
    return all(coords > 0.0f) && all(coords < 1.0f) ? LOCAL_VALID : LOCAL_OUTSIDE;
}

float SampleLocalVisibility(float3 coords, uint layer)
{
    const float2 halfTexel = 0.5f / 1024.0f;
    const float2 center = clamp(coords.xy, halfTexel, 1.0f - halfTexel);
    float visibility = LocalShadow.SampleCmpLevelZero(SunShadowSampler, float3(center, layer), coords.z);
    [branch]
    if (visibility > 0.0f && visibility < 1.0f)
    {
        // Узкий PCF на границе. Не читаем белый border за последним текселем.
        const float2 offset = float2(0.55f, 0.55f) / 1024.0f;
        const float2 low = clamp(coords.xy - offset, halfTexel, 1.0f - halfTexel);
        const float2 high = clamp(coords.xy + offset, halfTexel, 1.0f - halfTexel);
        visibility = (visibility +
            LocalShadow.SampleCmpLevelZero(SunShadowSampler, float3(low, layer), coords.z) +
            LocalShadow.SampleCmpLevelZero(SunShadowSampler, float3(high, layer), coords.z)) / 3.0f;
    }
    return visibility;
}

float LocalVisibility(LocalLightData source, float3 position, float3 normal, float3 lightDirection)
{
    float3 coords;
    uint layer;
    if (ProjectLocalShadow(source, position, normal, lightDirection, coords, layer) != LOCAL_VALID)
        return 1.0f;
    return SampleLocalVisibility(coords, layer);
}

bool DebugLightMatches(uint index)
{
    return DiagnosticLight.y == 0 || DiagnosticLight.x == index + 1;
}

float3 DebugLocalShadow(float3 position, float3 normal)
{
    [loop]
    for (uint index = 0; index < min(LightingFlags.z, (uint)MAX_LOCAL_LIGHTS); ++index)
    {
        const LocalLightData source = LocalLights[index];
        const float3 delta = source.positionInverseRangeSquared.xyz - position;
        const float3 lightDirection = SafeDirection(delta);
        if (!DebugLightMatches(index) || source.type.y == 0 ||
            dot(delta, delta) * source.positionInverseRangeSquared.w >= 1.0f ||
            (source.type.x != 0 && dot(-lightDirection, source.directionOuterCos.xyz) < source.directionOuterCos.w))
            continue;
        float3 coords;
        uint layer;
        if (ProjectLocalShadow(source, position, normal, lightDirection, coords, layer) != LOCAL_VALID)
            continue;
        const float3 sampleCoords = float3(coords.xy, layer);
        if (ViewMode == 4)
        {
            const float depth = LocalShadow.SampleLevel(WallmarkSampler, sampleCoords, 0);
            const float nearDepth = asfloat(source.type.z);
            const float farDepth = asfloat(source.type.w);
            const float linearDepth = nearDepth * farDepth / lerp(farDepth, nearDepth, depth);
            return saturate((linearDepth - nearDepth) / (farDepth - nearDepth)).xxx;
        }
        if (ViewMode == 5)
            return SampleLocalVisibility(coords, layer).xxx;
        return frac((layer + 1) * float3(0.618f, 0.381f, 0.173f));
    }
    return float3(0.12f, 0.0f, 0.12f);
}

float3 IndirectHemi(float3 normal, float bakedHemi)
{
    if (HemiColor.w <= 0.0f || (TextureMode & (512u | 1024u | 4096u | 8192u)) == 0)
        return 0.0f;
    // R4 v_hemi: направленная полусфера модулируется одним запечённым каналом.
    const float weight = saturate(bakedHemi) * saturate(0.5f + 0.5f * normal.y);
    const float3 ambient = max(0.0f, Ambient.rgb);
    return pow(ambient + max(0.0f, HemiColor.rgb) * weight, 2.2f) - pow(ambient, 2.2f);
}

float LocalFalloff(LocalLightData source, float3 toLight)
{
    // R4 plight_local: q-linear falloff, .95 радиуса уже в снимке.
    return saturate(1.0f - max(0.1f, dot(toLight, toLight)) * source.positionInverseRangeSquared.w);
}

float LocalAngular(LocalLightData source, float3 normal, float3 lightDir)
{
    const float cone = source.type.x != 0 ?
        smoothstep(source.directionOuterCos.w, source.colorInnerCos.w,
                   dot(-lightDir, source.directionOuterCos.xyz)) : 1.0f;
    const float cosine = saturate(dot(normal, lightDir));
    return cone * cosine;
}

float3 LocalLighting(float3 normal, float3 position, bool diagnostic, bool withShadows)
{
    float3 light = 0.0f;
    [loop]
    for (uint index = 0; index < min(LightingFlags.z, (uint)MAX_LOCAL_LIGHTS); ++index)
    {
        if (diagnostic && !DebugLightMatches(index))
            continue;
        const LocalLightData source = LocalLights[index];
        const float3 toLight = source.positionInverseRangeSquared.xyz - position;
        const float falloff = LocalFalloff(source, toLight);
        if (falloff <= 0.0f)
            continue;
        const float3 lightDir = SafeDirection(toLight);
        const float angular = LocalAngular(source, normal, lightDir);
        if (angular > 0.0f)
            light += source.colorInnerCos.rgb * (pow(falloff, 2.2f) * angular *
                (withShadows ? LocalVisibility(source, position, normal, lightDir) : 1.0f));
    }
    return light;
}

float4 SampleBaseColor(float2 uv)
{
    return (TextureMode & 16) != 0 ? BaseColor.SampleLevel(WallmarkSampler, uv, 0) :
        BaseColor.Sample(BaseSampler, uv);
}

void ApplyAlphaTest(float alpha)
{
    if ((TextureMode & 2) != 0)
        clip(alpha - AlphaThreshold);
}

float3 ToDisplay(float3 color)
{
    color = saturate(color);
    return max(min(12.92f * color, 0.04045f),
               1.055f * pow(color, 1.0f / 2.4f) - 0.055f);
}

float2 WaterUv(float2 uv, float2 position, float amplitude)
{
    // timers.z у R4/IX-Ray соответствует Device.fTimeGlobal / 10.
    const float angle = WaterWeather.x * 0.1f +
        dot(position, float2(0.2111f, 0.2333f) * amplitude);
    return uv + amplitude * float2(sin(angle), cos(angle));
}

float3 ReconstructOpaqueWorld(float2 uv, float depth)
{
    const float4 clipPosition = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
    const float4 world = mul(clipPosition, InverseViewProjection);
    return world.xyz / max(world.w, 1.0e-6f);
}

struct OpaqueDepthSample
{
    float2 uv;
    float depth;
};

OpaqueDepthSample ReadOpaqueDepth(float2 uv, float2 dimensions)
{
    const int2 pixel = clamp(int2(uv * dimensions), int2(0, 0), int2(dimensions) - 1);
    OpaqueDepthSample sample;
    sample.uv = (float2(pixel) + 0.5f) / dimensions;
    sample.depth = OpaqueDepth.Load(int3(pixel, 0));
    return sample;
}

)"
R"(float3 WaterReflection(float3 position, float3 normal, float3 direction, float2 dimensions, float fresnel)
{
    // Цвет среды остаётся fallback для промахов screen-space луча.
    const float3 weatherSky = max(0.0f, WaterSky.rgb);
    const float3 sky = weatherSky / max(1.0f, max(weatherSky.r, max(weatherSky.g, weatherSky.b))) *
        lerp(0.55f, 1.0f, saturate(direction.y));
    float3 reflected = sky;
    [branch]
    if (fresnel >= 0.04f && direction.y >= -0.07f)
    {
        const float3 origin = position + normal * 0.07f;
        [loop]
        for (uint step = 1; step <= 8; ++step)
        {
            const float distanceAlongRay = 0.22f * step * step;
            const float3 rayPoint = origin + direction * distanceAlongRay;
            const float4 clipPoint = mul(float4(rayPoint, 1), ViewProjection);
            if (clipPoint.w <= 0.0f)
                break;
            const float3 ndc = clipPoint.xyz / clipPoint.w;
            const float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
            if (any(uv <= 0.0f) || any(uv >= 1.0f) || ndc.z <= 0.0f || ndc.z >= 1.0f)
                break;
            const OpaqueDepthSample opaque = ReadOpaqueDepth(uv, dimensions);
            if (opaque.depth >= 1.0f || ndc.z < opaque.depth)
                continue;
            const float3 offset = ReconstructOpaqueWorld(opaque.uv, opaque.depth) - origin;
            const float alongRay = dot(offset, direction);
            if (alongRay <= 0.15f)
                continue;
            const float3 offRay = offset - direction * alongRay;
            const float tolerance = 0.08f + 0.14f * step;
            if (dot(offRay, offRay) < tolerance * tolerance && alongRay <= distanceAlongRay + tolerance)
            {
                const float2 edge = min(opaque.uv, 1.0f - opaque.uv);
                const float confidence = saturate(min(edge.x, edge.y) * 10.0f) * (1.0f - step / 9.0f);
                reflected = lerp(sky, OpaqueColor.SampleLevel(WallmarkSampler, opaque.uv, 0).rgb, confidence);
                break;
            }
            if (LegacyWaterSsr != 0)
                break;
        }
    }
    return reflected;
}

float4 ShadeWater(PSInput input, float4 baseColor)
{
    uint width, height;
    OpaqueDepth.GetDimensions(width, height);
    const float2 dimensions = float2(width, height);
    const float2 screenUv = input.position.xy / dimensions;
    const float2 position = input.world.xz;
    const float2 base = position * 0.3f;
    const float2 drift = WaterWeather.yz * WaterWeather.x;
    const float3 first = WaterNormal.Sample(BaseSampler, WaterUv(base, position, 0.15f) + drift).xyz;
    const float3 second = WaterNormal.Sample(BaseSampler, WaterUv(base * 1.1f, position, 0.55f) - drift * 0.4f).xyz;
    // На редкой геометрии Jupiter основная мелкая волна задаётся двумя
    // нормалями с исходным диапазоном компонент.
    float3 wave = first + second - 1.0f;
    wave.z = max(abs(wave.z), 0.12f);
    const float2 drop = sin(position * float2(17.0f, 21.0f) - WaterWeather.x * 5.0f);
    wave.xy += drop * (0.065f * WaterWeather.w);
    const float3 groundNormal = normalize(input.normal);
    const float3 axis = abs(groundNormal.x) < 0.99f ? float3(-1, 0, 0) : float3(0, 0, 1);
    const float3 tangent = normalize(axis - groundNormal * dot(axis, groundNormal));
    const float3 normal = normalize(tangent * wave.x + cross(groundNormal, tangent) * wave.y + groundNormal * wave.z);
    const float3 incident = normalize(input.world - CameraPosition.xyz);
    const float3 reflection = reflect(incident, normal);
    const float fresnel = 0.02f + 0.98f * pow(1.0f - saturate(dot(-incident, normal)), 5.0f);
    if (ViewMode == 11)
        return float4(normal * 0.5f + 0.5f, 1.0f);
    const float3 reflected = WaterReflection(input.world, normal, reflection, dimensions, fresnel);
    if (ViewMode == 12)
        return float4(ToDisplay(reflected), 1.0f);

    const bool puddle = (TextureMode & 256u) != 0;
    const OpaqueDepthSample opaque = ReadOpaqueDepth(screenUv, dimensions);
    const float thickness = opaque.depth >= 1.0f ? 12.0f :
        distance(ReconstructOpaqueWorld(opaque.uv, opaque.depth), input.world);
    const float shore = saturate(thickness * (puddle ? 100.0f : 3.0f));
    const float2 distortedUv = screenUv + wave.xy * (shore * (puddle ? 0.004f : 0.02f));
    float2 refractionUv = screenUv;
    if (all(distortedUv > 0.0f) && all(distortedUv < 1.0f))
    {
        const OpaqueDepthSample displaced = ReadOpaqueDepth(distortedUv, dimensions);
        if (displaced.depth >= opaque.depth - 1.0e-4f && displaced.depth > input.position.z)
            refractionUv = distortedUv;
    }
    const float3 transmitted = OpaqueColor.SampleLevel(WallmarkSampler, refractionUv, 0).rgb;
    const float turbidity = puddle ? 0.005f : 1.0f - exp(-0.55f * thickness);
    const float3 waterColor = lerp(float3(0.025f, 0.055f, 0.043f), baseColor.rgb,
                                   saturate(baseColor.a) * 0.35f);
    const float3 refracted = lerp(transmitted, waterColor, turbidity);
    const float edge = puddle ? saturate((1.0f - max(abs(input.uv.x), abs(input.uv.y))) * 18.0f) : 1.0f;
    const float coverage = shore * edge * (puddle ? WaterSky.a : 1.0f);
    const float3 composite = lerp(refracted, reflected, fresnel);
    [branch]
    if (coverage >= 1.0f)
        return float4(ToDisplay(composite), 1.0f);
    const float3 original = all(refractionUv == screenUv) ? transmitted :
        OpaqueColor.SampleLevel(WallmarkSampler, screenUv, 0).rgb;
    return float4(ToDisplay(lerp(original, composite, coverage)), 1.0f);
}

float3 TerrainAlbedo(float3 baseLinear, float2 uv, float3 world)
{
    // R4 ограничивает detail range. Плавный переход сохраняет базовый слой
    // за пределами полезной дальности и убирает пять дальних выборок.
    const float3 viewOffset = world - CameraPosition.xyz;
    const float distanceSquared = dot(viewOffset, viewOffset);
    const float2 uvDx = ddx(uv);
    const float2 uvDy = ddy(uv);
    if (distanceSquared >= 2500.0f)
        return baseLinear;
    const float4 mask = TerrainMask.SampleGrad(BaseSampler, uv, uvDx, uvDy);
    const float weight = dot(mask, 1.0f);
    if (weight <= 1.0e-5f)
        return baseLinear;
    const float2 detailUv = uv * TerrainScale;
    const float2 detailDx = uvDx * TerrainScale;
    const float2 detailDy = uvDy * TerrainScale;
    const float3 layers = (TerrainRed.SampleGrad(BaseSampler, detailUv, detailDx, detailDy).rgb * mask.r +
        TerrainGreen.SampleGrad(BaseSampler, detailUv, detailDx, detailDy).rgb * mask.g +
        TerrainBlue.SampleGrad(BaseSampler, detailUv, detailDx, detailDy).rgb * mask.b +
        TerrainAlpha.SampleGrad(BaseSampler, detailUv, detailDx, detailDy).rgb * mask.a) / weight;
    const float3 encoded = saturate(2.0f * ToDisplay(baseLinear) * layers);
    const float3 decoded = lerp(encoded / 12.92f, pow((encoded + 0.055f) / 1.055f, 2.4f),
                               step(0.04045f, encoded));
    return lerp(baseLinear, decoded, 1.0f - smoothstep(1225.0f, 2500.0f, distanceSquared));
}

)"
R"(float4 main(PSInput input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    // Считаем производные плоскости до clip и ветвления на режимы материалов.
    const float3 correctedPlaneNormal = SunFilter == 4 || ViewMode == 24 || ViewMode == 30 ?
        SunPlaneNormal(ddx_fine(input.world), ddy_fine(input.world)) : 0.0f;
    const bool flipNormal = !frontFace && (TextureMode & 32) != 0;
    const float3 n = SafeDirection(flipNormal ? -input.normal : input.normal);
    float4 baseColor = float4(0.72f, 0.72f, 0.76f, 1.0f);
    const bool textured = (TextureMode & 1) != 0;
    if (textured)
        baseColor = SampleBaseColor(input.uv);
    ApplyAlphaTest(baseColor.a);
    if ((TextureMode & 128u) != 0)
        return ShadeWater(input, baseColor);
    if (ViewMode == 1)
        return float4(frac((MaterialId + 1) * float3(0.618f, 0.381f, 0.173f)), 1.0f);
    if (ViewMode == 2)
        return float4(frac(input.uv), saturate(n.y * 0.5f + 0.5f), 1.0f);
    if (ViewMode == 3)
        return textured ? float4(0.1f, 0.1f, 0.1f, 1.0f)
                        : float4(0.9f, 0.1f, 0.1f, 1.0f);
    if (ViewMode >= 4 && ViewMode <= 6)
        return float4(DebugLocalShadow(input.world, n), 1.0f);
    if ((TextureMode & 2048u) != 0)
        baseColor.rgb = TerrainAlbedo(baseColor.rgb, input.uv, input.world);
    if (ViewMode == 7)
        return float4(ToDisplay(baseColor.rgb), 1.0f);
    if (ViewMode == 8)
        return float4(n * 0.5f + 0.5f, 1.0f);
    float bakedHemi = input.hemi;
    if ((TextureMode & 1024u) != 0 && HemiColor.w > 0.0f)
        bakedHemi = HemiLightmap.Sample(WallmarkSampler, input.lmapUv).a; // CoP: s_hemi alpha.
    if ((TextureMode & 4096u) != 0)
        bakedHemi = baseColor.a; // BmmD terrain: alpha базового слоя перед detail.
    if (ViewMode == 10)
        return float4(ToDisplay(IndirectHemi(n, bakedHemi)), 1.0f);
    if (ViewMode == 14)
        return float4(ToDisplay(LocalLighting(n, input.world, true, false)), 1.0f);
    if (ViewMode == 15 || ViewMode == 16)
    {
        const uint count = min(LightingFlags.z, (uint)MAX_LOCAL_LIGHTS);
        if (DiagnosticLight.x == 0 || DiagnosticLight.x > count)
            return float4(1.0f, 0.0f, 1.0f, 1.0f);
        if (ViewMode == 16)
            return float4(DiagnosticLight.x / (float)MAX_LOCAL_LIGHTS,
                          (DiagnosticLight.w & 255u) / 255.0f, DiagnosticLight.z / 2.0f, 1.0f);
        const LocalLightData source = LocalLights[DiagnosticLight.x - 1];
        const float3 toLight = source.positionInverseRangeSquared.xyz - input.world;
        const float falloff = LocalFalloff(source, toLight);
        const float3 lightDir = SafeDirection(toLight);
        const float angular = LocalAngular(source, n, lightDir);
        const float3 raw = source.colorInnerCos.rgb * (pow(falloff, 2.2f) * angular);
        const float visibility = falloff > 0.0f && angular > 0.0f ?
            LocalVisibility(source, input.world, n, lightDir) : 1.0f;
        return float4(falloff, visibility, dot(raw * visibility, float3(0.2126f, 0.7152f, 0.0722f)), 1.0f);
    }
    if (ViewMode == 17)
        return float4(saturate(0.5f + (input.world - DiagnosticOrigin.xyz) * (1.0f / 16.0f)), 1.0f);
    if (ViewMode == 18)
    {
        const uint count = min(LightingFlags.z, (uint)MAX_LOCAL_LIGHTS);
        if (DiagnosticLight.x == 0 || DiagnosticLight.x > count)
            return float4(1, 0, 1, 1);
        const LocalLightData source = LocalLights[DiagnosticLight.x - 1];
        const float3 delta = source.positionInverseRangeSquared.xyz - input.world;
        const float3 direction = SafeDirection(delta);
        if (LocalFalloff(source, delta) <= 0.0f || LocalAngular(source, n, direction) <= 0.0f)
            return float4(0, 0, 0, 1);
        float3 coords;
        uint layer;
        const uint status = ProjectLocalShadow(source, input.world, n, direction, coords, layer);
        if (status == LOCAL_NO_MAP) return float4(1, 0, 0, 1);
        if (status == LOCAL_OUTSIDE) return float4(1, 1, 0, 1);
        if (status == LOCAL_BAD_LAYER) return float4(0, 1, 1, 1);
        if (status == LOCAL_BAD_PROJECTION) return float4(1, 0, 1, 1);
        return float4(0, 0.25f + 0.75f * SampleLocalVisibility(coords, layer), 0, 1);
    }
    const float sunCosine = LightingFlags.y != 0 ? saturate(dot(n, -SunDirection.xyz)) : 0.0f;
    // Geometric receiver plane, computed before the light-dependent branch.
    const float3 planeNormal = SunFilter == 7 ? SafeDirection(cross(ddx(input.world), ddy(input.world))) :
        correctedPlaneNormal;
    if (ViewMode == 19 || ViewMode == 20 || (ViewMode >= 26 && ViewMode <= 38))
        return float4(DebugSun(ViewMode, SelectSun(input.world, n, sunCosine), planeNormal, sunCosine), 1);
    if (ViewMode == 21)
        return float4(ToDisplay(Ambient.rgb), 1);
    if (ViewMode == 22)
        return float4(ToDisplay(SunColor.rgb * sunCosine *
            SunVisibility(input.world, n, sunCosine, planeNormal)), 1);
    if (ViewMode == 23)
        return float4(ToDisplay(LocalLighting(n, input.world, false, true)), 1);
    if (ViewMode == 24)
        return float4(planeNormal * (dot(planeNormal, n) < 0.0f ? -0.5f : 0.5f) + 0.5f, 1);
    if (ViewMode == 13)
    {
        const float3 luminance = float3(0.2126f, 0.7152f, 0.0722f);
        const float direct = dot(SunColor.rgb * sunCosine, luminance);
        const float local = dot(LocalLighting(n, input.world, true, true), luminance);
        const float3 display = ToDisplay(float3(direct, 0.0f, local));
        return float4(display.r, SunVisibility(input.world, n, sunCosine, planeNormal), display.b, 1.0f);
    }
    float3 light = 1.0f;
    if ((TextureMode & 4) == 0)
    {
        if (LightingFlags.x != 0)
        {
            light = Ambient.rgb + IndirectHemi(n, bakedHemi) + LocalLighting(n, input.world, false, true);
            if (sunCosine > 0.0f)
                light += SunColor.rgb * (sunCosine * SunVisibility(input.world, n, sunCosine, planeNormal));
        }
        else
        {
            const float3 sunDir = normalize(float3(-0.42f, 0.78f, 0.35f));
            const float sun = saturate(dot(n, sunDir));
            const float sky = saturate(n.y) * 0.22f;
            light = 0.55f + 0.45f * sun + sky;
        }
    }
    if (ViewMode == 9)
        return float4(ToDisplay(light), 1.0f);
    // Diagnostic HDR over-range: red marks a channel that would be clipped
    // by the existing LDR final composition. No production exposure change.
    if (ViewMode == 25)
        return float4(saturate(max(baseColor.rgb * light - 1.0f, 0.0f)), 1);
    const float3 linearColor = saturate(baseColor.rgb * light);
    float alpha = (TextureMode & 8) != 0 ? baseColor.a : 1.0f;
    // Прозрачная вода до переноса R4 scene/depth/refraction: DDS alpha
    // меняет непрозрачность базового слоя, оставляя геометрию за водой видимой.
    if ((TextureMode & 64) != 0)
        alpha = lerp(0.25f, 0.60f, alpha);
    return float4(max(min(12.92f * linearColor, 0.04045f),
                      1.055f * pow(linearColor, 1.0f / 2.4f) - 0.055f), alpha);
}

void shadowMain(ShadowInterpolants input)
{
    if ((TextureMode & 2) != 0)
        ApplyAlphaTest(SampleBaseColor(input.uv).a);
}
)";

// Шесть плоскостей из row-vector ViewProjection (как в GameDx12Bridge.cpp).
struct FrustumPlane
{
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
    float d = 0.0f;
};

bool NormalizePlane(FrustumPlane& plane)
{
    const float length =
        std::sqrt(plane.nx * plane.nx + plane.ny * plane.ny + plane.nz * plane.nz);
    if (!std::isfinite(length) || length < 1.0e-9f)
        return false;
    plane.nx /= length;
    plane.ny /= length;
    plane.nz /= length;
    plane.d /= length;
    return true;
}

bool BuildFrustum(const DirectX::XMFLOAT4X4& full, FrustumPlane planes[6])
{
    float column[4][4] = {};
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t index = 0; index < 4; ++index)
            column[index][row] = full.m[row][index];

    const auto set = [&planes, &column](uint32_t index, float sign0, float sign3)
    {
        FrustumPlane& plane = planes[index];
        plane.nx = column[0][0] * sign0 + column[3][0] * sign3;
        plane.ny = column[0][1] * sign0 + column[3][1] * sign3;
        plane.nz = column[0][2] * sign0 + column[3][2] * sign3;
        plane.d = column[0][3] * sign0 + column[3][3] * sign3;
        return NormalizePlane(plane);
    };
    const auto setAxis = [&planes, &column](uint32_t index, float sign1,
                                            float sign3)
    {
        FrustumPlane& plane = planes[index];
        plane.nx = column[1][0] * sign1 + column[3][0] * sign3;
        plane.ny = column[1][1] * sign1 + column[3][1] * sign3;
        plane.nz = column[1][2] * sign1 + column[3][2] * sign3;
        plane.d = column[1][3] * sign1 + column[3][3] * sign3;
        return NormalizePlane(plane);
    };

    if (!set(0, 1.0f, 1.0f) || !set(1, -1.0f, 1.0f) ||
        !setAxis(2, 1.0f, 1.0f) || !setAxis(3, -1.0f, 1.0f))
        return false;

    FrustumPlane& nearPlane = planes[4];
    nearPlane.nx = column[2][0];
    nearPlane.ny = column[2][1];
    nearPlane.nz = column[2][2];
    nearPlane.d = column[2][3];
    FrustumPlane& farPlane = planes[5];
    farPlane.nx = column[3][0] - column[2][0];
    farPlane.ny = column[3][1] - column[2][1];
    farPlane.nz = column[3][2] - column[2][2];
    farPlane.d = column[3][3] - column[2][3];
    return NormalizePlane(nearPlane) && NormalizePlane(farPlane);
}

bool SphereInside(const FrustumPlane planes[6], const float center[3],
                  float radius)
{
    for (uint32_t index = 0; index < 6; ++index)
    {
        const FrustumPlane& plane = planes[index];
        const float distance = plane.nx * center[0] + plane.ny * center[1] +
                               plane.nz * center[2] + plane.d;
        if (distance < -radius)
            return false;
    }
    return true;
}

bool BoxInside(const FrustumPlane* planes, const float minimum[3], const float maximum[3])
{
    for (uint32_t index = 0; index < 6; ++index)
    {
        const auto& plane = planes[index];
        const float x = plane.nx >= 0.0f ? maximum[0] : minimum[0];
        const float y = plane.ny >= 0.0f ? maximum[1] : minimum[1];
        const float z = plane.nz >= 0.0f ? maximum[2] : minimum[2];
        if (plane.nx * x + plane.ny * y + plane.nz * z + plane.d < 0.0f)
            return false;
    }
    return true;
}

struct LocalShadowViews
{
    std::array<std::array<FrustumPlane, 6>, kLocalShadowLayers> frusta{};
    std::array<bool, kLocalShadowLayers> active{};
    std::array<bool, gamelight12::MaxLocalLights> invalidProjection{};
    std::array<bool, gamelight12::MaxLocalLights> suppressed{};
    uint32_t count = 0;
};

struct LocalProjection
{
    std::array<DirectX::XMFLOAT4X4, kPointShadowFaces> matrices{};
    std::array<std::array<FrustumPlane, 6>, kPointShadowFaces> frusta{};
    uint32_t count = 0;
};

bool BuildLocalShadowProjection(const gamelight12::LocalLight& light, LocalProjection& result)
{
    using namespace DirectX;
    constexpr gamelight12::Vec3 cubeDirections[] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    };
    constexpr gamelight12::Vec3 cubeUp[] = {
        {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}
    };
    static_assert(std::size(cubeDirections) == kPointShadowFaces);
    const bool spot = light.type == gamelight12::LocalType::Spot;
    const uint32_t faceCount = spot ? 1u : kPointShadowFaces;
    // Поля фильтрации по краям карты и на стыках шести граней.
    const float fov = spot ? 2.0f * std::acos(light.outerCos) + 0.04f : XM_PIDIV2 + 0.04f;
    if (!std::isfinite(fov) || fov <= 0.0f || fov >= XM_PI)
        return false;
    const auto vector = [](const gamelight12::Vec3& v) { return XMVectorSet(v.x, v.y, v.z, 0.0f); };
    const XMVECTOR eye = vector(light.position);
    if (!std::isfinite(light.shadowNear) || !std::isfinite(light.shadowFar) ||
        light.shadowNear <= 0.0f || light.shadowFar <= light.shadowNear)
        return false;
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(fov, 1.0f, light.shadowNear, light.shadowFar);
    for (uint32_t face = 0; face < faceCount; ++face)
    {
        const auto direction = spot ? light.direction : cubeDirections[face];
        const auto up = spot ? (std::abs(direction.y) < 0.95f ? gamelight12::Vec3{0, 1, 0}
                                                             : gamelight12::Vec3{0, 0, 1}) : cubeUp[face];
        DirectX::XMFLOAT4X4 matrix;
        XMStoreFloat4x4(&matrix, XMMatrixLookToLH(eye, vector(direction), vector(up)) * projection);
        if (!FiniteMatrix(matrix) || !BuildFrustum(matrix, result.frusta[face].data()))
            return false;
        result.matrices[face] = Transpose(matrix);
    }
    result.count = faceCount;
    return true;
}

void SelectLocalShadowViews(const gamelight12::FrameSnapshot& lighting,
    uint32_t banks, game_shadow12::LocalAllocationState& previous,
    SceneFrameConstants& constants, LocalShadowViews& views)
{
    static_assert(gamelight12::MaxLocalLights == game_shadow12::MaxLocalCandidates);
    const uint32_t lightCount = constants.lightingFlags[2];
    if (lightCount > gamelight12::MaxLocalLights || !game_shadow12::ValidBanks(banks))
        return;
    std::array<game_shadow12::LocalCandidate, gamelight12::MaxLocalLights> candidates{};
    std::array<LocalProjection, gamelight12::MaxLocalLights> projections{};
    for (uint32_t i = 0; i < lightCount; ++i)
    {
        const auto& light = lighting.localLights[i];
        if (!light.castsShadow || !light.id)
            continue;
        if (!BuildLocalShadowProjection(light, projections[i]))
        {
            views.invalidProjection[i] = true;
            continue;
        }
        candidates[i] = {light.id, light.selectionScore,
            light.type == gamelight12::LocalType::Spot, light.nearVolume, true};
    }
    const auto plan = game_shadow12::AllocateLocalShadows(
        std::span(candidates.data(), lightCount), banks, lighting.levelSession, lighting.levelEpoch,
        previous, SceneStableLocalPriorityRequested());
    for (uint32_t slot = 0; slot < banks * game_shadow12::SlotsPerBank; ++slot)
    {
        const uint32_t index = plan.candidate[slot];
        if (index == game_shadow12::NoCandidate)
            continue;
        const auto& light = lighting.localLights[index];
        const auto& projection = projections[index];
        const uint32_t first = game_shadow12::SlotBase(slot);
        for (uint32_t face = 0; face < projection.count; ++face)
        {
            const uint32_t layer = first + face;
            constants.localShadowViewProjection[layer] = projection.matrices[face];
            views.frusta[layer] = projection.frusta[face];
            views.active[layer] = true;
            ++views.count;
        }
        auto& type = constants.localLights[index].type;
        type[1] = first + 1;
        type[2] = std::bit_cast<uint32_t>(light.shadowNear);
        type[3] = std::bit_cast<uint32_t>(light.shadowFar);
    }
    previous = plan.next;
    uint32_t suppressed = 0;
    // Explicit A/B policy only: suppress direct contribution of eligible
    // shadow-casting lights that have no map. Never suppress genuine unshadowed
    // lights or the deliberate NoLocalShadows mode. Default remains legacy.
    if (SceneSuppressUnmappedLocalLightsRequested())
        for (uint32_t i = 0; i < lightCount; ++i)
            if ((plan.eligibleMask & (1u << i)) != 0 && lighting.localLights[i].sourceShadow &&
                constants.localLights[i].type[1] == 0)
            {
                auto& color = constants.localLights[i].colorInnerCos;
                color.x = color.y = color.z = 0.0f;
                views.suppressed[i] = true;
                ++suppressed;
            }
    if (lighting.frame % 120 == 0 || lighting.diagnosticBurst)
        Msg("ph_dx12_lsa: frame=%llu banks=%u eligible=%u assigned=%u retained=%u released=%u misses=%u suppressed=%u views=%u",
            static_cast<unsigned long long>(lighting.frame), banks, plan.eligible, plan.assigned,
            plan.retained, plan.released, plan.misses, suppressed, views.count);
}

uint32_t SceneViewMode()
{
    static const uint32_t mode = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_SCENE_VIEW");
        if (value == nullptr || value[0] == '\0')
            return 0u;
        const unsigned long parsed = std::strtoul(value, nullptr, 10);
        return parsed <= 38ul ? static_cast<uint32_t>(parsed) : 0u;
    }();
    return mode;
}

bool SceneCullDisabled()
{
    static const bool disabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_SCENE_CULL");
        return value != nullptr && std::strcmp(value, "0") == 0;
    }();
    return disabled;
}

// Диагностика: принудительно отключить alpha-test clip. Нужна, чтобы
// отделить «геометрия не загружена» от «материал вырезан порогом».
bool SceneAlphaTestDisabled()
{
    static const bool disabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_SCENE_NOTEST");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return disabled;
}

const std::array<uint32_t, 2>& R1VisualSelection()
{
    static const auto selected = []
    {
        constexpr std::array<uint32_t, 2> empty{UINT32_MAX, UINT32_MAX};
        const char* value = std::getenv("OGSR_DX12_GAME_R1_VISUAL_ID");
        if (!value || !*value)
            return empty;

        auto ids = empty;
        std::string_view remaining(value);
        for (auto& id : ids)
        {
            const auto comma = remaining.find(',');
            auto token = remaining.substr(0, comma);
            const auto first = token.find_first_not_of(" \t");
            if (first == token.npos)
                break;
            token = token.substr(first, token.find_last_not_of(" \t") - first + 1);
            const char* end = token.data() + token.size();
            const auto parse = std::from_chars(token.data(), end, id);
            if (parse.ec != std::errc{} || parse.ptr != end || id == UINT32_MAX)
                break;
            if (comma == remaining.npos)
            {
                if (ids[1] == ids[0])
                    ids[1] = UINT32_MAX;
                return ids;
            }
            remaining.remove_prefix(comma + 1);
        }
        Msg("ph_dx12_f43_r1: invalid OGSR_DX12_GAME_R1_VISUAL_ID expected one or two decimal IDs");
        return empty;
    }();
    return selected;
}

uint8_t R1VisualTraceSlot(const std::array<uint32_t, 2>& selected, uint32_t visualId)
{
    for (uint8_t slot = 0; slot < selected.size(); ++slot)
        if (selected[slot] != UINT32_MAX && selected[slot] == visualId)
            return slot;
    return UINT8_MAX;
}

// F4.1-E: per-draw дамп для сравнения с R4 (ph_cmp: r4draw).
bool SceneCompareEnabled()
{
    static const bool enabled = []
    {
        const char* value = std::getenv("OGSR_DX12_CMP");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// Process-lifetime карантин: неизвестное завершение GPU-работы запрещает
// освобождение ресурсов, поэтому они остаются до конца процесса.
// Сырые указатели без деструкторов: аварийный путь не должен аллоцировать.
constexpr uint32_t kQuarantineBuffers = 16;
constexpr uint32_t kQuarantineTextures = 4096;

struct StaticSceneQuarantine12
{
    bool occupied = false;
    IUnknown* buffers[kQuarantineBuffers] = {};
    IUnknown* textures[kQuarantineTextures] = {};
    uint32_t bufferCount = 0;
    uint32_t textureCount = 0;
    IUnknown* heap = nullptr;
    IUnknown* constants = nullptr;
    IUnknown* rootSignature = nullptr;
    IUnknown* pipelineState = nullptr;
};

StaticSceneQuarantine12 g_staticQuarantine;
} // namespace

GameDx12StaticScene12::~GameDx12StaticScene12()
{
    // Любой ещё удерживаемый объект может иметь неизвестное завершение.
    if (m_loaded || m_gpuUnknown || m_uploadSubmitted || m_passSubmitted)
        QuarantineGpu();
    else
        ReleaseGpu();
}

bool GameDx12StaticScene12::Fail(std::string& reason, const char* text)
{
    m_lastError = text != nullptr ? text : "static scene failure";
    reason = m_lastError;
    return false;
}

bool GameDx12StaticScene12::Fail(std::string& reason, const std::string& text)
{
    m_lastError = text;
    reason = text;
    return false;
}

void GameDx12StaticScene12::BuildShadowCasterTrees()
{
    for (uint32_t index = 0; index < static_cast<uint32_t>(m_draws.size()); ++index)
    {
        const auto& draw = m_draws[index];
        if (CastsSunShadow(draw.surface))
            m_shadowCasters[draw.layout == 1 ? 0 : 1].draws.push_back(index);
    }
    // AABB узлов объединяет те же мировые сферы, что использует отсечение
    // отдельных draw. Локальные карты пропускают целые ветви уровня.
    for (auto& tree : m_shadowCasters)
        if (!tree.draws.empty())
            BuildShadowCasterNode(tree, 0, static_cast<uint32_t>(tree.draws.size()));
}

uint32_t GameDx12StaticScene12::BuildShadowCasterNode(ShadowCasterTree& tree, uint32_t first, uint32_t count)
{
    const uint32_t nodeIndex = static_cast<uint32_t>(tree.nodes.size());
    tree.nodes.emplace_back();
    ShadowCasterNode node{};
    node.first = first;
    node.count = count;
    const auto& firstDraw = m_draws[tree.draws[first]];
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        node.minimum[axis] = firstDraw.center[axis] - firstDraw.radius;
        node.maximum[axis] = firstDraw.center[axis] + firstDraw.radius;
    }
    for (uint32_t offset = 1; offset < count; ++offset)
    {
        const auto& draw = m_draws[tree.draws[first + offset]];
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            node.minimum[axis] = (std::min)(node.minimum[axis], draw.center[axis] - draw.radius);
            node.maximum[axis] = (std::max)(node.maximum[axis], draw.center[axis] + draw.radius);
        }
    }

    constexpr uint32_t leafSize = 12;
    if (count > leafSize)
    {
        const auto extent = [&](uint32_t axis) { return node.maximum[axis] - node.minimum[axis]; };
        uint32_t axis = extent(1) > extent(0) ? 1u : 0u;
        if (extent(2) > extent(axis))
            axis = 2;
        const uint32_t leftCount = count / 2;
        auto begin = tree.draws.begin() + first;
        std::nth_element(begin, begin + leftCount, begin + count, [&](uint32_t left, uint32_t right)
        {
            return m_draws[left].center[axis] < m_draws[right].center[axis];
        });
        node.count = 0;
        node.left = BuildShadowCasterNode(tree, first, leftCount);
        node.right = BuildShadowCasterNode(tree, first + leftCount, count - leftCount);
    }
    tree.nodes[nodeIndex] = node;
    return nodeIndex;
}

bool GameDx12StaticScene12::CreatePipeline(ID3D12Device* device,
                                           std::string& reason)
{
    D3D12_ROOT_PARAMETER params[13] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.ShaderRegister = 1;
    params[2].Constants.Num32BitValues = 4;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[3].Constants.ShaderRegister = 3;
    params[3].Constants.Num32BitValues = 16;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[4].Descriptor.ShaderRegister = 1;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[5].Constants.ShaderRegister = 4;
    params[5].Constants.Num32BitValues = 4;
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    auto shadowRange = range;
    shadowRange.BaseShaderRegister = 2;
    params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[6].DescriptorTable.NumDescriptorRanges = 1;
    params[6].DescriptorTable.pDescriptorRanges = &shadowRange;
    params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    auto waterRange = range;
    waterRange.NumDescriptors = kWaterSrvCount;
    waterRange.BaseShaderRegister = 3;
    params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[7].DescriptorTable.NumDescriptorRanges = 1;
    params[7].DescriptorTable.pDescriptorRanges = &waterRange;
    params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[kShadowLayerRoot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[kShadowLayerRoot].Constants.ShaderRegister = 6;
    params[kShadowLayerRoot].Constants.Num32BitValues = 1;
    params[kShadowLayerRoot].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    auto localShadowRange = range;
    localShadowRange.BaseShaderRegister = 6;
    params[kLocalShadowSrvRoot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kLocalShadowSrvRoot].DescriptorTable.NumDescriptorRanges = 1;
    params[kLocalShadowSrvRoot].DescriptorTable.pDescriptorRanges = &localShadowRange;
    params[kLocalShadowSrvRoot].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    auto hemiRange = range;
    hemiRange.BaseShaderRegister = 7;
    params[kHemiSrvRoot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kHemiSrvRoot].DescriptorTable.NumDescriptorRanges = 1;
    params[kHemiSrvRoot].DescriptorTable.pDescriptorRanges = &hemiRange;
    params[kHemiSrvRoot].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    auto terrainRange = range;
    terrainRange.BaseShaderRegister = 8;
    terrainRange.NumDescriptors = kTerrainTextureCount;
    params[kTerrainSrvRoot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kTerrainSrvRoot].DescriptorTable.NumDescriptorRanges = 1;
    params[kTerrainSrvRoot].DescriptorTable.pDescriptorRanges = &terrainRange;
    params[kTerrainSrvRoot].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[kTerrainScaleRoot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[kTerrainScaleRoot].Constants.ShaderRegister = 7;
    params[kTerrainScaleRoot].Constants.Num32BitValues = 1;
    params[kTerrainScaleRoot].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_ANISOTROPIC;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxAnisotropy = 8;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    auto wallmarkSampler = sampler;
    wallmarkSampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    wallmarkSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    wallmarkSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    wallmarkSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    wallmarkSampler.MaxAnisotropy = 1;
    wallmarkSampler.ShaderRegister = 1;
    auto shadowSampler = wallmarkSampler;
    shadowSampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    shadowSampler.ShaderRegister = 2;
    auto pointShadowSampler = shadowSampler;
    pointShadowSampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    pointShadowSampler.ShaderRegister = 3;
    const D3D12_STATIC_SAMPLER_DESC samplers[] = {sampler, wallmarkSampler, shadowSampler, pointShadowSampler};

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = static_cast<UINT>(std::size(params));
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = static_cast<UINT>(std::size(samplers));
    rootDesc.pStaticSamplers = samplers;
    rootDesc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3D10Blob> signature;
    ComPtr<ID3D10Blob> error;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, signature.GetAddressOf(),
        error.GetAddressOf());
    if (FAILED(hr))
    {
        reason = error != nullptr
                     ? std::string(static_cast<const char*>(error->GetBufferPointer()),
                                   error->GetBufferSize())
                     : "static scene root signature serialization failed";
        return false;
    }
    hr = device->CreateRootSignature(0, signature->GetBufferPointer(),
                                     signature->GetBufferSize(),
                                     IID_PPV_ARGS(m_rootSignature.GetAddressOf()));
    if (FAILED(hr))
        return Fail(reason, "static scene root signature creation failed");

    ComPtr<ID3D10Blob> vertexShader;
    ComPtr<ID3D10Blob> pixelShader;
    if (!CompileShader(kSceneVertexShader, "main", "vs_5_0",
                       vertexShader.GetAddressOf(), reason) ||
        !CompileShader(kScenePixelShader, "main", "ps_5_0",
                       pixelShader.GetAddressOf(), reason))
        return false;

    D3D12_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    const char* cullValue = std::getenv("OGSR_DX12_GAME_SCENE_CULL");
    rasterizer.CullMode = (cullValue != nullptr && std::strcmp(cullValue, "none") == 0)
                              ? D3D12_CULL_MODE_NONE
                              : D3D12_CULL_MODE_BACK;
    m_cullEnabled = rasterizer.CullMode == D3D12_CULL_MODE_BACK;
    rasterizer.FrontCounterClockwise = FALSE;
    rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizer.DepthClipEnable = TRUE;
    rasterizer.MultisampleEnable = FALSE;
    rasterizer.AntialiasedLineEnable = FALSE;
    rasterizer.ForcedSampleCount = 0;
    rasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC blend = {};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    blend.RenderTarget[0].BlendEnable = FALSE;
    blend.RenderTarget[0].LogicOpEnable = FALSE;
    blend.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depth.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_rootSignature.Get();
    desc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
    desc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
    desc.BlendState = blend;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = rasterizer;
    desc.DepthStencilState = depth;
    desc.InputLayout = kStaticInputLayouts[0];
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.NodeMask = 0;
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    const auto createDeferred = [&](DeferredPipelines& pipelines)
    {
        for (uint32_t slot = 0; slot < DeferredPipelines::Count; ++slot)
        {
            auto deferred = desc;
            deferred.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            deferred.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            if (slot != DeferredPipelines::Translucent)
            {
                // D32_FLOAT и обычный Z: ограниченный отрицательный bias
                // отделяет накладку от её опорной плоскости.
                deferred.RasterizerState.DepthBias = -1;
                deferred.RasterizerState.SlopeScaledDepthBias = -0.5f;
                deferred.RasterizerState.DepthBiasClamp = -4.0e-6f;
            }
            auto& target = deferred.BlendState.RenderTarget[0];
            target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED |
                D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
            if (slot != DeferredPipelines::Solid)
            {
                target.BlendEnable = TRUE;
                target.SrcBlend = slot == DeferredPipelines::Multiply
                    ? D3D12_BLEND_DEST_COLOR : D3D12_BLEND_SRC_ALPHA;
                target.DestBlend = slot == DeferredPipelines::Multiply
                    ? D3D12_BLEND_SRC_COLOR : D3D12_BLEND_INV_SRC_ALPHA;
                target.BlendOp = D3D12_BLEND_OP_ADD;
                target.SrcBlendAlpha = D3D12_BLEND_ONE;
                target.DestBlendAlpha = D3D12_BLEND_ZERO;
                target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            }
            if (FAILED(device->CreateGraphicsPipelineState(
                    &deferred, IID_PPV_ARGS(pipelines.states[slot].GetAddressOf()))))
                return Fail(reason, "deferred surface pipeline creation failed");
        }
        return true;
    };

    hr = device->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(m_pipelineState.GetAddressOf()));
    if (FAILED(hr))
        return Fail(reason, "static scene pipeline creation failed");
    if (!createDeferred(m_deferredPipelines[0]))
        return false;

    desc.InputLayout = kStaticInputLayouts[1];
    hr = device->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(m_pipelineUv24.GetAddressOf()));
    if (FAILED(hr))
        return Fail(reason, "static scene pipeline creation failed (uv24)");
    if (!createDeferred(m_deferredPipelines[1]))
        return false;

    if (gamewater12::PuddlesEnabled())
    {
        ComPtr<ID3D10Blob> puddleVertex;
        std::string puddleError;
        if (CompileShader(kSceneVertexShader, "puddleMain", "vs_5_0",
                          puddleVertex.GetAddressOf(), puddleError))
        {
            auto puddle = desc;
            puddle.VS = {puddleVertex->GetBufferPointer(), puddleVertex->GetBufferSize()};
            puddle.InputLayout = {};
            puddle.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            puddle.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            puddle.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            const HRESULT puddleHr = device->CreateGraphicsPipelineState(
                &puddle, IID_PPV_ARGS(m_pipelinePuddle.GetAddressOf()));
            if (FAILED(puddleHr))
                Msg("ph_dx12_f43e: puddle PSO unavailable hr=0x%08X", static_cast<unsigned>(puddleHr));
        }
        else
            Msg("ph_dx12_f43e: puddle VS unavailable reason='%s'", puddleError.c_str());
    }

    ComPtr<ID3D10Blob> auxVertex;
    if (!CompileShader(kAuxVertexShader, "main", "vs_5_0",
                       auxVertex.GetAddressOf(), reason))
        return false;
    desc.VS = {auxVertex->GetBufferPointer(), auxVertex->GetBufferSize()};
    desc.InputLayout = kAuxInputLayout;
    if (FAILED(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(m_pipelineSkeletal.GetAddressOf()))))
        return Fail(reason, "skeletal scene pipeline creation failed");
    if (!createDeferred(m_deferredPipelines[2]))
        return false;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    if (FAILED(device->CreateGraphicsPipelineState(
            &desc, IID_PPV_ARGS(m_pipelineDetail.GetAddressOf()))))
        return Fail(reason, "level detail pipeline creation failed");
    return true;
}

bool GameDx12StaticScene12::CreateSunShadowPipeline(ID3D12Device* device, std::string& reason)
{
    constexpr const char* entry = "shadowMain";
    constexpr const char* vertexProfile = "vs_5_0";
    constexpr const char* pixelProfile = "ps_5_0";
    ComPtr<ID3D10Blob> vertex;
    ComPtr<ID3D10Blob> pixel;
    if (!CompileShader(kSceneVertexShader, entry, vertexProfile, vertex.GetAddressOf(), reason) ||
        !CompileShader(kScenePixelShader, entry, pixelProfile, pixel.GetAddressOf(), reason))
        return false;

    // Depth-only проход. PS оставляет фиксированную глубину и отсекает
    // alpha-tested пиксели по тем же TEXCOORD0 и порогу, что в основной сцене.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadow{};
    shadow.pRootSignature = m_rootSignature.Get();
    shadow.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    shadow.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    shadow.SampleMask = UINT_MAX;
    shadow.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    shadow.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    shadow.RasterizerState.DepthClipEnable = TRUE;
    shadow.RasterizerState.DepthBias = 8;
    shadow.RasterizerState.SlopeScaledDepthBias = 0.85f;
    shadow.RasterizerState.DepthBiasClamp = 0.00004f;
    shadow.DepthStencilState.DepthEnable = TRUE;
    shadow.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadow.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadow.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadow.NumRenderTargets = 0;
    shadow.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadow.SampleDesc.Count = 1;

    static_assert(std::size(kStaticInputLayouts) == std::tuple_size_v<decltype(m_sunShadowPipelines)>);
    constexpr const char* layoutNames[] = {"classic", "uv24"};
    for (size_t i = 0; i < std::size(kStaticInputLayouts); ++i)
    {
        shadow.InputLayout = kStaticInputLayouts[i];
        const HRESULT hr = device->CreateGraphicsPipelineState(&shadow,
            IID_PPV_ARGS(m_sunShadowPipelines[i].GetAddressOf()));
        if (FAILED(hr))
        {
            char diagnostic[256]{};
            std::snprintf(diagnostic, sizeof(diagnostic),
                "CreateGraphicsPipelineState PSO=sun_shadow layout=%s HRESULT=0x%08X "
                "VS=%s/%s PS=%s/%s RTVCount=%u DSV=%u samples=%u depth_write=%u depth_func=%u cull=%u",
                layoutNames[i], static_cast<unsigned>(hr), entry, vertexProfile, entry, pixelProfile,
                shadow.NumRenderTargets, static_cast<unsigned>(shadow.DSVFormat), shadow.SampleDesc.Count,
                static_cast<unsigned>(shadow.DepthStencilState.DepthWriteMask),
                static_cast<unsigned>(shadow.DepthStencilState.DepthFunc),
                static_cast<unsigned>(shadow.RasterizerState.CullMode));
            reason = diagnostic;
            return false;
        }
    }
    if (SceneDynamicSunShadowsRequested() || SceneLocalShadowsRequested() || SceneDetailSunShadowsRequested())
    {
        ComPtr<ID3D10Blob> auxVertex;
        std::string auxReason;
        if (CompileShader(kAuxVertexShader, entry, vertexProfile, auxVertex.GetAddressOf(), auxReason))
        {
            shadow.VS = {auxVertex->GetBufferPointer(), auxVertex->GetBufferSize()};
            shadow.InputLayout = kAuxInputLayout;
            const HRESULT hr = device->CreateGraphicsPipelineState(
                &shadow, IID_PPV_ARGS(m_sunShadowAuxPipeline.GetAddressOf()));
            if (FAILED(hr))
            {
                char message[96]{};
                std::snprintf(message, sizeof(message), "skeletal sun shadow PSO HRESULT=0x%08X",
                    static_cast<unsigned>(hr));
                auxReason = message;
            }
        }
        if (!auxReason.empty())
        {
            m_sunShadowAuxPipeline.Reset();
            Msg("ph_dx12_f43e: sun_shadow_dynamic state=fallback reason='%s'", auxReason.c_str());
            if (FAILED(device->GetDeviceRemovedReason()))
            {
                reason = auxReason;
                return false;
            }
        }
    }
    return true;
}

bool GameDx12StaticScene12::CreateFrameConstants(ID3D12Device* device,
                                                 std::string& reason)
{
    const uint64_t bytes = kFrameConstantsSize * kFrameConstantsSlots;
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    const auto desc = BufferDesc(bytes);
    const HRESULT hr = device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(m_frameConstants.GetAddressOf()));
    if (FAILED(hr))
        return Fail(reason, "static scene constant buffer creation failed");
    const D3D12_RANGE noRead{};
    if (FAILED(m_frameConstants->Map(0, &noRead, &m_frameConstantsMapped)))
        return Fail(reason, "static scene constant buffer mapping failed");
    std::memset(m_frameConstantsMapped, 0, static_cast<size_t>(bytes));
    return true;
}

bool GameDx12StaticScene12::CreateSunShadow(ID3D12Device* device, std::string& reason)
{
    const auto& settings = SceneSunSettings();
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC image{};
    image.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    image.MipLevels = 1;
    image.Format = DXGI_FORMAT_R32_TYPELESS;
    image.SampleDesc.Count = 1;
    image.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    const auto allocate = [&](uint32_t slices, uint32_t resolution)
    {
        image.Width = image.Height = resolution;
        image.DepthOrArraySize = static_cast<UINT16>(slices);
        ComPtr<ID3D12Resource> maps;
        HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &image,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(maps.GetAddressOf()));
        if (FAILED(hr))
            return hr;
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        desc.NumDescriptors = slices;
        ComPtr<ID3D12DescriptorHeap> views;
        hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(views.GetAddressOf()));
        if (FAILED(hr))
            return hr;
        auto handle = views->GetCPUDescriptorHandleForHeapStart();
        const auto stride = device->GetDescriptorHandleIncrementSize(desc.Type);
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Texture2DArray.ArraySize = 1;
        for (uint32_t slice = 0; slice < slices; ++slice, handle.ptr += stride)
        {
            dsv.Texture2DArray.FirstArraySlice = slice;
            device->CreateDepthStencilView(maps.Get(), &dsv, handle);
        }
        m_sunShadowBytes = device->GetResourceAllocationInfo(0, 1, &image).SizeInBytes;
        m_sunShadowResolution = resolution;
        m_sunShadowSlices = slices;
        m_sunShadowDsvStride = stride;
        m_sunShadow = std::move(maps);
        m_sunShadowDsv = std::move(views);
        return S_OK;
    };
    HRESULT hr = allocate(settings.count, settings.resolution);
    if (FAILED(hr) && (settings.count != 1 || settings.resolution != game_shadow12::DefaultResolution) &&
        SUCCEEDED(device->GetDeviceRemovedReason()))
    {
        Msg("ph_dx12_csm: allocation fallback requested=%u/%u hr=0x%08X effective=1/2048",
            settings.count, settings.resolution, static_cast<unsigned>(hr));
        hr = allocate(1, game_shadow12::DefaultResolution);
    }
    if (FAILED(hr))
    {
        Msg("ph_dx12_csm: allocation failed hr=0x%08X", static_cast<unsigned>(hr));
        return Fail(reason, "sun shadow DSV heap allocation failed");
    }
    return true;
}

bool GameDx12StaticScene12::CreateLocalShadow(ID3D12Device* device, std::string& reason)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC image{};
    image.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    image.Width = kLocalShadowSize;
    image.Height = kLocalShadowSize;
    image.MipLevels = 1;
    image.Format = DXGI_FORMAT_R32_TYPELESS;
    image.SampleDesc.Count = 1;
    image.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    const uint32_t requested = SceneLocalShadowBanksRequested();
    for (uint32_t banks = requested; banks != 0; --banks)
    {
        const uint32_t slices = banks * game_shadow12::LayersPerBank;
        image.DepthOrArraySize = static_cast<UINT16>(slices);
        ComPtr<ID3D12Resource> maps;
        ComPtr<ID3D12DescriptorHeap> descriptors;
        HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &image,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(maps.GetAddressOf()));
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        desc.NumDescriptors = slices;
        if (SUCCEEDED(hr))
            hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(descriptors.GetAddressOf()));
        if (FAILED(hr))
        {
            Msg("ph_dx12_lsa: allocation_failed requested_banks=%u attempted_banks=%u hr=0x%08X",
                requested, banks, static_cast<unsigned>(hr));
            if (FAILED(device->GetDeviceRemovedReason()))
                break;
            continue;
        }
        const auto stride = device->GetDescriptorHandleIncrementSize(desc.Type);
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Texture2DArray.ArraySize = 1;
        auto handle = descriptors->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t layer = 0; layer < slices; ++layer, handle.ptr += stride)
        {
            dsv.Texture2DArray.FirstArraySlice = layer;
            device->CreateDepthStencilView(maps.Get(), &dsv, handle);
        }
        m_localShadowSlices = slices;
        m_localShadowBytes = device->GetResourceAllocationInfo(0, 1, &image).SizeInBytes;
        m_localShadowDsvStride = stride;
        m_localShadow = std::move(maps);
        m_localShadowDsv = std::move(descriptors);
        m_localShadowAllocation = {};
        Msg("ph_dx12_lsa: allocation_ready requested_banks=%u effective_banks=%u slices=%u bytes=%llu suppress_unmapped=%d",
            requested, banks, slices, static_cast<unsigned long long>(m_localShadowBytes),
            SceneSuppressUnmappedLocalLightsRequested() ? 1 : 0);
        return true;
    }
    return Fail(reason, "local shadow bank allocation failed");
}

bool GameDx12StaticScene12::CreateInstanceBuffer(ID3D12Device* device,
                                                 std::string& reason)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    const auto desc = BufferDesc(kTransformSlotBytes * kFrameConstantsSlots);
    const D3D12_RANGE noRead{};
    if (FAILED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(m_frameTransforms.GetAddressOf()))) ||
        FAILED(m_frameTransforms->Map(0, &noRead, &m_frameTransformsMapped)))
        return Fail(reason, "scene transform buffer allocation failed");
    return true;
}

bool GameDx12StaticScene12::PrepareUpload(
    const GameDx12StaticSceneOwner12& owner, std::span<const uint8_t> geomBytes,
    const std::vector<GameDx12SceneTexture12>& textures, std::string& reason)
{
    ID3D12Device* device = owner.device;
    (void)device;
    HRESULT hr = owner.allocator->Reset();
    if (FAILED(hr))
        return Fail(reason, "static scene upload allocator reset failed");
    hr = owner.list->Reset(owner.allocator, nullptr);
    if (FAILED(hr))
        return Fail(reason, "static scene upload list reset failed");

    if (!CreatePools(owner, geomBytes, reason))
        return false;
    if (!CreateTextures(owner, textures, false, reason))
        return false;
    return true;
}

// Копирование через один staging-буфер: чанк ограничен свободным остатком,
// а при заполнении — Close/Execute/Signal/Wait/Reset. Так полный уровень не
// требует staging размером во весь level.geom.
bool GameDx12StaticScene12::CreatePools(
    const GameDx12StaticSceneOwner12& owner, std::span<const uint8_t> geomBytes,
    std::string& reason)
{
    ID3D12Device* device = owner.device;
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    const auto stagingDesc = BufferDesc(kStagingBytes);
    auto& staging = m_copyStaging;
    if (FAILED(device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &stagingDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(staging.GetAddressOf()))))
        return Fail(reason, "static scene staging buffer creation failed");
    void* mapped = nullptr;
    const D3D12_RANGE noRead{};
    if (FAILED(staging->Map(0, &noRead, &mapped)))
        return Fail(reason, "static scene staging buffer mapping failed");

    uint64_t cursor = 0;
    const auto flush = [&]() -> bool
    {
        if (cursor == 0)
            return true;
        if (!SubmitAndWait(owner, reason))
            return false;
        if (FAILED(owner.allocator->Reset()) ||
            FAILED(owner.list->Reset(owner.allocator, nullptr)))
        {
            Fail(reason, "static scene upload list reset failed");
            return false;
        }
        cursor = 0;
        return true;
    };

    m_vertexPools.clear();
    m_indexPools.clear();
    m_vertexPools.resize(m_poolRefs.vertex_pools.size());
    m_indexPools.resize(m_poolRefs.index_pools.size());

    const auto uploadBuffer = [&](const xraylevel12::VertexPoolInfo* vb,
                                 const xraylevel12::IndexPoolInfo* ib,
                                 Buffer12& target) -> bool
    {
        uint64_t bytes = 0;
        uint64_t sourceOffset = 0;
        uint32_t stride = 1;
        uint32_t elements = 0;
        if (vb != nullptr)
        {
            stride = vb->stride;
            elements = vb->count;
            bytes = static_cast<uint64_t>(vb->count) * vb->stride;
            sourceOffset = vb->data_offset;
        }
        else
        {
            elements = ib->count;
            bytes = static_cast<uint64_t>(ib->count) * 2ull;
            sourceOffset = ib->data_offset;
        }
        if (bytes == 0 || sourceOffset > geomBytes.size() ||
            bytes > geomBytes.size() - static_cast<size_t>(sourceOffset))
        {
            Fail(reason, "static scene pool range exceeds level.geom");
            return false;
        }
        if (bytes > static_cast<uint64_t>(std::numeric_limits<UINT>::max()))
        {
            Fail(reason, "static scene pool exceeds one D3D12 buffer");
            return false;
        }
        const auto desc = BufferDesc(bytes);
        // Буферы D3D12 всегда создаются в COMMON: COPY_DEST для buffer
        // игнорируется и даёт предупреждение Debug Layer. Явный барьер ниже
        // переводит ресурс в состояние чтения.
        if (FAILED(device->CreateCommittedResource(
                &defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(target.resource.GetAddressOf()))))
        {
            Fail(reason, "static scene pool allocation failed");
            return false;
        }
        target.bytes = bytes;

        uint64_t copied = 0;
        while (copied < bytes)
        {
            if (cursor == kStagingBytes)
            {
                if (!flush())
                    return false;
            }
            const uint64_t available = kStagingBytes - cursor;
            const uint64_t chunk = (std::min)(available, bytes - copied);
            std::memcpy(static_cast<uint8_t*>(mapped) + cursor,
                        geomBytes.data() + sourceOffset + copied,
                        static_cast<size_t>(chunk));
            owner.list->CopyBufferRegion(target.resource.Get(), copied, staging.Get(),
                                         cursor, chunk);
            cursor += (chunk + kStagingAlignment - 1) / kStagingAlignment *
                      kStagingAlignment;
            if (cursor > kStagingBytes)
                cursor = kStagingBytes;
            copied += chunk;
        }

        // Буферы в D3D12 не имеют реального состояния: COMMON совместим с
        // любым использованием, а transition barrier для buffer даёт
        // предупреждение Debug Layer. Поэтому барьер не ставится вовсе.
        (void)stride;
        (void)elements;
        return true;
    };

    uint64_t geometryBytes = 0;
    for (size_t index = 0; index < m_poolRefs.vertex_pools.size(); ++index)
    {
        if (!uploadBuffer(&m_poolRefs.vertex_pools[index], nullptr,
                          m_vertexPools[index]))
            return false;
        geometryBytes += m_vertexPools[index].bytes;
    }
    for (size_t index = 0; index < m_poolRefs.index_pools.size(); ++index)
    {
        if (!uploadBuffer(nullptr, &m_poolRefs.index_pools[index],
                          m_indexPools[index]))
            return false;
        geometryBytes += m_indexPools[index].bytes;
    }
    if (!flush())
        return false;
    staging->Unmap(0, nullptr);
    staging.Reset();
    m_stats.geometry_bytes = geometryBytes;
    m_stats.vertex_pools = static_cast<uint32_t>(m_vertexPools.size());
    m_stats.index_pools = static_cast<uint32_t>(m_indexPools.size());

    // Предвычисленные view-объекты: адреса валидны, пока живы пулы.
    for (auto& draw : m_draws)
    {
        if (draw.vbPool >= m_vertexPools.size() ||
            draw.ibPool >= m_indexPools.size())
            return Fail(reason, "static scene draw references a missing pool");
        const auto& vbInfo = m_poolRefs.vertex_pools[draw.vbPool];
        const auto& ibInfo = m_poolRefs.index_pools[draw.ibPool];
        draw.vertexView.BufferLocation =
            m_vertexPools[draw.vbPool].resource->GetGPUVirtualAddress() +
            static_cast<uint64_t>(draw.vbase) * vbInfo.stride;
        draw.vertexView.SizeInBytes = draw.vcount * vbInfo.stride;
        draw.vertexView.StrideInBytes = vbInfo.stride;
        draw.indexView.BufferLocation =
            m_indexPools[draw.ibPool].resource->GetGPUVirtualAddress() +
            static_cast<uint64_t>(draw.ibase) * 2ull;
        draw.indexView.SizeInBytes = draw.icount * 2u;
        draw.indexView.Format = DXGI_FORMAT_R16_UINT;
        (void)ibInfo;
    }
    return true;
}

bool GameDx12StaticScene12::CreateTextures(
    const GameDx12StaticSceneOwner12& owner,
    const std::vector<GameDx12SceneTexture12>& textures, bool append,
    std::string& reason)
{
    ID3D12Device* device = owner.device;
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
    nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrv.Texture2D.MipLevels = 1;
    if (!append)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = kSceneDescriptorCount;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&heap,
                IID_PPV_ARGS(m_textureHeap.GetAddressOf()))))
            return Fail(reason, "scene descriptor heap creation failed");
        m_textureDescriptorSize = device->GetDescriptorHandleIncrementSize(heap.Type);
        if (m_textureDescriptorSize == 0 || (m_sunShadow &&
            (m_sunShadowSlices == 0 || m_sunShadowSlices > game_shadow12::MaxSunCascades ||
             m_sunShadow->GetDesc().DepthOrArraySize != m_sunShadowSlices)))
            return Fail(reason, "invalid scene shadow descriptor layout");
        const auto base = m_textureHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateShaderResourceView(nullptr, &nullSrv, base);
        auto shadowSrv = nullSrv;
        shadowSrv.Format = DXGI_FORMAT_R32_FLOAT;
        auto shadow = base;
        shadow.ptr += static_cast<SIZE_T>(kSunShadowDescriptor) * m_textureDescriptorSize;
        auto arraySrv = shadowSrv;
        arraySrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        arraySrv.Texture2DArray = {};
        arraySrv.Texture2DArray.MipLevels = 1;
        arraySrv.Texture2DArray.ArraySize = m_sunShadow ? m_sunShadowSlices : 1;
        device->CreateShaderResourceView(m_sunShadow.Get(), &arraySrv, shadow);

        auto localSrv = arraySrv;
        if (m_localShadow && (!game_shadow12::ValidBanks(m_localShadowSlices / game_shadow12::LayersPerBank) ||
            m_localShadowSlices % game_shadow12::LayersPerBank != 0 ||
            m_localShadow->GetDesc().DepthOrArraySize != m_localShadowSlices))
            return Fail(reason, "invalid local shadow bank descriptor layout");
        localSrv.Texture2DArray.ArraySize = m_localShadow ? m_localShadowSlices : game_shadow12::LayersPerBank;
        auto local = base;
        local.ptr += static_cast<SIZE_T>(kLocalShadowDescriptor) * m_textureDescriptorSize;
        device->CreateShaderResourceView(m_localShadow.Get(), &localSrv, local);
        // У воды, массива теней и terrain непересекающиеся интервалы.
        for (uint32_t index = kNullWaterDescriptor; index < kWaterDescriptorEnd; ++index)
        {
            auto descriptor = base;
            descriptor.ptr += static_cast<SIZE_T>(index) * m_textureDescriptorSize;
            device->CreateShaderResourceView(nullptr,
                (index - kNullWaterDescriptor) % kWaterSrvCount == 2 ? &shadowSrv : &nullSrv, descriptor);
        }
        for (uint32_t index = kTerrainDescriptor; index < kSceneDescriptorCount; ++index)
        {
            auto descriptor = base;
            descriptor.ptr += static_cast<SIZE_T>(index) * m_textureDescriptorSize;
            device->CreateShaderResourceView(nullptr, &nullSrv, descriptor);
        }
        Msg("ph_dx12_f43fr2: local_shadow_srv t=6 slot=%u dimension=2d_array format=R32_FLOAT "
            "slices=%u resource=%d water_end=%u terrain_start=%u", kLocalShadowDescriptor,
            localSrv.Texture2DArray.ArraySize, m_localShadow ? 1 : 0, kWaterDescriptorEnd, kTerrainDescriptor);
    }
    if (textures.empty())
        return true;
    const size_t baseIndex = m_textures.size();
    if (textures.size() > kMaxTextures - baseIndex)
        return Fail(reason, "scene texture descriptor budget exhausted");
    auto cpu = m_textureHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += baseIndex * m_textureDescriptorSize;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    const auto stagingDesc = BufferDesc(kStagingBytes);
    auto& staging = m_copyStaging;
    if (FAILED(device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &stagingDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(staging.GetAddressOf()))))
        return Fail(reason, "static scene texture staging creation failed");
    void* mapped = nullptr;
    const D3D12_RANGE noRead{};
    if (FAILED(staging->Map(0, &noRead, &mapped)))
        return Fail(reason, "static scene texture staging mapping failed");

    const auto flush = [&]() -> bool
    {
        if (!SubmitAndWait(owner, reason))
            return false;
        if (FAILED(owner.allocator->Reset()) ||
            FAILED(owner.list->Reset(owner.allocator, nullptr)))
        {
            Fail(reason, "static scene texture list reset failed");
            return false;
        }
        return true;
    };

    m_textures.resize(baseIndex + textures.size());
    const uint64_t budget = append ? kMaxAuxTextureBytes - m_auxResources.textureBytes
                                   : kMaxSceneTextureBytes;
    uint64_t cursor = 0;
    uint64_t textureBytes = 0;
    uint32_t uploaded = 0;
    uint32_t failed = 0;

    for (size_t index = 0; index < textures.size(); ++index)
    {
        cpu.ptr += m_textureDescriptorSize;
        const auto& source = textures[index];
        device->CreateShaderResourceView(nullptr, &nullSrv, cpu);
        m_textureKeys.emplace(source.name, static_cast<uint32_t>(baseIndex + index));
        const auto& dds = source.dds;
        if (dds.data.empty() || dds.mipCount == 0)
        {
            ++failed;
            continue;
        }
        const bool blockCompressed =
            dds.format == DXGI_FORMAT_BC1_UNORM ||
            dds.format == DXGI_FORMAT_BC2_UNORM ||
            dds.format == DXGI_FORMAT_BC3_UNORM ||
            dds.format == DXGI_FORMAT_BC4_UNORM ||
            dds.format == DXGI_FORMAT_BC5_UNORM ||
            dds.format == DXGI_FORMAT_BC7_UNORM;
        DXGI_FORMAT typeless = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
        switch (dds.format)
        {
        case DXGI_FORMAT_BC1_UNORM:
            typeless = DXGI_FORMAT_BC1_TYPELESS;
            srvFormat = DXGI_FORMAT_BC1_UNORM_SRGB;
            break;
        case DXGI_FORMAT_BC2_UNORM:
            typeless = DXGI_FORMAT_BC2_TYPELESS;
            srvFormat = DXGI_FORMAT_BC2_UNORM_SRGB;
            break;
        case DXGI_FORMAT_BC3_UNORM:
            typeless = DXGI_FORMAT_BC3_TYPELESS;
            srvFormat = DXGI_FORMAT_BC3_UNORM_SRGB;
            break;
        case DXGI_FORMAT_BC4_UNORM:
            typeless = DXGI_FORMAT_BC4_TYPELESS;
            srvFormat = DXGI_FORMAT_BC4_UNORM;
            break;
        case DXGI_FORMAT_BC5_UNORM:
            typeless = DXGI_FORMAT_BC5_TYPELESS;
            srvFormat = DXGI_FORMAT_BC5_UNORM;
            break;
        case DXGI_FORMAT_BC7_UNORM:
            typeless = DXGI_FORMAT_BC7_TYPELESS;
            srvFormat = DXGI_FORMAT_BC7_UNORM_SRGB;
            break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            typeless = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            srvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            break;
        default:
            ++failed;
            Msg("ph_dx12_f41: texture unsupported name='%s' format=%u",
                source.name.c_str(), static_cast<unsigned>(dds.format));
            continue;
        }
        (void)blockCompressed;
        if (source.name == gamewater12::WaterNormal)
            srvFormat = dds.format; // Карта нормалей хранит линейные компоненты.

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = dds.width;
        desc.Height = dds.height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = static_cast<UINT16>(dds.mipCount);
        desc.Format = typeless;
        desc.SampleDesc.Count = 1;

        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(dds.mipCount);
        std::vector<UINT> rows(dds.mipCount);
        std::vector<UINT64> rowBytes(dds.mipCount);
        UINT64 total = 0;
        device->GetCopyableFootprints(&desc, 0, dds.mipCount, 0, layouts.data(),
                                      rows.data(), rowBytes.data(), &total);
        if (total > kStagingBytes || total > budget - textureBytes)
        {
            ++failed;
            Msg("ph_dx12_f41: texture too large name='%s' bytes=%llu",
                source.name.c_str(), static_cast<unsigned long long>(total));
            continue;
        }

        auto& texture = m_textures[baseIndex + index];
        texture.alphaTest = source.alphaTest;
        texture.footprintBytes = total;
        if (FAILED(device->CreateCommittedResource(
                &defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(texture.image.GetAddressOf()))))
            return Fail(reason, "static scene texture allocation failed");

        if (cursor + total > kStagingBytes)
        {
            if (!flush())
                return false;
            cursor = 0;
        }
        const uint64_t base = cursor;
        for (uint32_t mip = 0; mip < dds.mipCount; ++mip)
        {
            const auto& level = dds.mips[mip];
            if (level.dataOffset > dds.data.size() ||
                level.dataSize > dds.data.size() - level.dataOffset)
                return Fail(reason, "static scene texture source is truncated");
            const UINT rowCount = rows[mip];
            for (UINT row = 0; row < rowCount; ++row)
            {
                const uint64_t destination =
                    base + layouts[mip].Offset +
                    static_cast<uint64_t>(row) * layouts[mip].Footprint.RowPitch;
                const uint64_t sourceRow =
                    level.dataOffset + static_cast<uint64_t>(row) * level.rowPitch;
                std::memcpy(static_cast<uint8_t*>(mapped) + destination,
                            dds.data.data() + sourceRow, level.rowPitch);
            }
            D3D12_TEXTURE_COPY_LOCATION from = {};
            from.pResource = staging.Get();
            from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint = layouts[mip];
            from.PlacedFootprint.Offset += base;
            D3D12_TEXTURE_COPY_LOCATION into = {};
            into.pResource = texture.image.Get();
            into.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            into.SubresourceIndex = mip;
            owner.list->CopyTextureRegion(&into, 0, 0, 0, &from, nullptr);
        }
        cursor += (total + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) /
                  D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT *
                  D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = texture.image.Get();
            barrier.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter =
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            owner.list->ResourceBarrier(1, &barrier);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = srvFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = dds.mipCount;
        device->CreateShaderResourceView(texture.image.Get(), &srv, cpu);
        texture.srvFormat = srvFormat;

        textureBytes += total;
        ++uploaded;
    }

    if (cursor != 0 && !flush())
        return false;
    staging->Unmap(0, nullptr);
    staging.Reset();

    if (append)
    {
        m_auxResources.texturesUploaded += uploaded;
        m_auxResources.textureBytes += textureBytes;
    }
    else
    {
        m_stats.textures_requested = static_cast<uint32_t>(textures.size());
        m_stats.textures_uploaded = uploaded;
        m_stats.textures_failed = failed;
        m_stats.texture_bytes = textureBytes;
    }
    return true;
}

bool GameDx12StaticScene12::SubmitAndWait(
    const GameDx12StaticSceneOwner12& owner, std::string& reason)
{
    if (FAILED(owner.list->Close()))
        return Fail(reason, "scene upload list close failed");
    if (*owner.fenceValue >= UINT64_MAX - 1)
        return Fail(reason, "scene upload fence overflow");
    m_gpuUnknown = true;
    ID3D12CommandList* lists[] = {owner.list};
    owner.queue->ExecuteCommandLists(1, lists);
    const uint64_t expected = ++(*owner.fenceValue);
    if (FAILED(owner.queue->Signal(owner.fence, expected)))
        return Fail(reason, "static scene fence signal failed");
    if (owner.fence->GetCompletedValue() < expected &&
        FAILED(owner.fence->SetEventOnCompletion(expected, owner.fenceEvent)))
        return Fail(reason, "static scene fence event failed");
    if (owner.fence->GetCompletedValue() < expected &&
        WaitForSingleObject(owner.fenceEvent, kFenceTimeoutMs) != WAIT_OBJECT_0)
        return Fail(reason, "static scene fence wait failed");
    const uint64_t completed = owner.fence->GetCompletedValue();
    if (completed == UINT64_MAX || completed < expected ||
        owner.device->GetDeviceRemovedReason() != S_OK)
        return Fail(reason, "static scene completion unconfirmed");
    m_uploadFenceValue = expected;
    m_gpuUnknown = false;
    return true;
}

bool GameDx12StaticScene12::Load(
    const GameDx12StaticSceneOwner12& owner, const char* levelName,
    uint64_t levelSession, uint64_t levelEpoch,
    const xraylevel12::LevelPools& pools, std::span<const uint8_t> geomBytes,
    const std::vector<GameDx12SceneTexture12>& textures,
    const std::vector<GameDx12StaticMaterial12>& materials, std::string& reason)
{
    if (owner.device == nullptr || owner.queue == nullptr ||
        owner.fence == nullptr || owner.fenceEvent == nullptr ||
        owner.allocator == nullptr || owner.list == nullptr ||
        owner.fenceValue == nullptr)
        return Fail(reason, "invalid static scene owner context");
    if (m_loaded || m_gpuUnknown || m_uploadSubmitted)
        return Fail(reason, "static scene is already loaded");
    if (levelName == nullptr || levelName[0] == '\0')
        return Fail(reason, "missing static scene level name");
    if (levelSession == 0 || levelEpoch == 0)
        return Fail(reason, "invalid static scene identity");
    if (geomBytes.empty())
        return Fail(reason, "empty level.geom payload");
    if (materials.size() != pools.visuals.size())
        return Fail(reason, "material list does not match the visual list");
    if (textures.size() > kMaxTextures)
        return Fail(reason, "static scene texture count exceeds capacity");

    try
    {
        m_stats = LoadStats12{};
        m_stats.visuals_total = static_cast<uint32_t>(pools.visuals.size());
        m_stats.visuals_drawable = pools.visuals_drawable;
        m_stats.visuals_rejected = pools.visuals_rejected;
        m_poolRefs = pools;
        m_levelName = levelName;
        m_levelSession = levelSession;
        m_levelEpoch = levelEpoch;
        m_visualTrace = {};
        m_visualTraceFrame = 0;
        m_auxTraceMessages = 0;
        m_generation = (levelSession * 0x9E3779B97F4A7C15ull) ^
                       (levelEpoch * 0xBF58476D1CE4E5B9ull) ^
                       0x4634314C4C534354ull;
        if (m_generation == 0)
            m_generation = 1;

        m_draws.clear();
        m_draws.reserve(pools.visuals_drawable);
        const auto& selected = R1VisualSelection();
        std::array<bool, 2> seen{};
        std::vector<int8_t> cutoutCoverage(textures.size(), -1);
        std::vector<std::array<uint32_t, kTerrainTextureCount>> terrainSets;
        for (size_t index = 0; index < pools.visuals.size(); ++index)
        {
            const auto& visual = pools.visuals[index];
            const uint8_t traceSlot = R1VisualTraceSlot(selected, visual.id);
            if (traceSlot != UINT8_MAX)
                seen[traceSlot] = true;
            if (!visual.drawable)
            {
                if (traceSlot != UINT8_MAX)
                    Msg("ph_dx12_f43_r1: static visual_id=%u type=%u shader_id=%u "
                        "state=rejected reason='%s'",
                        visual.id, static_cast<unsigned>(visual.type), visual.shader_id,
                        xraylevel12::ReadErrorMessage(visual.reject));
                continue;
            }
            const auto& material = materials[index];
            if (material.hasOverride)
                ++m_stats.material_overrides;
            if (material.surface == SurfaceMode::Unsupported)
            {
                ++m_stats.unsupported_materials;
                if (traceSlot != UINT8_MAX)
                    Msg("ph_dx12_f43_r1: static visual_id=%u shader_id=%u "
                        "state=rejected reason='unsupported_material'",
                        visual.id, visual.shader_id);
                continue;
            }
            DrawSource12 draw;
            draw.visualId = visual.id;
            draw.vbPool = visual.vb_id;
            draw.ibPool = visual.ib_id;
            draw.vbase = visual.vbase;
            draw.vcount = visual.vcount;
            draw.ibase = visual.ibase;
            draw.icount = visual.icount;
            draw.shaderId = visual.shader_id;
            draw.traceSlot = traceSlot;
            const bool haveTexture = material.textureIndex < textures.size();
            if (haveTexture)
                draw.descriptor = material.textureIndex + 1;
            else if (material.textureIndex != GameDx12StaticMaterial12::kNoTexture)
                return Fail(reason, "static material references an out-of-range texture");
            bool binaryAlpha = haveTexture && textures[material.textureIndex].alphaTest;
            if (haveTexture && !binaryAlpha && material.checkAlphaCoverage)
            {
                auto& cached = cutoutCoverage[material.textureIndex];
                if (cached < 0)
                    cached = HasCutoutCoverage(textures[material.textureIndex].dds) ? 1 : 0;
                binaryAlpha = cached == 1;
            }
            draw.surface = ResolveSurface(material.surface, binaryAlpha);
            draw.wallmarkClamp = material.wallmarkClamp;
            draw.vertexHemi = !visual.has_world && !material.usesHemiLightmap &&
                visual.layout == xraylevel12::VertexLayout12::Classic;
            if (!visual.has_world && material.hemiTextureIndex < textures.size() &&
                HasPackedLightmapUv(visual, pools))
                draw.hemiDescriptor = material.hemiTextureIndex + 1;
            draw.terrainBaseHemi = !visual.has_world && haveTexture && material.terrainBaseHemi;
            if (!visual.has_world && material.terrainScale > 0.0f)
            {
                const auto found = std::find(terrainSets.begin(), terrainSets.end(), material.terrainTextures);
                if (found != terrainSets.end())
                    draw.terrainSet = static_cast<uint32_t>(std::distance(terrainSets.begin(), found)) + 1;
                else if (terrainSets.size() < kMaxTerrainSets)
                {
                    terrainSets.push_back(material.terrainTextures);
                    draw.terrainSet = static_cast<uint32_t>(terrainSets.size());
                }
                draw.terrainScale = material.terrainScale;
            }
            draw.transparency = material.transparency;
            draw.layout =
                visual.layout == xraylevel12::VertexLayout12::UvAt24 ? 2 : 1;
            for (int axis = 0; axis < 3; ++axis)
                draw.center[axis] = visual.sphere_center[axis];
            draw.radius = visual.sphere_radius;
            if (visual.has_world)
            {
                draw.layout = 3;
                for (uint32_t element = 0; element < 16; ++element)
                    draw.world[element] = visual.world[element];
                float center[3]{};
                float radius = 0.0f;
                static const bool legacyFloraBounds = gamewater12::Enabled("OGSR_DX12_GAME_LEGACY_FLORA_BOUNDS");
                if (!legacyFloraBounds)
                {
                    if (BuildTreeWorldBounds(visual, pools, geomBytes, center, radius))
                    {
                        for (uint32_t axis = 0; axis < 3; ++axis)
                            draw.center[axis] = center[axis];
                        draw.radius = radius;
                    }
                    else
                        Msg("ph_dx12_fr3: tree_bounds invalid visual=%u; retaining header bounds", visual.id);
                }
            }
            m_draws.push_back(draw);
        }
        for (size_t slot = 0; slot < selected.size(); ++slot)
            if (selected[slot] != UINT32_MAX && !seen[slot])
                Msg("ph_dx12_f43_r1: static visual_id=%u state=rejected "
                    "reason='visual_not_in_level_pool'", selected[slot]);
        if (m_draws.empty())
            return Fail(reason, "level has no drawable static geometry");

        if (!CreatePipeline(owner.device, reason) ||
            !CreateFrameConstants(owner.device, reason) ||
            !CreateInstanceBuffer(owner.device, reason))
        {
            ReleaseGpu();
            return false;
        }

        const bool sunShadowsRequested = SceneSunShadowsRequested();
        const bool localShadowsRequested = SceneLocalShadowsRequested();
        if (sunShadowsRequested || localShadowsRequested)
        {
            std::string shadowReason;
            if (!CreateSunShadowPipeline(owner.device, shadowReason))
            {
                m_sunShadowPipelines = {};
                m_sunShadowAuxPipeline.Reset();
                Msg("ph_dx12_f43e: shadow_pso state=fallback reason='%s'", shadowReason.c_str());
            }
            else
            {
                if (sunShadowsRequested)
                {
                    if (CreateSunShadow(owner.device, shadowReason))
                        Msg("ph_dx12_f43b: sun_shadow state=ready format=D32_FLOAT size=%u slices=%u bytes=%llu",
                            m_sunShadowResolution, m_sunShadowSlices, static_cast<unsigned long long>(m_sunShadowBytes));
                    else
                    {
                        m_sunShadow.Reset();
                        m_sunShadowDsv.Reset();
                        Msg("ph_dx12_f43b: sun_shadow state=fallback reason='%s'", shadowReason.c_str());
                    }
                }
                if (localShadowsRequested)
                {
                    if (CreateLocalShadow(owner.device, shadowReason))
                        Msg("ph_dx12_f43e: local_shadow state=ready size=%u layers=%u",
                            kLocalShadowSize, m_localShadowSlices);
                    else
                    {
                        m_localShadow.Reset();
                        m_localShadowDsv.Reset();
                        Msg("ph_dx12_f43e: local_shadow state=fallback reason='%s'", shadowReason.c_str());
                    }
                }
            }
            if (FAILED(owner.device->GetDeviceRemovedReason()))
            {
                ReleaseGpu();
                return Fail(reason, "device removed during shadow initialization");
            }
        }

        const auto poolsStart = Clock::now();
        if (!PrepareUpload(owner, geomBytes, textures, reason))
        {
            if (m_gpuUnknown)
                QuarantineGpu();
            else
            {
                (void)owner.list->Close();
                ReleaseGpu();
            }
            return false;
        }
        m_stats.pools_ms = Ms(poolsStart);
        if (FAILED(owner.list->Close()))
        {
            ReleaseGpu();
            return Fail(reason, "scene upload final close failed");
        }

        for (auto& texture : m_textures)
            texture.upload.Reset();
        m_uploadConfirmed = true;

        std::array<bool, kMaxTerrainSets + 1> terrainReady{};
        uint32_t terrainReadyCount = 0;
        auto terrainCpu = m_textureHeap->GetCPUDescriptorHandleForHeapStart();
        terrainCpu.ptr += static_cast<SIZE_T>(kTerrainDescriptor + kTerrainTextureCount) * m_textureDescriptorSize;
        for (size_t setIndex = 0; setIndex < terrainSets.size(); ++setIndex)
        {
            const auto& set = terrainSets[setIndex];
            const bool complete = std::all_of(set.begin(), set.end(), [&](uint32_t index)
            {
                return index < m_textures.size() && m_textures[index].image != nullptr;
            });
            if (complete)
            {
                for (uint32_t index : set)
                {
                    const auto& texture = m_textures[index];
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                    // Маска и слои читаются как записанные UNORM значения R4.
                    // Преобразование цвета выполняется после смешивания.
                    srv.Format = textures[index].dds.format;
                    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srv.Texture2D.MipLevels = texture.image->GetDesc().MipLevels;
                    owner.device->CreateShaderResourceView(texture.image.Get(), &srv, terrainCpu);
                    terrainCpu.ptr += m_textureDescriptorSize;
                }
                terrainReady[setIndex + 1] = true;
                ++terrainReadyCount;
            }
            else
                terrainCpu.ptr += static_cast<SIZE_T>(kTerrainTextureCount) * m_textureDescriptorSize;
        }
        uint32_t lightmapDraws = 0;
        uint32_t terrainDraws = 0;
        for (auto& draw : m_draws)
        {
            // Есть запись DDS в таблице, но отсутствует загруженный SRV.
            // Обычная поверхность получит диагностический цвет, а вырезка
            // или накладка без текстуры пропускается.
            const uint32_t requestedDescriptor = draw.descriptor;
            const bool haveSrv = requestedDescriptor != kNullDescriptor &&
                m_textures[requestedDescriptor - 1].image != nullptr;
            const bool skip = !haveSrv && draw.surface != SurfaceMode::Opaque;
            if (!haveSrv)
            {
                if (draw.descriptor != kNullDescriptor || draw.surface != SurfaceMode::Opaque)
                    ++m_stats.material_missing_srv;
                draw.descriptor = kNullDescriptor;
                if (!skip)
                    ++m_stats.draws_without_texture;
            }
            if (!haveSrv)
                draw.terrainBaseHemi = false;
            if (draw.hemiDescriptor != kNullDescriptor && !m_textures[draw.hemiDescriptor - 1].image)
                draw.hemiDescriptor = kNullDescriptor;
            if (!terrainReady[draw.terrainSet])
                draw.terrainSet = 0;
            if (!skip && draw.hemiDescriptor != kNullDescriptor)
                ++lightmapDraws;
            if (!skip && draw.terrainSet != 0)
                ++terrainDraws;
            if (!skip && IsCutout(draw.surface))
                ++m_stats.drawing_alpha_test;
            if (draw.traceSlot != UINT8_MAX)
            {
                const auto* source = requestedDescriptor == kNullDescriptor
                    ? nullptr : &textures[requestedDescriptor - 1];
                const auto byteOffset = uint64_t{draw.vbase} * draw.vertexView.StrideInBytes;
                Msg("ph_dx12_f43f: material visual_id=%u shader_id=%u albedo_srv=%u hemi_srv=%u terrain_set=%u detail_scale=%.3f",
                    draw.visualId, draw.shaderId, draw.descriptor, draw.hemiDescriptor, draw.terrainSet, draw.terrainScale);
                Msg("ph_dx12_f43_r1: static visual_id=%u shader_id=%u texture='%s' "
                    "state=%s reason='%s' pso='%s' layout=%u srv=%u requested_srv=%u "
                    "dds_format=%u srv_format=%u thm_binary=%d "
                    "vb_pool=%u vbase=%u vcount=%u vb_offset=%llu vb_stride=%u vb_bytes=%u "
                    "ib_pool=%u ibase=%u icount=%u ib_offset=%llu ib_format=%u "
                    "start_index=0 base_vertex=0",
                    draw.visualId, draw.shaderId, source ? source->name.c_str() : "-",
                    skip ? "rejected" : "ready", haveSrv ? "none" : "no_srv",
                    gameaux12::SurfaceModeName(draw.surface), static_cast<unsigned>(draw.layout),
                    draw.descriptor, requestedDescriptor,
                    source ? static_cast<unsigned>(source->dds.format) : 0u,
                    haveSrv ? static_cast<unsigned>(m_textures[draw.descriptor - 1].srvFormat) : 0u,
                    source && source->alphaTest ? 1 : 0,
                    draw.vbPool, draw.vbase, draw.vcount,
                    static_cast<unsigned long long>(byteOffset), draw.vertexView.StrideInBytes,
                    draw.vertexView.SizeInBytes, draw.ibPool, draw.ibase, draw.icount,
                    static_cast<unsigned long long>(uint64_t{draw.ibase} * sizeof(uint16_t)),
                    static_cast<unsigned>(draw.indexView.Format));
            }
        }
        std::erase_if(m_draws, [](const DrawSource12& draw)
        {
            return draw.descriptor == kNullDescriptor && draw.surface != SurfaceMode::Opaque;
        });
        if (m_draws.empty())
        {
            ReleaseGpu();
            return Fail(reason, "level has no drawable static materials");
        }

        if (m_sunShadow != nullptr || m_localShadow != nullptr)
            BuildShadowCasterTrees();
        m_loaded = true;
        Msg("ph_dx12_f43f: material lightmap_draws=%u terrain_draws=%u terrain_sets=%u/%zu",
            lightmapDraws, terrainDraws, terrainReadyCount, terrainSets.size());
        Msg("ph_dx12_f41: scene loaded level='%s' session=%llu epoch=%llu "
            "visuals=%u drawable=%u rejected=%u draws=%zu vb_pools=%u ib_pools=%u "
            "geometry_bytes=%llu textures=%u/%u texture_bytes=%llu "
            "no_texture=%u alpha_test=%u upload_fence=%llu",
            m_levelName.c_str(), static_cast<unsigned long long>(m_levelSession),
            static_cast<unsigned long long>(m_levelEpoch),
            m_stats.visuals_total, m_stats.visuals_drawable,
            m_stats.visuals_rejected, m_draws.size(), m_stats.vertex_pools,
            m_stats.index_pools,
            static_cast<unsigned long long>(m_stats.geometry_bytes),
            m_stats.textures_uploaded, m_stats.textures_requested,
            static_cast<unsigned long long>(m_stats.texture_bytes),
            m_stats.draws_without_texture, m_stats.drawing_alpha_test,
            static_cast<unsigned long long>(m_uploadFenceValue));
        return true;
    }
    catch (const std::bad_alloc&)
    {
        if (m_gpuUnknown)
            QuarantineGpu();
        else
        {
            (void)owner.list->Close();
            ReleaseGpu();
        }
        return Fail(reason, "static scene allocation failed");
    }
    catch (const std::exception&)
    {
        if (m_gpuUnknown)
            QuarantineGpu();
        else
        {
            (void)owner.list->Close();
            ReleaseGpu();
        }
        return Fail(reason, "static scene exception");
    }
    catch (...)
    {
        if (m_gpuUnknown)
            QuarantineGpu();
        else
        {
            (void)owner.list->Close();
            ReleaseGpu();
        }
        return Fail(reason, "static scene unknown exception");
    }
}

bool GameDx12StaticScene12::RegisterMeshes(
    const GameDx12StaticSceneOwner12& owner,
    std::span<const gameaux12::Mesh* const> sources,
    std::vector<uint32_t>& handles, std::string& reason)
{
    handles.assign(sources.size(), gameaux12::kInvalidMesh);
    if (!m_loaded || !m_uploadConfirmed || m_gpuUnknown)
        return Fail(reason, "scene is unavailable for mesh registration");

    struct PendingMesh
    {
        uint32_t source;
        uint32_t requiredBones;
    };
    uint64_t bytes = 0;
    std::vector<PendingMesh> pending;
    std::vector<std::string> textureNames;
    std::unordered_set<std::string> unique;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        const auto& mesh = *sources[i];
        if (mesh.surface == SurfaceMode::Unsupported)
        {
            ++m_auxResources.unsupportedMaterial;
            continue;
        }
        if (mesh.vertices.empty() || mesh.indices.empty() ||
            mesh.vertices.size() > 65536 || mesh.indices.size() > UINT32_MAX / 2u)
            continue;
        if (mesh.texture.empty())
        {
            ++m_auxResources.missingMaterial;
            continue;
        }
        const auto meshBytes = mesh.vertices.size() * sizeof(gameaux12::Vertex) +
            ((mesh.indices.size() * sizeof(uint16_t) + 3u) & ~size_t{3});
        if (meshBytes > kMaxAuxGeometryBytes - m_auxResources.geometryBytes - bytes)
            continue;
        const uint32_t requiredBones = RequiredAuxBones(mesh);
        if (requiredBones == 0)
            continue;
        if (!m_textureKeys.contains(mesh.texture) && !unique.contains(mesh.texture))
        {
            if (textureNames.size() >= kMaxTextures - m_textures.size())
                continue;
            unique.emplace(mesh.texture);
            textureNames.push_back(mesh.texture);
        }
        pending.push_back({static_cast<uint32_t>(i), requiredBones});
        bytes += meshBytes;
    }
    if (pending.empty())
        return true;
    if (!textureNames.empty())
    {
        std::vector<GameDx12SceneTexture12> textures;
        GameDx12TextureReadStats12 stats;
        GameDx12J2ABatch_ReadTextureNames(textureNames, textures, stats,
            kMaxAuxTextureBytes - m_auxResources.textureBytes);
        if (textures.size() != textureNames.size())
            return Fail(reason, "additional texture read failed");
        if (FAILED(owner.allocator->Reset()) ||
            FAILED(owner.list->Reset(owner.allocator, nullptr)))
            return Fail(reason, "additional texture list reset failed");
        bool uploaded = false;
        try
        {
            uploaded = CreateTextures(owner, textures, true, reason);
        }
        catch (...)
        {
            if (!m_gpuUnknown)
                (void)owner.list->Close();
            throw;
        }
        if (!m_gpuUnknown)
        {
            const HRESULT close = owner.list->Close();
            if (uploaded && FAILED(close))
                return Fail(reason, "additional texture list close failed");
        }
        if (!uploaded)
            return false;
    }

    // Вместе храним VB и IB всех новых моделей. Редкие загрузки не
    // инвалидируют адреса уже записанных кадров или существующие дескрипторы.
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    const auto desc = BufferDesc(bytes);
    ComPtr<ID3D12Resource> geometry;
    if (FAILED(owner.device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(geometry.GetAddressOf()))))
        return Fail(reason, "additional mesh allocation failed");
    m_auxMeshes.reserve(m_auxMeshes.size() + pending.size());
    m_auxGeometry.reserve(m_auxGeometry.size() + 1);
    void* mapped = nullptr;
    const D3D12_RANGE noRead{};
    if (FAILED(geometry->Map(0, &noRead, &mapped)))
        return Fail(reason, "additional mesh map failed");
    const auto address = geometry->GetGPUVirtualAddress();
    uint64_t cursor = 0;
    for (const auto& candidate : pending)
    {
        const uint32_t i = candidate.source;
        const auto& source = *sources[i];
        const uint32_t texture = m_textureKeys.at(source.texture);
        if (m_textures[texture].image == nullptr)
        {
            ++m_auxResources.missingMaterial;
            continue;
        }
        const uint32_t vbBytes = static_cast<uint32_t>(
            source.vertices.size() * sizeof(gameaux12::Vertex));
        const uint32_t ibBytes = static_cast<uint32_t>(
            source.indices.size() * sizeof(uint16_t));
        AuxMesh mesh;
        mesh.descriptor = texture + 1;
        mesh.surface = ResolveSurface(source.surface,
                                     source.cutout || m_textures[texture].alphaTest);
        mesh.wallmarkClamp = gameaux12::UsesWallmarkClamp(source.shader);
        mesh.transparency = gameaux12::ClassifyTransparentMaterial(source.shader);
        mesh.indices = static_cast<uint32_t>(source.indices.size());
        mesh.requiredBones = candidate.requiredBones;
        mesh.skinned = source.skinned;
        mesh.allocation = static_cast<uint32_t>(m_auxGeometry.size());
        mesh.vbOffset = static_cast<uint32_t>(cursor);
        mesh.vb = {address + cursor, vbBytes, sizeof(gameaux12::Vertex)};
        std::memcpy(static_cast<uint8_t*>(mapped) + cursor, source.vertices.data(), vbBytes);
        cursor += vbBytes;
        mesh.ibOffset = static_cast<uint32_t>(cursor);
        mesh.ib = {address + cursor, ibBytes, DXGI_FORMAT_R16_UINT};
        std::memcpy(static_cast<uint8_t*>(mapped) + cursor, source.indices.data(), ibBytes);
        cursor += (static_cast<uint64_t>(ibBytes) + 3u) & ~uint64_t{3};
        for (const auto& vertex : source.vertices)
            mesh.radius = (std::max)(mesh.radius, std::hypot(
                vertex.position[0], vertex.position[1], vertex.position[2]));
        handles[i] = static_cast<uint32_t>(m_auxMeshes.size());
        m_auxMeshes.push_back(mesh);
        ++m_auxResources.meshesUploaded;
    }
    geometry->Unmap(0, nullptr);
    if (cursor != 0)
    {
        m_auxResources.geometryBytes += bytes;
        m_auxGeometry.push_back(std::move(geometry));
    }
    return true;
}

uint64_t GameDx12StaticScene12::TextureFootprintBytes(std::span<const uint32_t> meshes) const
{
    std::unordered_set<uint32_t> textures;
    uint64_t bytes = 0;
    for (const uint32_t mesh : meshes)
    {
        if (mesh >= m_auxMeshes.size())
            continue;
        const uint32_t texture = m_auxMeshes[mesh].descriptor - 1;
        if (textures.insert(texture).second)
            bytes += m_textures[texture].footprintBytes;
    }
    return bytes;
}

void GameDx12StaticScene12::FramePreparation::Clear()
{
    statics.clear();
    objects.clear();
    for (auto& shadow : shadows)
    {
        for (auto& layout : shadow.statics)
            layout.clear();
        shadow.objects.clear();
    }
}

bool GameDx12StaticScene12::RecordNativeFrame(
    GameDx12NativeFrame12::Recording recording,
    const DirectX::XMFLOAT4X4& logicalViewProjection,
    const float cameraPosition[3], const gameaux12::Frame& extras,
    const gamelight12::FrameSnapshot& lighting,
    const gamewater12::Frame& water,
    FrameStats12& stats, AuxStats& auxStats, std::string& reason)
{
    auto* list = recording.list;
    const uint32_t frameSlot = recording.slot;
    stats = FrameStats12{};
    auxStats = AuxStats{};
    if (list == nullptr)
        return Fail(reason, "static scene null command list");
    if (!m_loaded || !m_uploadConfirmed || m_gpuUnknown)
        return Fail(reason, "static scene is not available");
    if (frameSlot >= kFrameConstantsSlots)
        return Fail(reason, "static scene frame slot out of range");
    if (!FiniteMatrix(logicalViewProjection) || cameraPosition == nullptr ||
        !std::isfinite(cameraPosition[0]) || !std::isfinite(cameraPosition[1]) ||
        !std::isfinite(cameraPosition[2]))
        return Fail(reason, "static scene camera is not finite");

    const auto cullStart = Clock::now();
    SceneFrameConstants constants{};
    game_shadow12::SunPlan sunPlan;
    std::array<std::array<FrustumPlane, 6>, game_shadow12::MaxSunCascades> sunPlanes{};
    LocalShadowViews localShadowViews;
    const uint32_t viewMode = SceneViewMode();
    static const bool legacyWaterSsr = gamewater12::Enabled("OGSR_DX12_GAME_LEGACY_WATER_SSR");
    constants.legacyWaterSsr = legacyWaterSsr ? 1u : 0u;
    const uint32_t sunFilter = SceneSunFilter();
    const auto& sunSettings = SceneSunSettings();
    constants.sunFilter = sunFilter != 0 ? sunFilter : !SceneReceiverPlaneSunRequested() ? 3u : m_sunShadowSlices > 1 ? 2u : 7u;
    constants.sunReceiverBiasMask = sunSettings.biasMask & 3u;
    constants.sunShadowResolution = m_sunShadowResolution;
    constants.localShadowConfig[0] = m_localShadow ? m_localShadowSlices : 0;
    constants.diagnosticLight[1] = lighting.tracedLightId != 0 ? 1u : 0u;
    constants.diagnosticLight[2] = frameSlot;
    constants.diagnosticLight[3] = static_cast<uint32_t>(lighting.frame);
    constants.diagnosticOrigin = {lighting.diagnosticOrigin.x, lighting.diagnosticOrigin.y,
        lighting.diagnosticOrigin.z, 0.0f};
    constants.viewProjection = Transpose(logicalViewProjection);
    constants.cameraPosition = {cameraPosition[0], cameraPosition[1], cameraPosition[2], 1.0f};
    if (lighting.enabled && lighting.levelSession == LevelSession() && lighting.levelEpoch == LevelEpoch())
    {
        constants.lightingFlags[0] = 1u;
        constants.lightingFlags[1] = lighting.sunEnabled ? 1u : 0u;
        constants.ambient = {lighting.ambient.x, lighting.ambient.y, lighting.ambient.z, 0.0f};
        if (lighting.hemiEnabled)
            constants.hemiColor = {lighting.hemi.x, lighting.hemi.y, lighting.hemi.z, 1.0f};
        constants.sunDirection = {lighting.sunDirection.x, lighting.sunDirection.y, lighting.sunDirection.z, 0.0f};
        constants.sunColor = {lighting.sunColor.x, lighting.sunColor.y, lighting.sunColor.z,
            SceneReceiverPlaneSunRequested() ? 1.0f : 0.0f};
        if (m_sunShadow != nullptr && (viewMode == 0 || viewMode >= 4) &&
            lighting.sunEnabled && lighting.shadowsEnabled)
        {
            const auto start = Clock::now();
            const bool fitRequested = sunSettings.distance != 0 && m_sunShadowSlices == sunSettings.count;
            bool fitted = fitRequested && game_shadow12::MakeSunPlan(logicalViewProjection, cameraPosition,
                {lighting.sunDirection.x, lighting.sunDirection.y, lighting.sunDirection.z},
                m_sunShadowSlices, m_sunShadowResolution, static_cast<float>(sunSettings.distance), sunPlan);
            for (uint32_t slice = 0; slice < sunPlan.count; ++slice)
                fitted &= BuildFrustum(sunPlan.cascades[slice].viewProjection, sunPlanes[slice].data());
            if (!fitted)
            {
                sunPlan = {};
                auto& fallback = sunPlan.cascades[0];
                fallback.viewProjection = SunShadowViewProjection(cameraPosition, logicalViewProjection,
                    lighting.sunDirection, m_sunShadowResolution);
                fallback.depthSpan = 192.0f - 0.1f;
                fallback.radius = kSunShadowRadius;
                fallback.texelWorld = 2.0f * kSunShadowRadius / m_sunShadowResolution;
                if (FiniteMatrix(fallback.viewProjection) && BuildFrustum(fallback.viewProjection, sunPlanes[0].data()))
                    sunPlan.count = 1;
                if (fitRequested && lighting.frame % 120 == 0)
                    Msg("ph_dx12_csm: fallback=invalid_cascade_plan active=%u", sunPlan.count);
            }
            constants.lightingFlags[3] = sunPlan.count;
            constants.sunCameraForward = {sunPlan.cameraForward.x, sunPlan.cameraForward.y,
                sunPlan.cameraForward.z, fitted ? 1.0f : 0.0f};
            for (uint32_t slice = 0; slice < sunPlan.count; ++slice)
            {
                const auto& cascade = sunPlan.cascades[slice];
                constants.sunShadowViewProjection[slice] = Transpose(cascade.viewProjection);
                constants.sunCascadeData[slice] = {cascade.splitNear, cascade.splitFar, cascade.blendStart, cascade.texelWorld};
                // Сохраняем смещение в метрах при изменении глубины ортообъёма.
                constants.sunDepthBias[slice] = (sunSettings.biasMask & 2u) != 0
                    ? 0.000015f * (192.0f - 0.1f) / cascade.depthSpan : 0.0f;
            }
            stats.shadowCpuMs += Ms(start);
        }
        if (lighting.localEnabled)
        {
            const uint32_t lightCount = (std::min)(lighting.localCount, static_cast<uint32_t>(lighting.localLights.size()));
            constants.lightingFlags[2] = lightCount;
            for (uint32_t index = 0; index < lightCount; ++index)
            {
                const auto& light = lighting.localLights[index];
                const float range = light.range;
                auto& gpu = constants.localLights[index];
                gpu.positionInverseRangeSquared = {light.position.x, light.position.y, light.position.z,
                    1.0f / (std::max)(range * range, 0.0001f)};
                gpu.directionOuterCos = {light.direction.x, light.direction.y, light.direction.z, light.outerCos};
                gpu.colorInnerCos = {light.color.x, light.color.y, light.color.z, light.innerCos};
                gpu.type[0] = static_cast<uint32_t>(light.type);
                if (light.id == lighting.tracedLightId)
                    constants.diagnosticLight[0] = index + 1;
            }
            if (m_localShadow != nullptr && (viewMode == 0 || viewMode >= 4))
            {
                const auto selectionStart = Clock::now();
                SelectLocalShadowViews(lighting, m_localShadowSlices / game_shadow12::LayersPerBank,
                    m_localShadowAllocation, constants, localShadowViews);
                for (uint32_t slot = 0; slot < lightCount; ++slot)
                {
                    const auto layerPlusOne = constants.localLights[slot].type[1];
                    if (layerPlusOne != 0)
                    {
                        const uint32_t index = stats.shadowedLights++;
                        stats.shadowLightIds[index] = lighting.localLights[slot].id;
                        stats.shadowLightFirstSlices[index] = layerPlusOne - 1;
                    }
                }
                stats.shadowCpuMs += Ms(selectionStart);
            }
        }
    }
    if (lighting.diagnosticBurst || lighting.localTraceCount != 0)
    {
        gamelight12::Vec3 direction;
        gamelight12::NormalizeDirection(logicalViewProjection._14, logicalViewProjection._24,
            logicalViewProjection._34, direction);
        Msg("ph_dx12_f43fr2: frame=%llu slot=%u cb_offset=%llu view=%u probe_mode=%u "
            "camera=(%.4f,%.4f,%.4f) forward=(%.6f,%.6f,%.6f) light_count=%u "
            "candidate_count=%u sun=%u sun_map=%u sun_dir=(%.5f,%.5f,%.5f) sun_rgb=(%.5f,%.5f,%.5f) "
            "ambient=(%.5f,%.5f,%.5f) hemi=(%.5f,%.5f,%.5f)",
            static_cast<unsigned long long>(lighting.frame), frameSlot,
            static_cast<unsigned long long>(frameSlot * kFrameConstantsSize), viewMode,
            static_cast<unsigned>(lighting.probeMode), cameraPosition[0], cameraPosition[1], cameraPosition[2],
            direction.x, direction.y, direction.z, constants.lightingFlags[2], lighting.localCandidateCount,
            constants.lightingFlags[1], constants.lightingFlags[3],
            constants.sunDirection.x, constants.sunDirection.y, constants.sunDirection.z,
            constants.sunColor.x, constants.sunColor.y, constants.sunColor.z,
            constants.ambient.x, constants.ambient.y, constants.ambient.z,
            constants.hemiColor.x, constants.hemiColor.y, constants.hemiColor.z);
    }
    for (uint32_t index = 0; index < lighting.localTraceCount; ++index)
    {
        const auto& trace = lighting.localTrace[index];
        const bool selected = trace.gpuSlot >= 0 &&
            static_cast<uint32_t>(trace.gpuSlot) < constants.lightingFlags[2];
        const auto layer = selected ? constants.localLights[trace.gpuSlot].type[1] : 0u;
        const bool observed = trace.inFrustumQuery || trace.inNearQuery;
        const char* shadowReason = "none";
        if (!selected)
            shadowReason = "not_selected_for_lighting";
        else if (!trace.shadowEligible)
            shadowReason = trace.sourceShadow ? "ineligible" : "source_unshadowed";
        else if (m_localShadow == nullptr)
            shadowReason = "map_unavailable_or_disabled";
        else if (viewMode > 0 && viewMode < 4)
            shadowReason = "view_disabled";
        else if (localShadowViews.invalidProjection[trace.gpuSlot])
            shadowReason = "invalid_projection";
        else if (layer == 0)
            shadowReason = localShadowViews.suppressed[trace.gpuSlot] ? "shadow_budget_suppressed" : "shadow_budget";
        const auto* gpu = selected ? &constants.localLights[trace.gpuSlot].positionInverseRangeSquared : nullptr;
        Msg("ph_dx12_f43fr1: light_id=%llu frame=%llu distance=%.2f radius=%.2f active=%d "
            "type=%u frustum_query=%d near_query=%d near_volume=%d camera_inside=%d candidate=%d "
            "selected_for_lighting=%d source_shadow=%d shadow_eligible=%d selected_for_shadows=%d "
            "gpu_slot=%d shadow_layer=%d position=(%.3f,%.3f,%.3f) gpu_position=(%.3f,%.3f,%.3f) "
            "rejection_reason=%s shadow_rejection=%s",
            static_cast<unsigned long long>(trace.id), static_cast<unsigned long long>(lighting.frame),
            trace.distance, trace.radius, observed ? (trace.active ? 1 : 0) : -1,
            static_cast<unsigned>(trace.type), trace.inFrustumQuery ? 1 : 0, trace.inNearQuery ? 1 : 0,
            trace.nearVolume ? 1 : 0, trace.cameraInside ? 1 : 0, trace.candidate ? 1 : 0,
            selected ? 1 : 0, trace.sourceShadow ? 1 : 0, trace.shadowEligible ? 1 : 0, layer != 0 ? 1 : 0,
            trace.gpuSlot, layer != 0 ? static_cast<int>(layer - 1) : -1,
            trace.position.x, trace.position.y, trace.position.z,
            gpu ? gpu->x : 0.0f, gpu ? gpu->y : 0.0f, gpu ? gpu->z : 0.0f,
            gamelight12::LocalTraceReasonName(trace.rejection), shadowReason);
        const auto record = selected ? constants.localLights[trace.gpuSlot] : LocalLightGpu{};
        const bool packedIdMatches = selected && lighting.localLights[trace.gpuSlot].id == trace.id;
        Msg("ph_dx12_f43fr2: light_record frame=%llu id=%llu production_slot=%d gpu_slot=%d packed_id_match=%d "
            "source_dir=(%.6g,%.6g,%.6g,%.6g) source_rgb=(%.6g,%.6g,%.6g,%.6g) "
            "gpu_pos_rsq=(%.6g,%.6g,%.6g,%.6g) gpu_dir=(%.6g,%.6g,%.6g,%.6g) "
            "gpu_rgb=(%.6g,%.6g,%.6g,%.6g) gpu_type=(%u,%u,%u,%u)",
            static_cast<unsigned long long>(lighting.frame), static_cast<unsigned long long>(trace.id),
            trace.productionSlot, trace.gpuSlot, packedIdMatches ? 1 : 0,
            trace.direction.x, trace.direction.y, trace.direction.z, trace.outerCos,
            trace.color.x, trace.color.y, trace.color.z, trace.innerCos,
            record.positionInverseRangeSquared.x, record.positionInverseRangeSquared.y,
            record.positionInverseRangeSquared.z, record.positionInverseRangeSquared.w,
            record.directionOuterCos.x, record.directionOuterCos.y, record.directionOuterCos.z, record.directionOuterCos.w,
            record.colorInnerCos.x, record.colorInnerCos.y, record.colorInnerCos.z, record.colorInnerCos.w,
            record.type[0], record.type[1], record.type[2], record.type[3]);
    }
    const auto normalTexture = m_textureKeys.find(gamewater12::WaterNormal);
    const bool waterView = viewMode == 11 || viewMode == 12;
    const bool waterReady = water.valid && (viewMode == 0 || waterView) && gameaux12::TransparencyEnabled() &&
        recording.opaqueColor != nullptr && normalTexture != m_textureKeys.end() &&
        m_textures[normalTexture->second].image != nullptr;
    if (waterReady)
    {
        using namespace DirectX;
        XMStoreFloat4x4(&constants.inverseViewProjection,
            XMMatrixTranspose(XMMatrixInverse(nullptr, XMLoadFloat4x4(&logicalViewProjection))));
        constants.waterWeather = water.weather;
        constants.waterSky = {water.sky.x, water.sky.y, water.sky.z, water.wetness};
    }
    const bool waterAvailable = waterReady && FiniteMatrix(constants.inverseViewProjection);
    // NativeFrame::Begin дождался fence перед повторным использованием слота.
    auto* frameDestination = static_cast<uint8_t*>(m_frameConstantsMapped) +
        static_cast<uint64_t>(frameSlot) * kFrameConstantsSize;
    std::memcpy(frameDestination, &constants, sizeof(constants));

    struct ShadowPass
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dsv;
        uint32_t size;
        game_shadow12::View view;
        const FrustumPlane* planes;
        bool dynamic;
    };
    static_assert(GameDx12NativeFrame12::kShadowListCount == game_shadow12::MaxViews);
    std::array<ShadowPass, GameDx12NativeFrame12::kShadowListCount> passes{};
    uint32_t passCount = 0;
    const bool dynamicSun = SceneDynamicSunShadowsRequested();
    if (sunPlan.count != 0)
    {
        auto dsv = m_sunShadowDsv->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t slice = 0; slice < sunPlan.count; ++slice, dsv.ptr += m_sunShadowDsvStride)
            passes[passCount++] = {dsv, m_sunShadowResolution, {slice}, sunPlanes[slice].data(), dynamicSun};
    }
    if (localShadowViews.count != 0)
    {
        auto dsv = m_localShadowDsv->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t layer = 0; layer < m_localShadowSlices; ++layer, dsv.ptr += m_localShadowDsvStride)
            if (localShadowViews.active[layer])
                passes[passCount++] = {dsv, kLocalShadowSize, {layer + game_shadow12::MaxSunCascades},
                    localShadowViews.frusta[layer].data(), true};
    }

    FrustumPlane planes[6] = {};
    const bool cull = !SceneCullDisabled() && BuildFrustum(logicalViewProjection, planes);
    stats.total = static_cast<uint32_t>(m_draws.size());
    const bool validExtras = extras.session == LevelSession() && extras.epoch == LevelEpoch() &&
        extras.registryGeneration == Generation();
    if (validExtras && extras.poses.size() > gameaux12::kMaxFrameTransforms)
        return Fail(reason, "skeletal poses exceed frame transform capacity");

    // Общий снимок держит только неизменяемые представления опубликованных
    // данных. Регистрация ресурсов и захват объектов закончены до его создания.
    // Владелец не меняет регистр и снимок до выхода из всех GR-задач.
    const struct PreparationSnapshot
    {
        uint64_t session, epoch, generation;
        std::span<const DrawSource12> draws;
        std::span<const ShadowCasterTree, 2> casters;
        std::span<const gameaux12::ObjectDraw> objects;
        std::span<const uint32_t> parts;
        std::span<const AuxMesh> meshes;
        uint32_t poseCount;
    } snapshot{LevelSession(), LevelEpoch(), Generation(), m_draws, m_shadowCasters,
        validExtras ? std::span(extras.objects) : std::span<const gameaux12::ObjectDraw>{},
        validExtras ? std::span(extras.objectMeshes) : std::span<const uint32_t>{},
        m_auxMeshes, validExtras ? static_cast<uint32_t>(extras.poses.size()) : 0};
    const auto poseCount = snapshot.poseCount;
    const auto objectParts = [&snapshot](const gameaux12::ObjectDraw& object) -> std::span<const uint32_t>
    {
        if (object.generation == 0 || object.boneCount == 0 || object.boneCount > gameaux12::kMaxBones ||
            object.poseOffset > snapshot.poseCount || object.boneCount > snapshot.poseCount - object.poseOffset ||
            object.firstMesh > snapshot.parts.size() || object.meshCount > snapshot.parts.size() - object.firstMesh)
            return {};
        return snapshot.parts.subspan(object.firstMesh, object.meshCount);
    };
    const auto objectMesh = [&snapshot](uint32_t handle, const gameaux12::ObjectDraw& object) -> const AuxMesh*
    {
        if (handle >= snapshot.meshes.size())
            return nullptr;
        const auto& mesh = snapshot.meshes[handle];
        return mesh.skinned == object.skinned && mesh.requiredBones <= object.boneCount ? &mesh : nullptr;
    };
    static_assert(std::tuple_size_v<decltype(FramePreparation::shadows)> == game_shadow12::MaxViews);
    static_assert(std::tuple_size_v<decltype(ShadowPreparation::statics)> == std::tuple_size_v<decltype(m_sunShadowPipelines)>);
    // Эталон Verify использует тот же обход, что и исходная serial запись.
    const auto visitLegacyCasters = [&](const ShadowCasterTree& tree, const FrustumPlane* frustum, const auto& emit)
    {
        const auto visit = [&](const auto& self, uint32_t index) -> void
        {
            const auto& node = tree.nodes[index];
            if (!BoxInside(frustum, node.minimum, node.maximum))
                return;
            if (node.count == 0)
            {
                self(self, node.left);
                self(self, node.right);
                return;
            }
            for (uint32_t offset = 0; offset < node.count; ++offset)
            {
                const uint32_t drawIndex = tree.draws[node.first + offset];
                const auto& draw = m_draws[drawIndex];
                if (SphereInside(frustum, draw.center, draw.radius))
                    emit(drawIndex);
            }
        };
        visit(visit, 0);
    };
    auto& prepared = m_preparedFrame;
    bool usePrepared = false;
    static const renderprep12::Mode grMode = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_GR");
        if (value && std::strcmp(value, "parallel") == 0)
            return renderprep12::Mode::Parallel;
        if (value && std::strcmp(value, "verify") == 0)
            return renderprep12::Mode::Verify;
        return renderprep12::Mode::Serial;
    }();
    const bool packForRecording = recording.UsesParallelShadowLists() && passCount > 1;
    if (grMode != renderprep12::Mode::Serial || packForRecording)
    {
        static const uint32_t grain = []
        {
            const char* value = std::getenv("OGSR_DX12_GAME_GR_GRAIN");
            if (!value || !*value)
                return 256u;
            char* end = nullptr;
            const auto parsed = std::strtoul(value, &end, 10);
            return *end == '\0' && parsed >= 32 && parsed <= 8192 ? static_cast<uint32_t>(parsed) : 256u;
        }();
        // Explicit cap on temporary masks and ordered caster indices.
        constexpr size_t maxSources = 1u << 20;
        const size_t objectCount = snapshot.objects.size();
        std::string prepReason;
        GameDx12GRRangeStats12 grStats;
        uint32_t workCount = 0;
        uint32_t participantsUsed = 0;
        try
        {
            if (snapshot.draws.size() > maxSources || objectCount > maxSources || snapshot.parts.size() > maxSources)
                throw std::runtime_error("GR native preparation exceeds source cap");
            const uint32_t staticBlocks = (static_cast<uint32_t>(snapshot.draws.size()) + grain - 1) / grain;
            const uint32_t objectBlocks = (static_cast<uint32_t>(objectCount) + grain - 1) / grain;
            workCount = staticBlocks + objectBlocks + passCount;

            const auto prepStart = Clock::now();
            prepared.Clear();
            prepared.statics.resize(snapshot.draws.size());
            prepared.objects.resize(objectCount);
            auto& workers = m_preparationParticipants;
            workers.assign(workCount, UINT32_MAX);
            const auto process = [&](uint32_t begin, uint32_t end, uint32_t threadIndex)
            {
                for (uint32_t task = begin; task < end; ++task)
                {
                    if (task < staticBlocks)
                    {
                        const uint32_t first = task * grain;
                        const uint32_t last = (std::min)(first + grain, static_cast<uint32_t>(snapshot.draws.size()));
                        for (uint32_t i = first; i < last; ++i)
                        {
                            const auto& draw = snapshot.draws[i];
                            prepared.statics[i] = !cull || SphereInside(planes, draw.center, draw.radius);
                        }
                    }
                    else if (task < staticBlocks + objectBlocks)
                    {
                        const uint32_t first = (task - staticBlocks) * grain;
                        const uint32_t last = (std::min)(first + grain, static_cast<uint32_t>(objectCount));
                        for (uint32_t i = first; i < last; ++i)
                        {
                            const auto& object = snapshot.objects[i];
                            prepared.objects[i] = !cull || SphereInside(planes, object.center, object.radius);
                        }
                    }
                    else
                    {
                        const auto& pass = passes[task - staticBlocks - objectBlocks];
                        const uint32_t projection = pass.view.projection;
                        const auto* frustum = pass.planes;
                        auto& view = prepared.shadows[projection];
                        for (uint32_t layout = 0; layout < snapshot.casters.size(); ++layout)
                        {
                            const auto& tree = snapshot.casters[layout];
                            if (tree.nodes.empty())
                                continue;
                            auto& indices = view.statics[layout];
                            const auto visit = [&](const auto& self, uint32_t index, uint32_t depth) -> void
                            {
                                if (index >= tree.nodes.size() || depth > 32)
                                    throw std::runtime_error("invalid shadow BVH child");
                                const auto& node = tree.nodes[index];
                                if (!BoxInside(frustum, node.minimum, node.maximum))
                                    return;
                                if (node.count == 0)
                                {
                                    self(self, node.left, depth + 1);
                                    self(self, node.right, depth + 1);
                                    return;
                                }
                                if (node.first > tree.draws.size() || node.count > tree.draws.size() - node.first)
                                    throw std::runtime_error("invalid shadow BVH leaf");
                                for (uint32_t offset = 0; offset < node.count; ++offset)
                                {
                                    const uint32_t drawIndex = tree.draws[node.first + offset];
                                    if (drawIndex >= snapshot.draws.size())
                                        throw std::runtime_error("invalid static shadow handle");
                                    const auto& draw = snapshot.draws[drawIndex];
                                    if (SphereInside(frustum, draw.center, draw.radius))
                                        indices.push_back(drawIndex);
                                }
                            };
                            visit(visit, 0, 0);
                        }
                        if (pass.dynamic)
                        {
                            for (uint32_t i = 0; i < objectCount; ++i)
                            {
                                const auto& object = snapshot.objects[i];
                                if (!SphereInside(frustum, object.center, object.radius))
                                    continue;
                                for (const uint32_t handle : objectParts(object))
                                {
                                    const auto* mesh = objectMesh(handle, object);
                                    if (mesh && CastsSunShadow(mesh->surface))
                                    {
                                        if (view.objects.size() == maxSources)
                                            throw std::runtime_error("GR native shadow packet cap exceeded");
                                        view.objects.push_back({i, handle});
                                    }
                                }
                            }
                        }
                    }
                    workers[task] = threadIndex;
                }
            };
            bool complete = true;
            if (grMode == renderprep12::Mode::Serial)
            {
                process(0, workCount, 0);
                stats.grSerialMs = Ms(prepStart);
            }
            else
            {
                complete = GameDx12GR_RunRanges(workCount, 1, process, grStats, prepReason);
                stats.grParallelMs = Ms(prepStart);
            }
            usePrepared = complete && std::find(workers.begin(), workers.end(), UINT32_MAX) == workers.end() &&
                snapshot.session == LevelSession() && snapshot.epoch == LevelEpoch() && snapshot.generation == Generation();
            if (!usePrepared)
                throw std::runtime_error(prepReason.empty() ? "incomplete GR frame preparation" : prepReason);
            std::sort(workers.begin(), workers.end());
            participantsUsed = static_cast<uint32_t>(std::unique(workers.begin(), workers.end()) - workers.begin());
            if (grMode == renderprep12::Mode::Verify)
            {
                const auto serialStart = Clock::now();
                auto& reference = m_referenceFrame;
                reference.Clear();
                for (const auto& draw : m_draws)
                    reference.statics.push_back(!cull || SphereInside(planes, draw.center, draw.radius));
                if (validExtras)
                    for (const auto& object : extras.objects)
                        reference.objects.push_back(!cull || SphereInside(planes, object.center, object.radius));
                for (uint32_t i = 0; i < passCount; ++i)
                {
                    const auto& pass = passes[i];
                    const uint32_t projection = pass.view.projection;
                    const auto* frustum = pass.planes;
                    auto& view = reference.shadows[projection];
                    for (uint32_t layout = 0; layout < m_shadowCasters.size(); ++layout)
                    {
                        const auto& tree = m_shadowCasters[layout];
                        if (!tree.nodes.empty())
                            visitLegacyCasters(tree, frustum, [&](uint32_t index) { view.statics[layout].push_back(index); });
                    }
                    if (pass.dynamic && validExtras)
                    {
                        for (const auto& object : extras.objects)
                        {
                            if (!SphereInside(frustum, object.center, object.radius))
                                continue;
                            for (const uint32_t handle : objectParts(object))
                            {
                                const auto* mesh = objectMesh(handle, object);
                                if (mesh && CastsSunShadow(mesh->surface))
                                    view.objects.push_back({static_cast<uint32_t>(&object - extras.objects.data()), handle});
                            }
                        }
                    }
                }
                if (!(reference == prepared))
                    return Fail(reason, "GR native preparation differs from legacy traversal");
                stats.grSerialMs = Ms(serialStart);
            }
            if (grMode == renderprep12::Mode::Verify)
                stats.grMode = "static-verify";
            else if (grMode == renderprep12::Mode::Parallel)
                stats.grMode = "static-parallel";
            else
                stats.grMode = "static-serial-packets";
        }
        catch (...)
        {
            if (grMode == renderprep12::Mode::Verify)
                return Fail(reason, "GR native verify failed; no partial frame submitted");
            // RunRanges drains before return; discard all temporary outputs.
            usePrepared = false;
            stats.grMode = "static-serial-fallback";
            static bool reported = false;
            if (!reported)
            {
                Msg("ph_dx12_mt: GR preparation failed; using legacy serial traversal reason=%s",
                    prepReason.empty() ? "exception_or_incomplete_plan" : prepReason.c_str());
                reported = true;
            }
        }
        if (lighting.frame % 120 == 0)
            Msg("ph_dx12_mt: frame=%llu mode=%s work_items=%u workers=%u participants_used=%u submitted=%llu completed=%llu "
                "failures=%llu unobserved=%llu grain=%u submit_ms=%.6f wait_ms=%.6f parallel_ms=%.6f serial_ms=%.6f",
                static_cast<unsigned long long>(lighting.frame), stats.grMode, workCount, grStats.backgroundWorkers, participantsUsed,
                static_cast<unsigned long long>(grStats.submitted), static_cast<unsigned long long>(grStats.completed),
                static_cast<unsigned long long>(grStats.failures), static_cast<unsigned long long>(grStats.unobservedFailures),
                grain, grStats.submitMs, grStats.waitMs, stats.grParallelMs, stats.grSerialMs);
    }
    stats.cull_ms = Ms(cullStart);

    const auto textureHeap = m_textureHeap.Get();
    const auto heapGpu = textureHeap->GetGPUDescriptorHandleForHeapStart();
    const auto frameCb = m_frameConstants->GetGPUVirtualAddress() +
        static_cast<uint64_t>(frameSlot) * kFrameConstantsSize;
    const auto transformOffset = frameSlot * kTransformSlotBytes;
    const auto transformSrv = validExtras ? m_frameTransforms->GetGPUVirtualAddress() + transformOffset : 0;
    const auto bindFrame = [&](ID3D12GraphicsCommandList* target)
    {
        target->SetGraphicsRootSignature(m_rootSignature.Get());
        target->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12DescriptorHeap* heaps[] = {textureHeap};
        target->SetDescriptorHeaps(1, heaps);
        target->SetGraphicsRootConstantBufferView(0, frameCb);
        if (validExtras)
            target->SetGraphicsRootShaderResourceView(4, transformSrv);
    };
    uint8_t* transformDestination = nullptr;
    if (validExtras)
    {
        transformDestination = static_cast<uint8_t*>(m_frameTransformsMapped) + transformOffset;
        if (poseCount != 0)
            std::memcpy(transformDestination, extras.poses.data(), poseCount * sizeof(DirectX::XMFLOAT4X4));
    }

    const float threshold = ps_r_alphatest_threshold;
    const bool alphaTestDisabled = SceneAlphaTestDisabled();
    // Дамп ограничен тремя кадрами, как и на стороне R4.
    static uint32_t compareDumpFrames = 0;
    const bool dumpDraws = SceneCompareEnabled() && compareDumpFrames < 3;
    const auto recordStart = Clock::now();
    static const bool traceAuxEnabled = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_R1_OBJECT_ID");
        return value != nullptr && *value != '\0';
    }();
    const bool traceAux = traceAuxEnabled && extras.traceId != UINT32_MAX;
    if (R1VisualSelection()[0] != UINT32_MAX || traceAux)
        ++m_visualTraceFrame;
    const auto traceStaticDraw = [&](const DrawSource12& draw, VisualTraceState state)
    {
        if (draw.traceSlot == UINT8_MAX)
            return;
        auto& trace = m_visualTrace[draw.traceSlot];
        if (trace.state == state || trace.messages >= kMaxVisualTraceMessages)
            return;
        trace.state = state;
        const bool submitted = state == VisualTraceState::Submitted;
        const bool overlay = IsOverlay(draw.surface);
        const bool deferredDepth = overlay || draw.surface == SurfaceMode::Translucent;
        Msg("ph_dx12_f43_r1: static visual_id=%u frame=%llu state=%s reason='%s' "
            "pso='%s' srv=%u alpha_clip=%d alpha_threshold=%.4f "
            "raster_cull=%s frustum=%d depth_func=%s depth_write=%d depth_bias=%d capped=%d",
            draw.visualId, static_cast<unsigned long long>(m_visualTraceFrame),
            submitted ? "submitted" : "culled", submitted ? "none" : "frustum",
            gameaux12::SurfaceModeName(draw.surface), draw.descriptor,
            IsCutout(draw.surface) && !alphaTestDisabled ? 1 : 0, threshold,
            m_cullEnabled ? "back" : "none", cull ? 1 : 0,
            deferredDepth ? "less_equal" : "less", deferredDepth ? 0 : 1, overlay ? 1 : 0,
            ++trace.messages == kMaxVisualTraceMessages ? 1 : 0);
    };

    struct DeferredDraw
    {
        const DrawSource12* draw;
        const AuxMesh* mesh;
        const gameaux12::ObjectDraw* object;
        SurfaceMode surface;
        gameaux12::TransparentMaterial transparency;
        float depth;
    };
    std::vector<DeferredDraw> deferred;
    const auto viewDepth = [&](const float center[3])
    {
        // При перспективной проекции clip.w пропорционален глубине камеры.
        const float depth = center[0] * logicalViewProjection._14 +
            center[1] * logicalViewProjection._24 + center[2] * logicalViewProjection._34 +
            logicalViewProjection._44;
        return std::isfinite(depth) ? depth : 0.0f;
    };

    ID3D12PipelineState* boundPipeline = nullptr;
    const auto bindPipeline = [&](SurfaceMode surface, uint32_t layout, ID3D12PipelineState* regular)
    {
        ID3D12PipelineState* pipeline = regular;
        if (IsOverlay(surface) || surface == SurfaceMode::Translucent)
        {
            auto slot = DeferredPipelines::Solid;
            if (surface == SurfaceMode::OverlayBlend)
                slot = DeferredPipelines::Blend;
            else if (surface == SurfaceMode::OverlayMultiply)
                slot = DeferredPipelines::Multiply;
            else if (surface == SurfaceMode::Translucent)
                slot = DeferredPipelines::Translucent;
            pipeline = m_deferredPipelines[layout].states[slot].Get();
        }
        if (boundPipeline != pipeline)
        {
            list->SetPipelineState(pipeline);
            boundPipeline = pipeline;
        }
    };
    auto nullTerrainSrv = heapGpu;
    nullTerrainSrv.ptr += static_cast<UINT64>(kTerrainDescriptor) * m_textureDescriptorSize;
    auto nullWaterSrv = heapGpu;
    nullWaterSrv.ptr += static_cast<UINT64>(kNullWaterDescriptor) * m_textureDescriptorSize;
    const auto bindColorFrame = [&](ID3D12GraphicsCommandList* target)
    {
        bindFrame(target);
        target->SetGraphicsRootDescriptorTable(7, nullWaterSrv);
        target->SetGraphicsRootDescriptorTable(kHemiSrvRoot, heapGpu);
        target->SetGraphicsRootDescriptorTable(kTerrainSrvRoot, nullTerrainSrv);
        target->SetGraphicsRoot32BitConstant(kTerrainScaleRoot, 0u, 0);
    };
    uint32_t boundHemi = 0;
    uint32_t boundTerrainSet = 0;
    float boundTerrainScale = 0.0f;
    bool waterPass = false;
    const bool limitMaterialView = (viewMode == 1 || (viewMode >= 7 && viewMode <= 10) || viewMode >= 13) &&
        R1VisualSelection()[0] != UINT32_MAX;
    const auto bindMaterial = [&](uint32_t descriptor, uint32_t id, SurfaceMode surface,
                                  bool wallmarkClamp, gameaux12::TransparentMaterial transparency,
                                  bool twoSided = false, bool vertexHemi = false,
                                  uint32_t materialFlags = 0, bool selectedVisual = false)
    {
        auto gpu = m_textureHeap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(descriptor) * m_textureDescriptorSize;
        list->SetGraphicsRootDescriptorTable(1, gpu);
        uint32_t mode = materialFlags | (descriptor == kNullDescriptor ? 0u : kSampleBase);
        if (descriptor != kNullDescriptor && IsCutout(surface) && !alphaTestDisabled)
            mode |= kClipAlpha;
        // DEST_COLOR/SRC_COLOR модифицирует уже освещённый цвет стены.
        if (surface == SurfaceMode::OverlayMultiply || (IsOverlay(surface) && !SceneLitDecalsEnabled()))
            mode |= kUnlit;
        if (IsBlended(surface))
            mode |= kOutputAlpha;
        if (wallmarkClamp)
            mode |= kWallmarkClamp;
        if (twoSided)
            mode |= kTwoSidedNormal;
        if (transparency == gameaux12::TransparentMaterial::Water)
            mode |= waterPass && gamewater12::WaterEnabled() ? kAdvancedWater : kWaterAlpha;
        if (vertexHemi)
            mode |= kVertexHemi;
        const uint32_t materialView = limitMaterialView && !selectedVisual ? 0u : viewMode;
        const uint32_t material[] = {mode, id, materialView, std::bit_cast<uint32_t>(threshold)};
        list->SetGraphicsRoot32BitConstants(2, 4, material, 0);
    };

    const float identity[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, 1.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 1.0f, 0.0f,
                                0.0f, 0.0f, 0.0f, 1.0f};
    bool worldIdentity = false;
    const auto submitStaticGeometry = [&](const DrawSource12& draw, float clipShift)
    {
        if (draw.layout == 3 || !worldIdentity)
        {
            worldIdentity = draw.layout != 3;
            list->SetGraphicsRoot32BitConstants(3, 16, worldIdentity ? identity : draw.world, 0);
        }
        const bool waves = waterPass && gamewater12::WaterEnabled() &&
            draw.surface == SurfaceMode::Translucent && draw.transparency == gameaux12::TransparentMaterial::Water;
        const uint32_t geometry[] = {draw.layout == 3 ? 1u : 0u, std::bit_cast<uint32_t>(clipShift),
                                     waves ? 1u : 0u};
        list->SetGraphicsRoot32BitConstants(5, 3, geometry, 0);
        if (draw.hemiDescriptor != kNullDescriptor && boundHemi != draw.hemiDescriptor)
        {
            auto hemiSrv = heapGpu;
            hemiSrv.ptr += static_cast<UINT64>(draw.hemiDescriptor) * m_textureDescriptorSize;
            list->SetGraphicsRootDescriptorTable(kHemiSrvRoot, hemiSrv);
            boundHemi = draw.hemiDescriptor;
        }
        if (draw.terrainSet != 0 && boundTerrainSet != draw.terrainSet)
        {
            auto terrainSrv = nullTerrainSrv;
            terrainSrv.ptr += static_cast<UINT64>(draw.terrainSet) * kTerrainTextureCount * m_textureDescriptorSize;
            list->SetGraphicsRootDescriptorTable(kTerrainSrvRoot, terrainSrv);
            boundTerrainSet = draw.terrainSet;
        }
        if (draw.terrainSet != 0 && boundTerrainScale != draw.terrainScale)
        {
            list->SetGraphicsRoot32BitConstant(kTerrainScaleRoot, std::bit_cast<uint32_t>(draw.terrainScale), 0);
            boundTerrainScale = draw.terrainScale;
        }
        const uint32_t materialFlags = (draw.hemiDescriptor == kNullDescriptor ? 0u : kLightmapHemi) |
            (draw.terrainSet == 0 ? 0u : kTerrainDetail) |
            (draw.terrainBaseHemi ? kTerrainBaseHemi : 0u);
        bindMaterial(draw.descriptor, draw.visualId, draw.surface, draw.wallmarkClamp, draw.transparency,
            false, draw.vertexHemi, materialFlags, draw.traceSlot != UINT8_MAX);
        list->IASetVertexBuffers(0, 1, &draw.vertexView);
        list->IASetIndexBuffer(&draw.indexView);
        list->DrawIndexedInstanced(draw.icount, 1, 0, 0, 0);
    };

    const auto submitAuxGeometry = [&](const AuxMesh& mesh, uint32_t id, SurfaceMode surface,
                                       uint32_t offset, uint32_t count, bool twoSided,
                                       float skyHemi = 0.0f)
    {
        list->SetGraphicsRoot32BitConstant(3, offset, 0);
        bindMaterial(mesh.descriptor, id, surface, mesh.wallmarkClamp, mesh.transparency, twoSided,
            false, skyHemi > 0.0f ? kObjectSkyHemi : 0u);
        const bool waves = waterPass && gamewater12::WaterEnabled() &&
            surface == SurfaceMode::Translucent && mesh.transparency == gameaux12::TransparentMaterial::Water;
        const uint32_t geometry[] = {0u,
            std::bit_cast<uint32_t>(IsOverlay(surface) ? kWallmarkClipShift : 0.0f),
            waves ? 1u : 0u, std::bit_cast<uint32_t>(skyHemi)};
        list->SetGraphicsRoot32BitConstants(5, static_cast<UINT>(std::size(geometry)), geometry, 0);
        worldIdentity = false;
        list->IASetVertexBuffers(0, 1, &mesh.vb);
        list->IASetIndexBuffer(&mesh.ib);
        list->DrawIndexedInstanced(mesh.indices, count, 0, 0, 0);
    };

    // Копируем матрицы травы на потоке-владельце в отдельный диапазон
    // защищённого fence слота. Задача sun map читает готовые пакеты.
    struct DetailShadowPacket
    {
        uint32_t mesh, first, count;
    };
    std::array<DetailShadowPacket, kDetailShadowInstances> detailPackets;
    uint32_t detailPacketCount = 0;
    if (constants.lightingFlags[3] != 0 && validExtras && SceneDetailSunShadowsRequested() && m_sunShadowAuxPipeline)
    {
        uint32_t next = gameaux12::kMaxFrameTransforms;
        constexpr uint32_t end = gameaux12::kMaxFrameTransforms + kDetailShadowInstances;
        for (const auto& group : extras.details)
        {
            if (next == end)
                break;
            if (group.mesh >= snapshot.meshes.size())
                continue;
            const auto& mesh = snapshot.meshes[group.mesh];
            if (mesh.skinned || mesh.requiredBones != 1)
                continue;
            const uint32_t first = next;
            for (const auto& transform : group.transforms)
            {
                if (next == end)
                    break;
                if (!FiniteMatrix(transform))
                    continue;
                const float center[] = {transform._41, transform._42, transform._43};
                const float scale = std::hypot(transform._11, transform._12, transform._13);
                const float distance = std::hypot(center[0] - cameraPosition[0],
                    center[1] - cameraPosition[1], center[2] - cameraPosition[2]);
                if (distance > 12.0f || !std::isfinite(scale) || !SphereInside(sunPlanes[0].data(), center, mesh.radius * scale))
                    continue;
                std::memcpy(transformDestination + uint64_t{next} * sizeof(transform), &transform, sizeof(transform));
                ++next;
            }
            if (next != first)
                detailPackets[detailPacketCount++] = {group.mesh, first, next - first};
        }
        if (lighting.diagnosticBurst)
            Msg("ph_dx12_fr3: detail_sun_shadow instances=%u cap=%u",
                next - gameaux12::kMaxFrameTransforms, kDetailShadowInstances);
    }
    const std::span<const DetailShadowPacket> sunDetails(detailPackets.data(), detailPacketCount);

    struct ShadowDraws
    {
        uint32_t statics = 0;
        uint32_t rigid = 0;
        uint32_t skinned = 0;
        uint32_t detail = 0;
        uint32_t detailDraws = 0;
        uint64_t hash = 14695981039346656037ull;
    };
    const bool sampleCsm = lighting.frame % 120 == 0;
    uint32_t timingMask = 0;
    for (uint32_t index = 0; index < passCount; ++index)
        timingMask |= 1u << passes[index].view.projection;
    recording.SetShadowTimingMask(timingMask);
    const FramePreparation& preparedPackets = prepared;
    const auto recordShadowMap = [&](ID3D12GraphicsCommandList* shadowList,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv, uint32_t size, uint32_t projection,
        const FrustumPlane* shadowPlanes, bool dynamic)
    {
        recording.StampShadow(*shadowList, projection, false);
        ShadowDraws draws;
        const auto mix = [&](uint32_t value)
        {
            draws.hash = (draws.hash ^ value) * 1099511628211ull;
        };
        if (sampleCsm)
            mix(projection);
        bindFrame(shadowList);
        uint32_t boundDescriptor = UINT32_MAX;
        bool identityBound = false;
        const auto bindShadowMaterial = [&](uint32_t descriptor, SurfaceMode surface)
        {
            if (!IsCutout(surface) || alphaTestDisabled)
                descriptor = kNullDescriptor;
            if (boundDescriptor == descriptor)
                return;
            boundDescriptor = descriptor;
            auto gpu = heapGpu;
            gpu.ptr += static_cast<UINT64>(descriptor) * m_textureDescriptorSize;
            shadowList->SetGraphicsRootDescriptorTable(1, gpu);
            const uint32_t mode = descriptor == kNullDescriptor ? 0u : kSampleBase | kClipAlpha;
            const uint32_t material[] = {mode, 0u, 0u, std::bit_cast<uint32_t>(threshold)};
            shadowList->SetGraphicsRoot32BitConstants(2, 4, material, 0);
        };
        const auto submitStaticShadow = [&](const DrawSource12& draw)
        {
            if (sampleCsm)
            {
                mix(static_cast<uint32_t>(&draw - snapshot.draws.data()));
                mix(draw.visualId);
                mix(draw.descriptor);
                mix(draw.icount);
            }
            if (draw.layout == 3 || !identityBound)
            {
                identityBound = draw.layout != 3;
                shadowList->SetGraphicsRoot32BitConstants(3, 16, identityBound ? identity : draw.world, 0);
            }
            shadowList->SetGraphicsRoot32BitConstant(5, draw.layout == 3 ? 1u : 0u, 0);
            bindShadowMaterial(draw.descriptor, draw.surface);
            shadowList->IASetVertexBuffers(0, 1, &draw.vertexView);
            shadowList->IASetIndexBuffer(&draw.indexView);
            shadowList->DrawIndexedInstanced(draw.icount, 1, 0, 0, 0);
        };
        const auto submitAuxShadow = [&](const AuxMesh& mesh, SurfaceMode surface, uint32_t first, uint32_t count)
        {
            if (sampleCsm)
            {
                mix(static_cast<uint32_t>(&mesh - snapshot.meshes.data()));
                mix(mesh.descriptor);
                mix(first);
                mix(count);
            }
            shadowList->SetGraphicsRoot32BitConstant(3, first, 0);
            bindShadowMaterial(mesh.descriptor, surface);
            shadowList->IASetVertexBuffers(0, 1, &mesh.vb);
            shadowList->IASetIndexBuffer(&mesh.ib);
            shadowList->DrawIndexedInstanced(mesh.indices, count, 0, 0, 0);
        };
        const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f};
        const D3D12_RECT rect{0, 0, static_cast<LONG>(size), static_cast<LONG>(size)};
        shadowList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        shadowList->RSSetViewports(1, &viewport);
        shadowList->RSSetScissorRects(1, &rect);
        shadowList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        shadowList->SetGraphicsRoot32BitConstant(kShadowLayerRoot, projection, 0);
        for (uint32_t layout = 0; layout < static_cast<uint32_t>(m_sunShadowPipelines.size()); ++layout)
        {
            const auto& tree = m_shadowCasters[layout];
            if (tree.nodes.empty())
                continue;
            shadowList->SetPipelineState(m_sunShadowPipelines[layout].Get());
            if (usePrepared)
            {
                for (const uint32_t index : preparedPackets.shadows[projection].statics[layout])
                {
                    submitStaticShadow(m_draws[index]);
                    ++draws.statics;
                }
                continue;
            }
            visitLegacyCasters(tree, shadowPlanes, [&](uint32_t index)
            {
                submitStaticShadow(m_draws[index]);
                ++draws.statics;
            });
        }
        if (dynamic && validExtras && m_sunShadowAuxPipeline != nullptr)
        {
            shadowList->SetPipelineState(m_sunShadowAuxPipeline.Get());
            if (usePrepared)
            {
                for (const auto& packet : preparedPackets.shadows[projection].objects)
                {
                    const auto& object = snapshot.objects[packet.objectIndex];
                    const auto& mesh = snapshot.meshes[packet.meshIndex];
                    submitAuxShadow(mesh, mesh.surface, object.poseOffset, 1);
                    ++(object.skinned ? draws.skinned : draws.rigid);
                }
            }
            else
            {
                for (const auto& object : extras.objects)
                {
                    if (!SphereInside(shadowPlanes, object.center, object.radius))
                        continue;
                    for (const uint32_t handle : objectParts(object))
                    {
                        const auto* mesh = objectMesh(handle, object);
                        if (!mesh || !CastsSunShadow(mesh->surface))
                            continue;
                        submitAuxShadow(*mesh, mesh->surface, object.poseOffset, 1);
                        ++(object.skinned ? draws.skinned : draws.rigid);
                    }
                }
            }
        }
        if (projection == 0 && !sunDetails.empty())
        {
            shadowList->SetPipelineState(m_sunShadowAuxPipeline.Get());
            for (const auto& packet : sunDetails)
            {
                submitAuxShadow(snapshot.meshes[packet.mesh], SurfaceMode::Cutout, packet.first, packet.count);
                ++draws.rigid;
                draws.detail += packet.count;
                ++draws.detailDraws;
            }
        }
        recording.StampShadow(*shadowList, projection, true);
        return draws;
    };
    const auto shadowRecordStart = Clock::now();
    // Карты принадлежат уровню. Одна прямая очередь последовательно записывает
    // глубину и читает её в цветном проходе, начиная и завершая кадр в SRV.
    constexpr auto depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    constexpr auto sampleState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const auto transitionShadows = [&](std::span<const ShadowPass> batch,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        std::array<D3D12_RESOURCE_BARRIER, game_shadow12::MaxViews> barriers{};
        for (size_t index = 0; index < batch.size(); ++index)
        {
            const auto view = batch[index].view;
            auto* map = view.IsSun() ? m_sunShadow.Get() : m_localShadow.Get();
            barriers[index].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            // У обеих карт один mip и одна плоскость: subresource == slice.
            barriers[index].Transition = {map, view.Slice(), before, after};
        }
        list->ResourceBarrier(static_cast<UINT>(batch.size()), barriers.data());
    };
    const auto activePasses = std::span(passes.data(), passCount);
    std::array<ShadowDraws, game_shadow12::MaxSunCascades> sunDraws{};
    const auto accumulateShadows = [&](const ShadowPass& pass, const ShadowDraws& draws)
    {
        stats.staticShadowDraws += draws.statics;
        stats.dynamicShadowDraws += draws.rigid + draws.skinned;
        if (pass.view.IsSun())
        {
            stats.sunRigidDraws += draws.rigid;
            stats.sunSkinnedDraws += draws.skinned;
            sunDraws[pass.view.Slice()] = draws;
            constants.sunCasterCounts[pass.view.Slice()] = draws.statics + draws.rigid + draws.skinned;
        }
        else
        {
            ++stats.localShadowMaps;
            ++(pass.view.Kind() == game_shadow12::ViewKind::SpotLight ? stats.spotShadowMaps : stats.pointShadowFaces);
            stats.localShadowDraws += draws.statics + draws.rigid + draws.skinned;
        }
    };
    if (passCount != 0 && recording.UsesShadowLists())
    {
        // Состояния карт задаёт владелец. Списки карт только пишут глубину.
        transitionShadows(activePasses, sampleState, depthState);
        struct RecordResult
        {
            ShadowDraws draws;
            HRESULT closed = E_PENDING;
            DWORD thread = 0;
            Clock::time_point start{};
            Clock::time_point end{};
        };
        std::array<ID3D12GraphicsCommandList*, GameDx12NativeFrame12::kShadowListCount> lists{};
        std::array<RecordResult, GameDx12NativeFrame12::kShadowListCount> results{};
        for (uint32_t index = 0; index < passCount; ++index)
            if (FAILED(recording.OpenShadowList(index, lists[index])))
                return Fail(reason, "failed to reset native shadow list");

        const bool parallelRecording = recording.UsesParallelShadowLists() && passCount > 1 && usePrepared;
        const bool sample = sampleCsm;
        const auto recordRange = [&](uint32_t first, uint32_t last, uint32_t)
        {
            for (uint32_t index = first; index < last; ++index)
            {
                const auto& pass = passes[index];
                auto& result = results[index];
                if (sample)
                {
                    result.thread = GetCurrentThreadId();
                    result.start = Clock::now();
                }
                result.draws = recordShadowMap(lists[index], pass.dsv, pass.size, pass.view.projection, pass.planes, pass.dynamic);
                result.closed = recording.CloseShadowList(index);
                if (sample)
                    result.end = Clock::now();
            }
        };
        GameDx12GRRangeStats12 recordStats;
        if (parallelRecording)
        {
            std::string taskReason;
            // RunRanges ждёт все принятые callbacks и на ошибке. После возврата
            // Cancel может безопасно закрыть оставшиеся открытыми списки.
            if (!GameDx12GR_RunRanges(passCount, 1, recordRange, recordStats, taskReason))
                return Fail(reason, "native shadow recording failed: " + taskReason);
        }
        else
            recordRange(0, passCount, 0);
        for (uint32_t index = 0; index < passCount; ++index)
        {
            if (FAILED(results[index].closed))
                return Fail(reason, "failed to close native shadow list");
            accumulateShadows(passes[index], results[index].draws);
        }
        if (sample)
        {
            uint32_t mask = 0;
            uint32_t overlap = 0;
            std::array<DWORD, GameDx12NativeFrame12::kShadowListCount> participants{};
            for (uint32_t index = 0; index < passCount; ++index)
            {
                mask |= 1u << passes[index].view.projection;
                participants[index] = results[index].thread;
                for (uint32_t prior = 0; prior < index; ++prior)
                    if (results[index].thread != results[prior].thread &&
                        results[index].start < results[prior].end && results[prior].start < results[index].end)
                        ++overlap;
            }
            const auto end = participants.begin() + passCount;
            std::sort(participants.begin(), end);
            const auto used = static_cast<uint32_t>(std::unique(participants.begin(), end) - participants.begin());
            const char* effective = parallelRecording ? "parallel" : "multi";
            const char* fallback = recording.UsesParallelShadowLists() && !parallelRecording ?
                (passCount < 2 ? "single_view" : "packets_unavailable") : "none";
            Msg("ph_dx12_mt_record: frame=%llu mode=%s fallback=%s lists=%u views=%u mask=0x%x participants=%u "
                "overlapping_pairs=%u workers=%u submitted=%llu completed=%llu submit_ms=%.6f wait_ms=%.6f",
                static_cast<unsigned long long>(lighting.frame), effective, fallback, passCount + 2, passCount, mask,
                used, overlap, recordStats.backgroundWorkers, static_cast<unsigned long long>(recordStats.submitted),
                static_cast<unsigned long long>(recordStats.completed), recordStats.submitMs, recordStats.waitMs);
        }
        if (FAILED(recording.BeginColorList()))
            return Fail(reason, "failed to start native color list");
        list = recording.list;
        stats.commandLists = passCount + 2;
        transitionShadows(activePasses, depthState, sampleState);
    }
    else
    {
        for (const auto& pass : activePasses)
        {
            const auto batch = std::span(&pass, 1);
            transitionShadows(batch, sampleState, depthState);
            const auto draws = recordShadowMap(list, pass.dsv, pass.size, pass.view.projection, pass.planes, pass.dynamic);
            accumulateShadows(pass, draws);
            list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
            transitionShadows(batch, depthState, sampleState);
        }
    }
    std::memcpy(frameDestination + offsetof(SceneFrameConstants, sunCasterCounts),
        constants.sunCasterCounts, sizeof(constants.sunCasterCounts));
    if (sunPlan.count != 0 && sampleCsm)
    {
        const float distance = constants.sunCameraForward.w != 0.0f ? sunPlan.cascades[sunPlan.count - 1].splitFar : 0.0f;
        Msg("ph_dx12_csm: frame=%llu profile=%s cascade_count=%u active_cascades=%u resolution=%u shadow_distance=%.3f "
            "total_draws=%u cpu_prep_ms=%.4f cpu_record_ms=%.4f memory_bytes=%llu",
            static_cast<unsigned long long>(lighting.frame), sunSettings.profile, m_sunShadowSlices, sunPlan.count,
            m_sunShadowResolution, distance, stats.staticShadowDraws + stats.dynamicShadowDraws,
            stats.cull_ms, Ms(shadowRecordStart), static_cast<unsigned long long>(m_sunShadowBytes));
        for (uint32_t slice = 0; slice < sunPlan.count; ++slice)
        {
            const auto& cascade = sunPlan.cascades[slice];
            const auto& draws = sunDraws[slice];
            Msg("ph_dx12_csm: slice=%u split=%.3f/%.3f blend=%.3f texel_m=%.5f center=(%.3f,%.3f,%.3f) "
                "extent=%.3f depth_span=%.3f static=%u rigid=%u skinned=%u detail_instances=%u detail_draws=%u "
                "draws=%u hash=%016llx filter=%u receiver_bias_mask=%u receiver_depth_bias=%.8f",
                slice, cascade.splitNear, cascade.splitFar, cascade.blendStart, cascade.texelWorld,
                cascade.lightCenter.x, cascade.lightCenter.y, cascade.lightCenter.z, cascade.radius, cascade.depthSpan,
                draws.statics, draws.rigid - draws.detailDraws, draws.skinned, draws.detail, draws.detailDraws,
                constants.sunCasterCounts[slice], static_cast<unsigned long long>(draws.hash),
                constants.sunFilter, constants.sunReceiverBiasMask, constants.sunDepthBias[slice]);
        }
    }
    recording.StampSceneStart();
    bindColorFrame(list);
    if (constants.lightingFlags[3] != 0 || localShadowViews.count != 0)
    {
        list->OMSetRenderTargets(1, &recording.rtv, FALSE, &recording.dsv);
        const D3D12_VIEWPORT mainViewport{0.0f, 0.0f, static_cast<float>(recording.width),
            static_cast<float>(recording.height), 0.0f, 1.0f};
        const D3D12_RECT mainRect{0, 0, static_cast<LONG>(recording.width), static_cast<LONG>(recording.height)};
        list->RSSetViewports(1, &mainViewport);
        list->RSSetScissorRects(1, &mainRect);
    }
    const double shadowRecordMs = Ms(shadowRecordStart);
    stats.shadowCpuMs += shadowRecordMs;
    auto shadowSrv = m_textureHeap->GetGPUDescriptorHandleForHeapStart();
    shadowSrv.ptr += static_cast<UINT64>(kSunShadowDescriptor) * m_textureDescriptorSize;
    list->SetGraphicsRootDescriptorTable(6, shadowSrv);
    auto localShadowSrv = m_textureHeap->GetGPUDescriptorHandleForHeapStart();
    localShadowSrv.ptr += static_cast<UINT64>(kLocalShadowDescriptor) * m_textureDescriptorSize;
    list->SetGraphicsRootDescriptorTable(kLocalShadowSrvRoot, localShadowSrv);

    const auto drawStatic = [&](const DrawSource12& draw)
    {
        const bool classic = draw.layout == 1;
        bindPipeline(draw.surface, classic ? 0u : 1u,
                     classic ? m_pipelineState.Get() : m_pipelineUv24.Get());
        submitStaticGeometry(draw, IsOverlay(draw.surface) ? kWallmarkClipShift : 0.0f);
        traceStaticDraw(draw, VisualTraceState::Submitted);
        CountSurfaceDraw(stats.surfaces, draw.surface);
        ++stats.draws;
        if (dumpDraws)
            Msg("ph_cmp: dx12draw id=%u variant=%u state=drawn shader_id=%u srv=%u surface='%s'",
                draw.visualId, draw.layout, draw.shaderId, draw.descriptor,
                gameaux12::SurfaceModeName(draw.surface));
    };
    const auto drawMesh = [&](const AuxMesh& mesh, uint32_t id, SurfaceMode surface,
                              uint32_t offset, uint32_t count, ID3D12PipelineState* regular, float skyHemi = 0.0f)
    {
        bindPipeline(surface, 2, regular);
        submitAuxGeometry(mesh, id, surface, offset, count, regular == m_pipelineDetail.Get(), skyHemi);
        CountSurfaceDraw(auxStats.surfaces, surface);
    };
    const auto drawObjectPart = [&](const AuxMesh& mesh, const gameaux12::ObjectDraw& object)
    {
        drawMesh(mesh, object.id, mesh.surface, object.poseOffset, 1, m_pipelineSkeletal.Get(), object.skyHemi);
        if (traceAux && object.id == extras.traceId && m_auxTraceMessages < kMaxVisualTraceMessages)
        {
            const auto handle = static_cast<uint32_t>(&mesh - m_auxMeshes.data());
            Msg("ph_dx12_f43_r1: aux_gpu object_id=%u generation=%llu frame=%llu handle=%u "
                "state=submitted pso='%s' srv=%u srv_format=%u allocation=%u vb_offset=%u "
                "vb_stride=%u vb_bytes=%u ib_offset=%u icount=%u ib_format=%u "
                "start_index=0 base_vertex=0 pose_offset=%u bone_count=%u capped=%d",
                static_cast<unsigned>(object.id), static_cast<unsigned long long>(object.generation),
                static_cast<unsigned long long>(m_visualTraceFrame), handle,
                gameaux12::SurfaceModeName(mesh.surface), mesh.descriptor,
                static_cast<unsigned>(m_textures[mesh.descriptor - 1].srvFormat), mesh.allocation,
                mesh.vbOffset, mesh.vb.StrideInBytes, mesh.vb.SizeInBytes, mesh.ibOffset, mesh.indices,
                static_cast<unsigned>(mesh.ib.Format), object.poseOffset, object.boneCount,
                ++m_auxTraceMessages == kMaxVisualTraceMessages ? 1 : 0);
        }
        ++(object.skinned ? auxStats.animatedDraws : auxStats.rigidDraws);
        if (object.id == extras.traceId)
            ++auxStats.tracedDraws;
    };

    // Базовое покрытие static: classic, uv@24, flora с мировой матрицей.
    for (uint32_t variant = 1; variant <= 3; ++variant)
    {
        for (const auto& draw : m_draws)
        {
            if (draw.layout != variant)
                continue;
            if (usePrepared ? !prepared.statics[&draw - m_draws.data()] :
                cull && !SphereInside(planes, draw.center, draw.radius))
            {
                ++stats.culled;
                traceStaticDraw(draw, VisualTraceState::Culled);
                if (dumpDraws)
                    Msg("ph_cmp: dx12draw id=%u variant=%u state=culled shader_id=%u srv=%u surface='%s'",
                        draw.visualId, variant, draw.shaderId, draw.descriptor,
                        gameaux12::SurfaceModeName(draw.surface));
                continue;
            }
            if (IsOverlay(draw.surface) || IsBlended(draw.surface))
            {
                deferred.push_back({&draw, nullptr, nullptr, draw.surface, draw.transparency, viewDepth(draw.center)});
                continue;
            }
            drawStatic(draw);
        }
    }

    if (validExtras)
    {
        uint32_t next = poseCount;
        for (const auto& object : extras.objects)
        {
            const auto parts = objectParts(object);
            if (parts.empty())
                continue;
            if (usePrepared ? !prepared.objects[&object - extras.objects.data()] :
                cull && !SphereInside(planes, object.center, object.radius))
            {
                ++(object.skinned ? auxStats.animatedCulled : auxStats.rigidCulled);
                continue;
            }
            ++(object.skinned ? auxStats.animatedVisible : auxStats.rigidVisible);
            bool submitted = false;
            for (const uint32_t handle : parts)
            {
                const auto* mesh = objectMesh(handle, object);
                if (mesh == nullptr)
                    continue;
                if (IsOverlay(mesh->surface) || IsBlended(mesh->surface))
                    deferred.push_back({nullptr, mesh, &object, mesh->surface, mesh->transparency, viewDepth(object.center)});
                else
                    drawObjectPart(*mesh, object);
                submitted = true;
            }
            if (submitted)
                ++(object.skinned ? auxStats.animatedSubmitted : auxStats.rigidSubmitted);
        }

        for (const auto& group : extras.details)
        {
            if (group.mesh >= m_auxMeshes.size() || m_auxMeshes[group.mesh].skinned ||
                m_auxMeshes[group.mesh].requiredBones != 1)
            {
                auxStats.detailMissingMaterial += static_cast<uint32_t>(group.transforms.size());
                continue;
            }
            const auto& mesh = m_auxMeshes[group.mesh];
            const uint32_t first = next;
            for (const auto& transform : group.transforms)
            {
                const float scale = std::hypot(transform._11, transform._12, transform._13);
                const float center[] = {transform._41, transform._42, transform._43};
                if (cull && !SphereInside(planes, center, mesh.radius * scale))
                {
                    ++auxStats.detailCulled;
                    continue;
                }
                ++auxStats.detailVisible;
                if (next == gameaux12::kMaxFrameTransforms)
                {
                    ++auxStats.detailBudget;
                    continue;
                }
                std::memcpy(transformDestination + next * sizeof(transform), &transform, sizeof(transform));
                ++next;
            }
            if (next > first)
            {
                const uint32_t count = next - first;
                drawMesh(mesh, group.mesh, SurfaceMode::Cutout, first, count, m_pipelineDetail.Get());
                auxStats.detailSubmitted += count;
                ++auxStats.detailDraws;
            }
        }
    }

    // R1 сортирует накладки как раньше. В B4 вода (R4 priority 2) идёт
    // перед стеклом (priority 3), внутри группы порядок от дальних к ближним.
    const bool sortTransparency = gameaux12::TransparencyEnabled();
    const auto passPriority = [sortTransparency](const DeferredDraw& entry)
    {
        if (sortTransparency && entry.surface == SurfaceMode::Translucent)
        {
            const auto category = entry.transparency == gameaux12::TransparentMaterial::None
                ? gameaux12::TransparentMaterial::Glass : entry.transparency;
            return static_cast<unsigned>(category);
        }
        return IsBlended(entry.surface) ? 1u : 0u;
    };
    std::stable_sort(deferred.begin(), deferred.end(), [&](const DeferredDraw& left, const DeferredDraw& right)
    {
        const unsigned leftPriority = passPriority(left);
        const unsigned rightPriority = passPriority(right);
        if (leftPriority != rightPriority)
            return leftPriority < rightPriority;
        return leftPriority != 0 && left.depth > right.depth;
    });
    const auto beginWater = [&]()
    {
        if (waterPass)
            return;
        const auto copyStart = Clock::now();
        const uint32_t slotDescriptor = kNullWaterDescriptor + kWaterSrvCount * (1 + frameSlot);
        // RecordNativeFrame validates frameSlot before recording. The reserved
        // range is checked at compile time; no frame may write local-shadow t6.
        static_assert(kWaterSrvCount == 3);
        const auto heapStart = m_textureHeap->GetCPUDescriptorHandleForHeapStart();
        auto destination = heapStart;
        destination.ptr += static_cast<SIZE_T>(slotDescriptor) * m_textureDescriptorSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Format = m_textures[normalTexture->second].srvFormat;
        srv.Texture2D.MipLevels = m_textures[normalTexture->second].image->GetDesc().MipLevels;
        recording.device->CreateShaderResourceView(m_textures[normalTexture->second].image.Get(), &srv, destination);
        destination.ptr += m_textureDescriptorSize;
        srv.Texture2D.MipLevels = 1;
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        recording.device->CreateShaderResourceView(recording.opaqueColor, &srv, destination);
        destination.ptr += m_textureDescriptorSize;
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        recording.device->CreateShaderResourceView(recording.depth, &srv, destination);
        recording.BeginWater();
        auto waterSrv = nullWaterSrv;
        waterSrv.ptr += static_cast<UINT64>(slotDescriptor - kNullWaterDescriptor) * m_textureDescriptorSize;
        list->SetGraphicsRootDescriptorTable(7, waterSrv);
        waterPass = true;
        ++stats.sceneCopies;
        stats.waterCpuMs += Ms(copyStart);
    };
    const auto endWater = [&]()
    {
        if (!waterPass)
            return;
        const auto endStart = Clock::now();
        recording.EndWater();
        list->SetGraphicsRootDescriptorTable(7, nullWaterSrv);
        waterPass = false;
        stats.waterCpuMs += Ms(endStart);
    };
    const auto drawPuddles = [&]()
    {
        if (!waterAvailable || !m_pipelinePuddle || water.wetness <= 0.0f)
            return;
        for (const auto& puddle : m_puddles)
        {
            if (!FiniteMatrix(puddle.world) || !std::isfinite(puddle.height) ||
                !std::isfinite(puddle.radius) || puddle.radius <= 0.0f)
                continue;
            const float center[] = {puddle.world._41, puddle.world._42, puddle.world._43};
            const float distance = std::hypot(center[0] - cameraPosition[0],
                center[1] - cameraPosition[1], center[2] - cameraPosition[2]);
            if (distance > 150.0f || (cull && !SphereInside(planes, center, puddle.radius)))
                continue;
            beginWater();
            if (stats.puddleDraws == 0)
            {
                list->SetPipelineState(m_pipelinePuddle.Get());
                bindMaterial(kNullDescriptor, 0, SurfaceMode::Translucent, false,
                             gameaux12::TransparentMaterial::None);
                list->SetGraphicsRoot32BitConstant(2, kAdvancedWater | kPuddleMaterial, 0);
            }
            const auto puddleStart = Clock::now();
            list->SetGraphicsRoot32BitConstants(3, 16, &puddle.world, 0);
            list->SetGraphicsRoot32BitConstant(5, std::bit_cast<uint32_t>(water.wetness * puddle.height), 3);
            list->DrawInstanced(6, 1, 0, 0);
            ++stats.puddleDraws;
            stats.waterCpuMs += Ms(puddleStart);
        }
        if (stats.puddleDraws != 0)
        {
            worldIdentity = false;
            boundPipeline = nullptr;
        }
    };
    bool drewPuddlePass = false;
    for (const auto& entry : deferred)
    {
        if (!drewPuddlePass && entry.surface == SurfaceMode::Translucent)
        {
            drawPuddles();
            drewPuddlePass = true;
        }
        if (waterAvailable && gamewater12::WaterEnabled() &&
            entry.surface == SurfaceMode::Translucent && entry.transparency == gameaux12::TransparentMaterial::Water)
        {
            beginWater();
            ++stats.waterDraws;
        }
        else
            endWater();

        const auto transparentDrawStart = waterPass ? Clock::now() : Clock::time_point{};
        if (entry.draw != nullptr)
            drawStatic(*entry.draw);
        else
            drawObjectPart(*entry.mesh, *entry.object);
        if (waterPass)
            stats.waterCpuMs += Ms(transparentDrawStart);
    }
    if (!drewPuddlePass)
        drawPuddles();
    endWater();
    stats.record_ms = Ms(recordStart);
    stats.sceneCpuMs = (std::max)(0.0, stats.record_ms - shadowRecordMs - stats.waterCpuMs);
    if (dumpDraws)
        ++compareDumpFrames;
    if (stats.draws == 0 && !m_limitLogged)
    {
        m_limitLogged = true;
        Msg("ph_dx12_f41: no visible static draws total=%u cull=%d",
            stats.total, cull ? 1 : 0);
    }
    return true;
}

void GameDx12StaticScene12::OnLevelTeardown(uint64_t nextEpoch)
{
    if (m_gpuUnknown)
        return;
    m_loaded = false;
    m_uploadConfirmed = false;
    m_passRecorded = false;
    m_passSubmitted = false;
    m_levelSession = 0;
    m_levelEpoch = nextEpoch;
    ReleaseGpu();
}

void GameDx12StaticScene12::OnDeviceReset(uint64_t nextEpoch)
{
    if (GpuCompletionUnknown())
        return;
    // Bridge вызывает это после Drain. Меняется размер окна/R4,
    // D3D12 device и level resources остаются теми же.
    m_levelEpoch = nextEpoch;
}

void GameDx12StaticScene12::ReleaseGpu()
{
    m_draws.clear();
    m_shadowCasters = {};
    for (auto& pool : m_vertexPools)
        pool.resource.Reset();
    m_vertexPools.clear();
    for (auto& pool : m_indexPools)
        pool.resource.Reset();
    m_indexPools.clear();
    for (auto& texture : m_textures)
    {
        texture.image.Reset();
        texture.upload.Reset();
    }
    m_textures.clear();
    m_textureKeys.clear();
    m_auxGeometry.clear();
    m_auxMeshes.clear();
    m_auxResources = {};
    m_copyStaging.Reset();
    if (m_frameTransformsMapped != nullptr && m_frameTransforms != nullptr)
        m_frameTransforms->Unmap(0, nullptr);
    m_frameTransformsMapped = nullptr;
    m_frameTransforms.Reset();
    if (m_frameConstantsMapped != nullptr && m_frameConstants != nullptr)
        m_frameConstants->Unmap(0, nullptr);
    m_frameConstantsMapped = nullptr;
    m_frameConstants.Reset();
    m_sunShadow.Reset();
    m_sunShadowDsv.Reset();
    m_localShadow.Reset();
    m_localShadowDsv.Reset();
    m_localShadowSlices = 0;
    m_localShadowBytes = 0;
    m_localShadowAllocation = {};
    m_sunShadowPipelines = {};
    m_sunShadowAuxPipeline.Reset();
    m_textureHeap.Reset();
    m_pipelineUv24.Reset();
    m_pipelineSkeletal.Reset();
    m_pipelineDetail.Reset();
    m_pipelinePuddle.Reset();
    m_puddles.clear();
    m_deferredPipelines = {};
    m_pipelineState.Reset();
    m_rootSignature.Reset();
    m_loaded = false;
}

void GameDx12StaticScene12::QuarantineGpu() noexcept
{
    m_quarantined = true;
    m_loaded = false;
    // Ресурсы не освобождаются: их последнее использование не подтверждено.
    // Сырые указатели сохраняются в process-lifetime карантине, поэтому
    // декструкторы ComPtr не вызывают Release и объекты живут до exit.
    g_staticQuarantine.occupied = true;
    const auto park = [](IUnknown*& slot, IUnknown* object) noexcept
    {
        if (slot == nullptr)
            slot = object;
    };
    for (auto& pool : m_vertexPools)
    {
        if (g_staticQuarantine.bufferCount < kQuarantineBuffers)
            park(g_staticQuarantine.buffers[g_staticQuarantine.bufferCount++],
                 pool.resource.Detach());
        else
            pool.resource.Detach();
    }
    for (auto& pool : m_indexPools)
    {
        if (g_staticQuarantine.bufferCount < kQuarantineBuffers)
            park(g_staticQuarantine.buffers[g_staticQuarantine.bufferCount++],
                 pool.resource.Detach());
        else
            pool.resource.Detach();
    }
    for (auto& texture : m_textures)
    {
        if (g_staticQuarantine.textureCount < kQuarantineTextures)
            park(g_staticQuarantine.textures[g_staticQuarantine.textureCount++],
                 texture.image.Detach());
        else
            texture.image.Detach();
        texture.upload.Detach();
    }
    for (auto& geometry : m_auxGeometry)
        geometry.Detach();
    m_copyStaging.Detach();
    m_frameTransforms.Detach();
    m_frameTransformsMapped = nullptr;
    m_pipelineSkeletal.Detach();
    m_pipelineDetail.Detach();
    m_pipelinePuddle.Detach();
    m_puddles.clear();
    m_sunShadow.Detach();
    m_sunShadowDsv.Detach();
    m_localShadow.Detach();
    m_localShadowDsv.Detach();
    for (auto& pipeline : m_sunShadowPipelines)
        pipeline.Detach();
    m_sunShadowAuxPipeline.Detach();
    for (auto& pipelines : m_deferredPipelines)
        for (auto& pipeline : pipelines.states)
            pipeline.Detach();
    m_textureKeys.clear();
    m_auxGeometry.clear();
    m_auxMeshes.clear();
    if (m_frameConstantsMapped != nullptr && m_frameConstants != nullptr)
        m_frameConstants->Unmap(0, nullptr);
    m_frameConstantsMapped = nullptr;
    park(g_staticQuarantine.constants, m_frameConstants.Detach());
    park(g_staticQuarantine.heap, m_textureHeap.Detach());
    park(g_staticQuarantine.pipelineState, m_pipelineState.Detach());
    if (g_staticQuarantine.pipelineState != nullptr)
        m_pipelineUv24.Detach();
    park(g_staticQuarantine.rootSignature, m_rootSignature.Detach());
    m_draws.clear();
    m_shadowCasters = {};
    m_vertexPools.clear();
    m_indexPools.clear();
    m_textures.clear();
}

void GameDx12StaticScene12::Shutdown(bool gpuCompletionConfirmed)
{
    if (gpuCompletionConfirmed && !m_gpuUnknown)
    {
        ReleaseGpu();
        return;
    }
    QuarantineGpu();
}

void GameDx12StaticScene12_ResolveMaterials(
    std::span<const uint8_t> levelBytes, const xraylevel12::LevelPools& pools,
    std::vector<std::string>& uniqueTextureNames,
    std::vector<GameDx12StaticMaterial12>& materials)
{
    uniqueTextureNames.clear();
    materials.assign(pools.visuals.size(), GameDx12StaticMaterial12{});

    // Один и тот же shader_id встречается у тысяч визуалов, поэтому запись
    // level-шейдера разбирается один раз, а не на каждый visual.
    struct Binding12
    {
        GameDx12StaticMaterial12 material;
        bool emptyRecord = false;
        bool malformedRecord = false;
    };
    std::unordered_map<uint32_t, Binding12> bindings;
    std::unordered_map<std::string, uint32_t> textureIndex;
    const auto registerTexture = [&](const std::string& name)
    {
        if (name.empty())
            return GameDx12StaticMaterial12::kNoTexture;
        const auto [found, inserted] = textureIndex.emplace(name, static_cast<uint32_t>(uniqueTextureNames.size()));
        if (inserted)
            uniqueTextureNames.push_back(name);
        return found->second;
    };

    const auto resolve = [&](std::string_view shader, std::string_view textures)
    {
        GameDx12StaticMaterial12 material;
        material.surface = gameaux12::ClassifySurface(shader);
        static const bool sourceAlphaTest = []
        {
            const char* disabled = std::getenv("OGSR_DX12_GAME_SOURCE_ALPHA_TEST");
            return !disabled || std::strcmp(disabled, "0") != 0;
        }();
        if (sourceAlphaTest && material.surface == SurfaceMode::Auto &&
            !shader.empty() && shader.size() < 256)
        {
            const std::string sourceShader(shader);
            bool blended = false;
            if (GameDx12ReadR4DeferredAlphaSource(sourceShader.c_str(), blended))
            {
                if (blended)
                    material.checkAlphaCoverage = true;
                else
                    material.surface = SurfaceMode::Cutout;
            }
        }

        // R4 uber_deffer включает USE_LM_HEMI для третьей текстуры lmap*.
        // У таких материалов NORMAL.w не является источником hemi основного прохода.
        const auto firstComma = textures.find(',');
        if (firstComma != textures.npos)
        {
            const auto nextTextures = textures.substr(firstComma + 1);
            const auto secondComma = nextTextures.find(',');
            if (secondComma != nextTextures.npos)
            {
                const auto hemiName = gameaux12::CanonicalBaseTextureName(nextTextures.substr(secondComma + 1));
                material.usesHemiLightmap = hemiName.starts_with("lmap");
                if (material.usesHemiLightmap)
                    material.hemiTextureIndex = registerTexture(hemiName);
            }
        }
        material.wallmarkClamp = gameaux12::UsesWallmarkClamp(shader);
        material.transparency = gameaux12::ClassifyTransparentMaterial(shader);
        if (material.surface != SurfaceMode::Unsupported)
        {
            const std::string texture = gameaux12::SurfaceBaseTextureName(shader, textures);
            material.textureIndex = registerTexture(texture);
            static const bool terrainDetail = []
            {
                const char* value = std::getenv("OGSR_DX12_GAME_TERRAIN_DETAIL");
                return value == nullptr || std::strcmp(value, "0") != 0;
            }();
            if (!texture.empty() && !shader.empty() && shader.size() < 256)
            {
                std::array<std::string, 4> names;
                float scale = 0.0f;
                const std::string sourceShader(shader);
                if (GameDx12ReadR4TerrainSource(sourceShader.c_str(), texture.c_str(), names, scale))
                {
                    material.terrainBaseHemi = true;
                    if (!terrainDetail)
                        return material;
                    material.terrainScale = scale;
                    material.terrainTextures[0] = registerTexture(
                        gameaux12::CanonicalBaseTextureName(texture + "_mask"));
                    for (size_t i = 0; i < names.size(); ++i)
                        material.terrainTextures[i + 1] =
                            registerTexture(gameaux12::CanonicalBaseTextureName(names[i]));
                }
            }
        }
        return material;
    };

    uint32_t withTexture = 0;
    uint32_t withoutRecord = 0;
    uint32_t withoutSlash = 0;
    const auto& selected = R1VisualSelection();
    for (size_t index = 0; index < pools.visuals.size(); ++index)
    {
        const auto& visual = pools.visuals[index];
        if (R1VisualTraceSlot(selected, visual.id) != UINT8_MAX)
        {
            const bool hasOverride = !visual.shader_override.empty();
            const auto record = hasOverride ? std::string_view{} :
                xraylevel12::FindShaderBinding(levelBytes, visual.shader_id);
            const size_t slash = record.find('/');
            const std::string_view shader = hasOverride ? std::string_view(visual.shader_override) :
                record.substr(0, slash);
            const std::string_view textureList = hasOverride ? std::string_view(visual.texture_override) :
                slash == record.npos ? std::string_view{} : record.substr(slash + 1);
            const auto baseTexture = textureList.substr(0, textureList.find(','));
            Msg("ph_dx12_f43_r1: static visual_id=%u type=%u state=source shader_id=%u "
                "binding='%s' shader='%.*s' source_texture='%.*s' "
                "swi=%d swi_id=%u world=%d",
                visual.id, static_cast<unsigned>(visual.type), visual.shader_id,
                hasOverride ? "ogf_texture" : "level_shaders",
                static_cast<int>((std::min)(shader.size(), size_t{255})), shader.empty() ? "" : shader.data(),
                static_cast<int>((std::min)(baseTexture.size(), size_t{255})),
                baseTexture.empty() ? "" : baseTexture.data(),
                visual.has_swi ? 1 : 0, visual.swi_id, visual.has_world ? 1 : 0);
            Msg("ph_dx12_f43e: geometry visual=%u class=static type=%u drawable=%d "
                "vb=%u base=%u count=%u ib=%u start=%u count=%u "
                "lod_swi=%d swi_id=%u world=[%.5f %.5f %.5f %.5f; "
                "%.5f %.5f %.5f %.5f; %.5f %.5f %.5f %.5f; %.5f %.5f %.5f %.5f]",
                visual.id, static_cast<unsigned>(visual.type), visual.drawable ? 1 : 0,
                visual.vb_id, visual.vbase, visual.vcount, visual.ib_id, visual.ibase, visual.icount,
                visual.has_swi ? 1 : 0, visual.swi_id,
                visual.world[0], visual.world[1], visual.world[2], visual.world[3],
                visual.world[4], visual.world[5], visual.world[6], visual.world[7],
                visual.world[8], visual.world[9], visual.world[10], visual.world[11],
                visual.world[12], visual.world[13], visual.world[14], visual.world[15]);
        }
        if (!visual.drawable)
            continue;

        auto& material = materials[index];
        if (!visual.shader_override.empty())
        {
            // FBasicVisual::Load применяет парный OGF_TEXTURE конкретного
            // visual поверх shader_id, даже если другие части делят shader_id.
            material = resolve(visual.shader_override, visual.texture_override);
            material.hasOverride = true;
        }
        else
        {
            const auto [found, inserted] = bindings.try_emplace(visual.shader_id);
            auto& binding = found->second;
            if (inserted)
            {
                const std::string_view record =
                    xraylevel12::FindShaderBinding(levelBytes, visual.shader_id);
                // R4 LevelLoad делит запись по первым '/'.
                const size_t slash = record.find('/');
                binding.emptyRecord = record.empty();
                binding.malformedRecord = !record.empty() && slash == record.npos;
                if (slash != record.npos)
                    binding.material = resolve(record.substr(0, slash), record.substr(slash + 1));
            }
            material = binding.material;
            withoutRecord += binding.emptyRecord;
            withoutSlash += binding.malformedRecord;
        }
        if (material.textureIndex != GameDx12StaticMaterial12::kNoTexture)
            ++withTexture;
    }
    Msg("ph_dx12_f41: materials drawable=%u textured=%u no_texture=%u "
        "distinct_shader_ids=%zu empty_record=%u record_without_slash=%u",
        pools.visuals_drawable, withTexture, pools.visuals_drawable - withTexture,
        bindings.size(), withoutRecord, withoutSlash);
}

void GameDx12StaticScene12_LogDeclarationInventory(
    const xraylevel12::LevelPools& pools)
{
    Msg("ph_dx12_f41: pools vb=%zu ib=%zu visuals=%zu drawable=%u rejected=%u "
        "vertex_payload_bytes=%llu",
        pools.vertex_pools.size(), pools.index_pools.size(),
        pools.visuals.size(), pools.visuals_drawable, pools.visuals_rejected,
        static_cast<unsigned long long>(pools.vertex_payload_bytes));

    for (const auto& pool : pools.vertex_pools)
    {
        char signature[192] = {};
        size_t used = 0;
        for (uint32_t element = 0;
             element < pool.declaration_count &&
             element < xraylevel12::kMaxDeclarationElements;
             ++element)
        {
            const auto& decl = pool.declaration[element];
            if (decl.stream == 0xFF)
                break;
            const int written = std::snprintf(
                signature + used, sizeof(signature) - used, "[%u/%u t%u u%u/%u]",
                static_cast<unsigned>(decl.stream), static_cast<unsigned>(decl.offset),
                static_cast<unsigned>(decl.type), static_cast<unsigned>(decl.usage),
                static_cast<unsigned>(decl.usage_index));
            if (written <= 0 || static_cast<size_t>(written) >= sizeof(signature) - used)
                break;
            used += static_cast<size_t>(written);
        }
        Msg("ph_dx12_f41: vb_pool id=%u count=%u stride=%u supported=%d "
            "layout='%s' decl=%s",
            pool.id, pool.count, pool.stride, pool.supported ? 1 : 0,
            xraylevel12::VertexLayoutName(pool.layout), signature);
    }

    uint32_t classicVisuals = 0;
    uint32_t uv24Visuals = 0;
    for (const auto& visual : pools.visuals)
    {
        if (!visual.drawable)
            continue;
        if (visual.layout == xraylevel12::VertexLayout12::Classic)
            ++classicVisuals;
        else if (visual.layout == xraylevel12::VertexLayout12::UvAt24)
            ++uv24Visuals;
    }
    Msg("ph_dx12_f41: layout_visuals classic=%u uv24=%u", classicVisuals,
        uv24Visuals);

    // Сводка причин отказа по фактическим данным уровня.
    uint32_t unsupportedType = 0;
    uint32_t unsupportedDeclaration = 0;
    uint32_t missingContainer = 0;
    uint32_t invalidRange = 0;
    uint32_t other = 0;
    for (const auto& visual : pools.visuals)
    {
        if (visual.drawable)
            continue;
        switch (visual.reject)
        {
        case xraylevel12::ReadError::UnsupportedVisualType:
            ++unsupportedType;
            break;
        case xraylevel12::ReadError::UnsupportedDeclaration:
            ++unsupportedDeclaration;
            break;
        case xraylevel12::ReadError::MissingOgfGcContainer:
        case xraylevel12::ReadError::UnsupportedOgfSubchunk:
            ++missingContainer;
            break;
        case xraylevel12::ReadError::InvalidVisualCounts:
        case xraylevel12::ReadError::VertexRangeOutsideVb:
            ++invalidRange;
            break;
        default:
            ++other;
            break;
        }
    }
    Msg("ph_dx12_f41: rejected_type=%u rejected_declaration=%u "
        "rejected_container=%u rejected_range=%u rejected_other=%u",
        unsupportedType, unsupportedDeclaration, missingContainer, invalidRange,
        other);

    for (const auto& entry : pools.rejected_chunks)
    {
        Msg("ph_dx12_f41: rejected_chunk type=%u chunk=%u count=%u",
            entry.type, entry.chunkId, entry.count);
    }
}

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
