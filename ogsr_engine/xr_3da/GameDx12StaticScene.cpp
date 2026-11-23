#include "stdafx.h"
#include "GameDx12StaticScene.h"

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

namespace
{
using Clock = std::chrono::steady_clock;
using gameaux12::SurfaceMode;

constexpr uint64_t kFrameConstantsSize = 256;
constexpr uint64_t kFrameConstantsSlots = 2;
constexpr uint64_t kStagingBytes = 128ull * 1024ull * 1024ull;
constexpr uint64_t kStagingAlignment = 512ull;
constexpr uint32_t kMaxTextures = 16384;
constexpr uint64_t kMaxAuxGeometryBytes = 64ull * 1024 * 1024;
constexpr uint64_t kMaxAuxTextureBytes = 256ull * 1024 * 1024;
constexpr uint64_t kTransformSlotBytes =
    gameaux12::kMaxFrameTransforms * sizeof(DirectX::XMFLOAT4X4);
static_assert(sizeof(DirectX::XMFLOAT4X4) == 4 * 4 * sizeof(float));
constexpr uint64_t kMaxTextureBytes = 16ull * 1024ull * 1024ull;
constexpr uint64_t kMaxSceneTextureBytes = 4ull * 1024ull * 1024ull * 1024ull;
constexpr const char* kFallbackTextureName = "";
constexpr uint8_t kMaxVisualTraceMessages = 12;

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

bool CompileShader(const char* source, const char* entryPoint,
                   const char* target, ID3D10Blob** outBlob,
                   std::string& reason)
{
    ComPtr<ID3D10Blob> error;
    const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr,
                                  nullptr, entryPoint, target, 0, 0, outBlob,
                                  &error);
    if (FAILED(hr))
    {
        if (error != nullptr)
            reason.assign(static_cast<const char*>(error->GetBufferPointer()),
                          error->GetBufferSize());
        else
            reason = "static scene shader compile failed";
        return false;
    }
    return true;
}

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
    int2   tc       : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float3 world    : TEXCOORD1;
};

cbuffer FrameCB : register(b0)
{
    float4x4 ViewProjection;
    float4 CameraPosition;
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
};

VSOutput main(VSInput input)
{
    VSOutput output;
    const float4 worldPosition = mul(float4(input.position, 1.0f), World);
    output.position = mul(worldPosition, ViewProjection);
    if (output.position.z > 0.0f)
        output.position.z = max(0.0f, output.position.z - WallmarkClipShift);
    output.normal = mul(2.0f * input.nh.zyx - 1.0f, (float3x3)World);
    // FTreeVisual и tree_s используют короткие UV без дробной добавки и масштаб 16.
    output.uv = TreeGeometry != 0 ? float2(input.tc) * (16.0f / 32768.0f) :
        (float2(input.tc) + float2(input.tangent.w, input.binormal.w)) * (32.0f / 32768.0f);
    output.world = worldPosition.xyz;
    return output;
}
)";

constexpr const char* kAuxVertexShader = R"(
cbuffer FrameCB : register(b0)
{
    float4x4 ViewProj;
};
cbuffer DrawCB : register(b3)
{
    uint MatrixBase;
};

cbuffer AuxGeometryCB : register(b4)
{
    uint ReservedGeometry;
    float WallmarkClipShift;
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
};

VSOutput main(float3 position : POSITION, float3 normal : NORMAL, float2 uv : TEXCOORD0,
              uint4 bones : BLENDINDICES, float4 weights : BLENDWEIGHT,
              uint instance : SV_InstanceID)
{
    // Матрицы позы уже включают object-to-world. Для деталей offset
    // адресует последовательность инстансов, у каждого одна матрица.
    const uint baseIndex = MatrixBase + instance;
    float3 worldPosition = 0.0f;
    float3 worldNormal = 0.0f;
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
    VSOutput output;
    output.position = mul(float4(worldPosition, 1.0f), ViewProj);
    if (output.position.z > 0.0f)
        output.position.z = max(0.0f, output.position.z - WallmarkClipShift);
    const float normalSquared = dot(worldNormal, worldNormal);
    output.normal = normalSquared > 0.0f ? normalize(worldNormal) : float3(0, 1, 0);
    output.uv = uv;
    output.world = worldPosition;
    return output;
}
)";

constexpr const char* kScenePixelShader = R"(
Texture2D BaseColor : register(t0);
SamplerState BaseSampler : register(s0);
SamplerState WallmarkSampler : register(s1);

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
};

float4 main(PSInput input) : SV_Target
{
    const float3 n = normalize(input.normal);
    float4 baseColor = float4(0.72f, 0.72f, 0.76f, 1.0f);
    const bool textured = (TextureMode & 1) != 0;
    if (textured)
        baseColor = (TextureMode & 16) != 0 ? BaseColor.SampleLevel(WallmarkSampler, input.uv, 0) :
            BaseColor.Sample(BaseSampler, input.uv);
    if ((TextureMode & 2) != 0)
        clip(baseColor.a - AlphaThreshold);
    if (ViewMode == 1)
        return float4(frac((MaterialId + 1) * float3(0.618f, 0.381f, 0.173f)), 1.0f);
    if (ViewMode == 2)
        return float4(frac(input.uv), saturate(n.y * 0.5f + 0.5f), 1.0f);
    if (ViewMode == 3)
        return textured ? float4(0.1f, 0.1f, 0.1f, 1.0f)
                        : float4(0.9f, 0.1f, 0.1f, 1.0f);
    const float3 sunDir = normalize(float3(-0.42f, 0.78f, 0.35f));
    const float sun = saturate(dot(n, sunDir));
    // Базовое освещение первого этапа: солнце + верхняя полусфера.
    const float sky = saturate(n.y) * 0.22f;
    const float light = 0.55f + 0.45f * sun + sky;
    const float3 linearColor = saturate(baseColor.rgb * ((TextureMode & 4) != 0 ? 1.0f : light));
    const float alpha = (TextureMode & 8) != 0 ? baseColor.a : 1.0f;
    return float4(max(min(12.92f * linearColor, 0.04045f),
                      1.055f * pow(linearColor, 1.0f / 2.4f) - 0.055f), alpha);
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

uint32_t SceneViewMode()
{
    static const uint32_t mode = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_SCENE_VIEW");
        if (value == nullptr || value[0] == '\0')
            return 0u;
        const unsigned long parsed = std::strtoul(value, nullptr, 10);
        return parsed <= 3ul ? static_cast<uint32_t>(parsed) : 0u;
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

bool GameDx12StaticScene12::CreatePipeline(ID3D12Device* device,
                                           std::string& reason)
{
    D3D12_ROOT_PARAMETER params[6] = {};
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
    params[5].Constants.Num32BitValues = 2;
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

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
    const D3D12_STATIC_SAMPLER_DESC samplers[] = {sampler, wallmarkSampler};

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

    const D3D12_INPUT_ELEMENT_DESC layout[] = {
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
    desc.InputLayout = {layout, static_cast<UINT>(std::size(layout))};
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

    // F4.1: вторая фактическая раскладка level.geom — TEXCOORD0 по смещению
    // 24 (вместо COLOR@24 + TEXCOORD@28). Шейдеры и состояния те же, поэтому
    // отличается только input layout.
    const D3D12_INPUT_ELEMENT_DESC layoutUv24[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BINORMAL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R16G16_SINT, 0, 24,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    desc.InputLayout = {layoutUv24, static_cast<UINT>(std::size(layoutUv24))};
    hr = device->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(m_pipelineUv24.GetAddressOf()));
    if (FAILED(hr))
        return Fail(reason, "static scene pipeline creation failed (uv24)");
    if (!createDeferred(m_deferredPipelines[1]))
        return false;

    ComPtr<ID3D10Blob> auxVertex;
    if (!CompileShader(kAuxVertexShader, "main", "vs_5_0",
                       auxVertex.GetAddressOf(), reason))
        return false;
    using Vertex = gameaux12::Vertex;
    static_assert(sizeof(Vertex) == 64);
    const D3D12_INPUT_ELEMENT_DESC auxLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, offsetof(Vertex, bones),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, weights),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    desc.VS = {auxVertex->GetBufferPointer(), auxVertex->GetBufferSize()};
    desc.InputLayout = {auxLayout, static_cast<UINT>(std::size(auxLayout))};
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
        heap.NumDescriptors = kMaxTextures + 1;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&heap,
                IID_PPV_ARGS(m_textureHeap.GetAddressOf()))))
            return Fail(reason, "scene descriptor heap creation failed");
        m_textureDescriptorSize = device->GetDescriptorHandleIncrementSize(heap.Type);
        device->CreateShaderResourceView(nullptr, &nullSrv,
            m_textureHeap->GetCPUDescriptorHandleForHeapStart());
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
            draw.surface = ResolveSurface(material.surface,
                haveTexture && textures[material.textureIndex].alphaTest);
            draw.wallmarkClamp = material.wallmarkClamp;
            draw.layout =
                visual.layout == xraylevel12::VertexLayout12::UvAt24 ? 2 : 1;
            if (visual.has_world)
            {
                // Flora: геометрия в локальных координатах, мир задаёт
                // OGF_TREEDEF2. Центр сферы и радиус переводятся в мир здесь,
                // чтобы culling работал по реальному положению дерева.
                draw.layout = 3;
                for (uint32_t element = 0; element < 16; ++element)
                    draw.world[element] = visual.world[element];
                const float local[3] = {visual.sphere_center[0],
                                        visual.sphere_center[1],
                                        visual.sphere_center[2]};
                draw.center[0] = local[0] * draw.world[0] +
                                 local[1] * draw.world[4] +
                                 local[2] * draw.world[8] + draw.world[12];
                draw.center[1] = local[0] * draw.world[1] +
                                 local[1] * draw.world[5] +
                                 local[2] * draw.world[9] + draw.world[13];
                draw.center[2] = local[0] * draw.world[2] +
                                 local[1] * draw.world[6] +
                                 local[2] * draw.world[10] + draw.world[14];
                const float sx = std::sqrt(draw.world[0] * draw.world[0] +
                                           draw.world[1] * draw.world[1] +
                                           draw.world[2] * draw.world[2]);
                const float sy = std::sqrt(draw.world[4] * draw.world[4] +
                                           draw.world[5] * draw.world[5] +
                                           draw.world[6] * draw.world[6]);
                const float sz = std::sqrt(draw.world[8] * draw.world[8] +
                                           draw.world[9] * draw.world[9] +
                                           draw.world[10] * draw.world[10]);
                const float scale = (std::max)(sx, (std::max)(sy, sz));
                draw.radius = visual.sphere_radius * (scale > 0.0f ? scale : 1.0f);
            }
            for (int c = 0; c < 3; ++c)
                draw.center[c] = visual.sphere_center[c];
            draw.radius = visual.sphere_radius;
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
            if (!skip && IsCutout(draw.surface))
                ++m_stats.drawing_alpha_test;
            if (draw.traceSlot != UINT8_MAX)
            {
                const auto* source = requestedDescriptor == kNullDescriptor
                    ? nullptr : &textures[requestedDescriptor - 1];
                const auto byteOffset = uint64_t{draw.vbase} * draw.vertexView.StrideInBytes;
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

        m_loaded = true;
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

bool GameDx12StaticScene12::RecordNativeFrame(
    ID3D12GraphicsCommandList* list, uint32_t frameSlot,
    const DirectX::XMFLOAT4X4& logicalViewProjection,
    const float cameraPosition[3], const gameaux12::Frame& extras,
    FrameStats12& stats, AuxStats& auxStats, std::string& reason)
{
    stats = FrameStats12{};
    auxStats = AuxStats{};
    if (list == nullptr)
        return Fail(reason, "static scene null command list");
    if (!m_loaded || !m_uploadConfirmed || m_gpuUnknown)
        return Fail(reason, "static scene is not available");
    if (frameSlot >= kFrameConstantsSlots)
        return Fail(reason, "static scene frame slot out of range");
    if (!FiniteMatrix(logicalViewProjection) || cameraPosition == nullptr)
        return Fail(reason, "static scene camera is not finite");

    const auto cullStart = Clock::now();
    const DirectX::XMFLOAT4X4 shaderViewProjection =
        Transpose(logicalViewProjection);
    auto* constants = static_cast<uint8_t*>(m_frameConstantsMapped) +
                      static_cast<uint64_t>(frameSlot) * kFrameConstantsSize;
    std::memcpy(constants, &shaderViewProjection, sizeof(shaderViewProjection));
    const float camera[4] = {cameraPosition[0], cameraPosition[1],
                             cameraPosition[2], 1.0f};
    std::memcpy(constants + 64, camera, sizeof(camera));

    FrustumPlane planes[6] = {};
    const bool cull = !SceneCullDisabled() && BuildFrustum(logicalViewProjection, planes);
    stats.total = static_cast<uint32_t>(m_draws.size());
    stats.cull_ms = Ms(cullStart);

    list->SetGraphicsRootSignature(m_rootSignature.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12DescriptorHeap* heaps[] = {m_textureHeap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootConstantBufferView(
        0, m_frameConstants->GetGPUVirtualAddress() +
               static_cast<uint64_t>(frameSlot) * kFrameConstantsSize);

    const uint32_t viewMode = SceneViewMode();
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
    const auto bindMaterial = [&](uint32_t descriptor, uint32_t id, SurfaceMode surface,
                                  bool wallmarkClamp)
    {
        auto gpu = m_textureHeap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(descriptor) * m_textureDescriptorSize;
        list->SetGraphicsRootDescriptorTable(1, gpu);
        uint32_t mode = descriptor == kNullDescriptor ? 0u : kSampleBase;
        if (descriptor != kNullDescriptor && IsCutout(surface) && !alphaTestDisabled)
            mode |= kClipAlpha;
        if (IsOverlay(surface))
            mode |= kUnlit;
        if (IsBlended(surface))
            mode |= kOutputAlpha;
        if (wallmarkClamp)
            mode |= kWallmarkClamp;
        const uint32_t material[] = {mode, id, viewMode, std::bit_cast<uint32_t>(threshold)};
        list->SetGraphicsRoot32BitConstants(2, 4, material, 0);
    };

    const float identity[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, 1.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 1.0f, 0.0f,
                                0.0f, 0.0f, 0.0f, 1.0f};
    bool worldIdentity = false;
    const auto drawStatic = [&](const DrawSource12& draw)
    {
        const bool classic = draw.layout == 1;
        bindPipeline(draw.surface, classic ? 0u : 1u,
                     classic ? m_pipelineState.Get() : m_pipelineUv24.Get());
        if (draw.layout == 3 || !worldIdentity)
        {
            worldIdentity = draw.layout != 3;
            list->SetGraphicsRoot32BitConstants(3, 16, worldIdentity ? identity : draw.world, 0);
        }
        const uint32_t geometry[] = {draw.layout == 3 ? 1u : 0u,
            std::bit_cast<uint32_t>(IsOverlay(draw.surface) ? kWallmarkClipShift : 0.0f)};
        list->SetGraphicsRoot32BitConstants(5, 2, geometry, 0);
        bindMaterial(draw.descriptor, draw.visualId, draw.surface, draw.wallmarkClamp);
        list->IASetVertexBuffers(0, 1, &draw.vertexView);
        list->IASetIndexBuffer(&draw.indexView);
        list->DrawIndexedInstanced(draw.icount, 1, 0, 0, 0);
        traceStaticDraw(draw, VisualTraceState::Submitted);
        CountSurfaceDraw(stats.surfaces, draw.surface);
        ++stats.draws;
        if (dumpDraws)
            Msg("ph_cmp: dx12draw id=%u variant=%u state=drawn shader_id=%u srv=%u surface='%s'",
                draw.visualId, draw.layout, draw.shaderId, draw.descriptor,
                gameaux12::SurfaceModeName(draw.surface));
    };
    const auto drawMesh = [&](const AuxMesh& mesh, uint32_t id, SurfaceMode surface,
                              uint32_t offset, uint32_t count, ID3D12PipelineState* regular)
    {
        bindPipeline(surface, 2, regular);
        bindMaterial(mesh.descriptor, id, surface, mesh.wallmarkClamp);
        list->SetGraphicsRoot32BitConstant(3, offset, 0);
        const uint32_t geometry[] = {0u,
            std::bit_cast<uint32_t>(IsOverlay(surface) ? kWallmarkClipShift : 0.0f)};
        list->SetGraphicsRoot32BitConstants(5, 2, geometry, 0);
        worldIdentity = false;
        list->IASetVertexBuffers(0, 1, &mesh.vb);
        list->IASetIndexBuffer(&mesh.ib);
        list->DrawIndexedInstanced(mesh.indices, count, 0, 0, 0);
        CountSurfaceDraw(auxStats.surfaces, surface);
    };
    const auto drawObjectPart = [&](const AuxMesh& mesh, const gameaux12::ObjectDraw& object)
    {
        drawMesh(mesh, object.id, mesh.surface, object.poseOffset, 1, m_pipelineSkeletal.Get());
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
            if (cull && !SphereInside(planes, draw.center, draw.radius))
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
                deferred.push_back({&draw, nullptr, nullptr, draw.surface, viewDepth(draw.center)});
                continue;
            }
            drawStatic(draw);
        }
    }

    if (extras.session == LevelSession() && extras.epoch == LevelEpoch())
    {
        // NativeFrame::Begin уже дождался fence выбранного слота.
        const uint64_t slotOffset = frameSlot * kTransformSlotBytes;
        auto* destination = static_cast<uint8_t*>(m_frameTransformsMapped) + slotOffset;
        if (extras.poses.size() > gameaux12::kMaxFrameTransforms)
            return Fail(reason, "skeletal poses exceed frame transform capacity");
        uint32_t next = static_cast<uint32_t>(extras.poses.size());
        if (next != 0)
            std::memcpy(destination, extras.poses.data(),
                        next * sizeof(DirectX::XMFLOAT4X4));
        list->SetGraphicsRootShaderResourceView(4,
            m_frameTransforms->GetGPUVirtualAddress() + slotOffset);

        for (const auto& object : extras.objects)
        {
            if (object.boneCount == 0 || object.boneCount > gameaux12::kMaxBones ||
                object.poseOffset > next || object.boneCount > next - object.poseOffset ||
                object.firstMesh > extras.objectMeshes.size() ||
                object.meshCount > extras.objectMeshes.size() - object.firstMesh)
                continue;
            if (cull && !SphereInside(planes, object.center, object.radius))
            {
                ++(object.skinned ? auxStats.animatedCulled : auxStats.rigidCulled);
                continue;
            }
            ++(object.skinned ? auxStats.animatedVisible : auxStats.rigidVisible);
            bool submitted = false;
            for (const uint32_t handle : std::span(extras.objectMeshes).subspan(
                     object.firstMesh, object.meshCount))
            {
                if (handle >= m_auxMeshes.size())
                    continue;
                const auto& mesh = m_auxMeshes[handle];
                if (mesh.skinned != object.skinned || mesh.requiredBones > object.boneCount)
                    continue;
                if (IsOverlay(mesh.surface) || IsBlended(mesh.surface))
                    deferred.push_back({nullptr, &mesh, &object, mesh.surface, viewDepth(object.center)});
                else
                    drawObjectPart(mesh, object);
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
                std::memcpy(destination + next * sizeof(transform), &transform, sizeof(transform));
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

    // Сначала непрозрачные накладки, затем blended по глубине от дальних к ближним.
    // Равные глубины сохраняют исходный порядок частей.
    std::stable_sort(deferred.begin(), deferred.end(), [](const DeferredDraw& left,
                                                         const DeferredDraw& right)
    {
        const bool leftBlended = IsBlended(left.surface);
        const bool rightBlended = IsBlended(right.surface);
        if (leftBlended != rightBlended)
            return !leftBlended;
        return leftBlended && left.depth > right.depth;
    });
    for (const auto& entry : deferred)
    {
        if (entry.draw != nullptr)
            drawStatic(*entry.draw);
        else
            drawObjectPart(*entry.mesh, *entry.object);
    }
    stats.record_ms = Ms(recordStart);
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
    m_textureHeap.Reset();
    m_pipelineUv24.Reset();
    m_pipelineSkeletal.Reset();
    m_pipelineDetail.Reset();
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

    const auto resolve = [&](std::string_view shader, std::string_view textures)
    {
        GameDx12StaticMaterial12 material;
        material.surface = gameaux12::ClassifySurface(shader);
        material.wallmarkClamp = gameaux12::UsesWallmarkClamp(shader);
        if (material.surface != SurfaceMode::Unsupported)
        {
            const std::string texture = gameaux12::CanonicalBaseTextureName(textures);
            if (!texture.empty())
            {
                const auto [found, inserted] = textureIndex.emplace(
                    texture, static_cast<uint32_t>(uniqueTextureNames.size()));
                if (inserted)
                    uniqueTextureNames.push_back(texture);
                material.textureIndex = found->second;
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
