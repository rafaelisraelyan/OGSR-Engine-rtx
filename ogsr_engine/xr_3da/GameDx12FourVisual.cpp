#include "stdafx.h"
#include "GameDx12FourVisual.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12OffscreenPass.h"
#include "GameDx12J2ABatch.h"
#include "GameDx12J2APreparation.h"
#include "GameDx12NativeFrame.h"
#include "GameDx12M23Fault.h"
#include "dx12j2bbatch.h"
#include "../Layers/xrRender/xrRender_console.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
constexpr uint32_t kVertexStride = 32;
constexpr uint64_t kConstantsBytes =
    dx12object12::kObjectConstantsOffset +
    static_cast<uint64_t>(GameDx12FourVisual12::kSceneCapacity) *
        dx12object12::kObjectConstantsSize;

// Process-lifetime quarantine. Raw pointers only: nothing here is released and
// no heap allocation happens on the emergency path.
struct FourVisualQuarantine12
{
    bool occupied = false;
    IUnknown* slots[GameDx12FourVisual12::kSceneCapacity][4] = {};
    IUnknown* textures[GameDx12FourVisual12::kSceneCapacity + 1][2] = {};
    IUnknown* textureHeap = nullptr;
    IUnknown* constants = nullptr;
    IUnknown* rootSignature = nullptr;
    IUnknown* pipelineState = nullptr;
    // Captured owner objects of an unknown submission. They must not be
    // released by the M23 destructor while the last-use fence is unconfirmed.
    IUnknown* passFence = nullptr;
    IUnknown* passDevice = nullptr;
};

FourVisualQuarantine12 g_fourVisualQuarantine;
uint64_t g_quarantineCalls = 0;
uint64_t g_quarantineOccupiedAtEntry = 0;
uint64_t g_quarantineDetachedResources = 0;

template <typename T>
void DetachToQuarantine(IUnknown*& destination, M23Ptr12<T>& object) noexcept
{
    if (object == nullptr)
        return;
    destination = object.Get();
    object.Detach();
    ++g_quarantineDetachedResources;
}

template <typename T>
void DetachAndDrop(M23Ptr12<T>& object) noexcept
{
    if (object == nullptr)
        return;
    object.Detach();
    ++g_quarantineDetachedResources;
}

// Exception-safe owner command-list recording guard. It is armed immediately
// after a successful Reset and stays armed until Close succeeds. Any ordinary
// return or C++ exception runs the noexcept destructor, which closes the list
// or terminally poisons it. It performs no allocation and does not touch
// std::string diagnostics, so persistent OOM cannot bypass it.
class M23RecordingGuard12
{
public:
    M23RecordingGuard12(ID3D12GraphicsCommandList* list, bool& poisoned,
                        bool& closed, const char* closeFaultName) noexcept
        : m_list(list), m_poisoned(poisoned), m_closed(closed),
          m_closeFaultName(closeFaultName)
    {
        m_closed = false;
    }
    M23RecordingGuard12(const M23RecordingGuard12&) = delete;
    M23RecordingGuard12& operator=(const M23RecordingGuard12&) = delete;
    ~M23RecordingGuard12() noexcept { Abandon(); }

    // Returns true only if Close succeeded. On failure the shared list is
    // terminally poisoned and must never be reset or reused.
    bool Close() noexcept
    {
        if (!m_armed)
            return false;
        m_armed = false;
        if (m_closeFaultName != nullptr &&
            m23fault12::Is(m_closeFaultName))
        {
            m_poisoned = true;
            m_closed = false;
            return false;
        }
        if (m_list == nullptr || FAILED(m_list->Close()))
        {
            m_poisoned = true;
            m_closed = false;
            return false;
        }
        m_closed = true;
        return true;
    }

    // Discards an unsubmitted recording. Safe only before ExecuteCommandLists.
    bool Abandon() noexcept
    {
        if (!m_armed)
            return m_closed;
        m_armed = false;
        if (m_closeFaultName != nullptr &&
            m23fault12::Is(m_closeFaultName))
        {
            m_poisoned = true;
            m_closed = false;
            return false;
        }
        if (m_list == nullptr || FAILED(m_list->Close()))
        {
            m_poisoned = true;
            m_closed = false;
            return false;
        }
        m_closed = true;
        return true;
    }

    bool Armed() const noexcept { return m_armed; }

private:
    ID3D12GraphicsCommandList* m_list = nullptr;
    bool& m_poisoned;
    bool& m_closed;
    const char* m_closeFaultName = nullptr;
    bool m_armed = true;
};

bool FiniteMatrix(const DirectX::XMFLOAT4X4& matrix) noexcept
{
    for (uint32_t row = 0; row < 4; ++row)
    {
        for (uint32_t column = 0; column < 4; ++column)
        {
            if (!std::isfinite(matrix.m[row][column]))
                return false;
        }
    }
    return true;
}

DirectX::XMFLOAT4X4 Transpose(const DirectX::XMFLOAT4X4& matrix) noexcept
{
    DirectX::XMFLOAT4X4 result;
    DirectX::XMStoreFloat4x4(
        &result, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&matrix)));
    return result;
}

D3D12_RESOURCE_DESC BufferDesc(uint64_t size) noexcept
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

uint64_t NextGeneration(uint64_t session, uint64_t epoch,
                        uint64_t previous) noexcept
{
    uint64_t value = (session * 0x9E3779B97F4A7C15ull) ^
                     (epoch * 0xBF58476D1CE4E5B9ull) ^
                     0x4D32334D4F555256ull;
    if (value == 0 || value == previous)
        ++value;
    if (value == 0)
        value = 1;
    return value;
}

uint32_t PopCount(uint32_t value) noexcept
{
    uint32_t count = 0;
    while (value != 0)
    {
        count += value & 1u;
        value >>= 1u;
    }
    return count;
}

bool CompileShader(const char* source, const char* entryPoint,
                   const char* target, ID3D10Blob** outBlob,
                   std::string& reason)
{
    ComPtr<ID3D10Blob> error;
    const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr,
                                  nullptr, nullptr, entryPoint, target, 0, 0,
                                  outBlob, &error);
    if (FAILED(hr))
    {
        if (error != nullptr)
            reason = static_cast<const char*>(error->GetBufferPointer());
        else
            reason = "shader compile failed";
        return false;
    }
    return true;
}

constexpr const char* kM23VertexShader = R"(
struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

cbuffer FrameCB : register(b0)
{
    float4x4 ViewProjection;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 World;
    float4 Tint;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(mul(float4(input.position, 1.0f), World),
                          ViewProjection);
    output.normal = input.normal;
    output.uv = input.uv;
    return output;
}
)";

// Diagnostic flat identity colour. This is an oracle buffer, not a material:
// every visual gets a unique tint so the combined readback can prove which
// real GPU draw produced which pixels.
constexpr const char* kM23PixelShader = R"(
cbuffer ObjectCB : register(b1)
{
    float4x4 World;
    float4 Tint;
};

float4 main() : SV_Target
{
    return Tint;
}
)";

constexpr const char* kScenePixelShader = R"(
Texture2D BaseColor : register(t0);
SamplerState BaseSampler : register(s0);
cbuffer MaterialCB : register(b2)
{
    uint TextureMode;
    uint MaterialId;
    uint ViewMode;
    float AlphaThreshold;
};

struct PSInput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target
{
    const float3 n = input.normal * rsqrt(max(dot(input.normal, input.normal), 0.00001));
    float4 baseColor = float4(1, 0, 1, 1);
    if (TextureMode != 0)
        baseColor = BaseColor.Sample(BaseSampler, input.uv);
    if (TextureMode == 2)
        clip(baseColor.a - AlphaThreshold);
    if (ViewMode == 1)
        return float4(frac((MaterialId + 1) * float3(0.618, 0.381, 0.173)), 1);
    if (ViewMode == 2)
        return float4(frac(input.uv), saturate(n.y * 0.5 + 0.5), 1);
    if (ViewMode == 3)
        return TextureMode == 0 ? float4(1, 0, 1, 1) : float4(0, 0, 0, 1);
    const float light = 0.48 + 0.52 * saturate(dot(n, normalize(float3(-0.4, 0.8, 0.3))));
    const float3 diffuseLinear = saturate(baseColor.rgb * light);
    return float4(max(min(12.92 * diffuseLinear, 0.04045),
                      1.055 * pow(diffuseLinear, 1.0 / 2.4) - 0.055), 1);
}
)";

std::wstring JoinPath(const wchar_t* directory, const wchar_t* name)
{
    std::wstring path(directory != nullptr ? directory : L"");
    if (!path.empty())
    {
        const wchar_t last = path[path.size() - 1];
        if (last != L'\\' && last != L'/')
            path += L'\\';
    }
    path += name;
    return path;
}
} // namespace

GameDx12FourVisual12::~GameDx12FourVisual12()
{
    // Any still-owned GPU object at destruction may have unknown completion.
    // Quarantine instead of releasing; the OS reclaims at process exit.
    Shutdown(false);
}

bool GameDx12FourVisual12::Fail(std::string& reason, const char* text)
{
#ifdef OGSR_M23_FAULT_INJECTION
    if (m23fault12::ErrorOom())
        throw std::bad_alloc();
#endif
    m_lastError = text != nullptr ? text : "M23 four-visual failure";
    reason = m_lastError;
    return false;
}

bool GameDx12FourVisual12::Fail(std::string& reason,
                                const std::string& text)
{
#ifdef OGSR_M23_FAULT_INJECTION
    if (m23fault12::ErrorOom())
        throw std::bad_alloc();
#endif
    m_lastError = text;
    reason = m_lastError;
    return false;
}

bool GameDx12FourVisual12::CreatePipeline(ID3D12Device* device,
                                          std::string& reason)
{
    if (device == nullptr)
        return Fail(reason, "M23 invalid device for pipeline");

    const char* cullEnv = std::getenv("OGSR_DX12_GAME_M23_CULL");
    if (cullEnv != nullptr && _stricmp(cullEnv, "none") == 0)
        m_cullMode = D3D12_CULL_MODE_NONE;
    else
        m_cullMode = D3D12_CULL_MODE_BACK;

    D3D12_ROOT_PARAMETER parameters[4] = {};
    for (uint32_t i = 0; i < 2; ++i)
    {
        parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[i].Descriptor.ShaderRegister = i;
        parameters[i].Descriptor.RegisterSpace = 0;
        parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    D3D12_DESCRIPTOR_RANGE textureRange = {};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = 1;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &textureRange;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[3].Constants.ShaderRegister = 2;
    parameters[3].Constants.Num32BitValues = 4;
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootDesc.NumParameters = m_textured ? 4u : 2u;
    rootDesc.pParameters = parameters;
    rootDesc.NumStaticSamplers = m_textured ? 1u : 0u;
    rootDesc.pStaticSamplers = m_textured ? &sampler : nullptr;
    rootDesc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3D10Blob> signature;
    ComPtr<ID3D10Blob> error;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr))
    {
        if (error != nullptr)
            reason = static_cast<const char*>(error->GetBufferPointer());
        return Fail(reason, "M23 root signature serialization failed");
    }
    hr = device->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(m_rootSignature.GetAddressOf()));
    if (FAILED(hr))
        return Fail(reason, "M23 root signature creation failed");
    m23probe12::MarkCreated();

    ComPtr<ID3D10Blob> vertexShader;
    ComPtr<ID3D10Blob> pixelShader;
    std::string compileReason;
    if (!CompileShader(kM23VertexShader, "main", "vs_5_0", &vertexShader,
                       compileReason) ||
        !CompileShader(m_textured ? kScenePixelShader : kM23PixelShader,
                       "main", "ps_5_0", &pixelShader,
                       compileReason))
        return Fail(reason, "M23 shader compile failed: " + compileReason);

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_rootSignature.Get();
    desc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
    desc.VS.BytecodeLength = vertexShader->GetBufferSize();
    desc.PS.pShaderBytecode = pixelShader->GetBufferPointer();
    desc.PS.BytecodeLength = pixelShader->GetBufferSize();
    desc.InputLayout.pInputElementDescs = inputLayout;
    desc.InputLayout.NumElements = 3;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.NumRenderTargets = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode =
        static_cast<D3D12_CULL_MODE>(m_cullMode);
    desc.RasterizerState.FrontCounterClockwise = FALSE;
    desc.RasterizerState.DepthClipEnable = TRUE;

    hr = device->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(m_pipelineState.GetAddressOf()));
    if (FAILED(hr))
    {
        Msg("ph_dx12_m23: CreateGraphicsPipelineState hr=0x%08X",
            static_cast<unsigned>(hr));
        return Fail(reason, "M23 graphics pipeline creation failed");
    }
    m23probe12::MarkCreated();

    Msg("ph_dx12_m23: pipeline ready rt=R8G8B8A8_UNORM depth=D32_FLOAT "
        "depth_func=LESS depth_write=1 cull=%s front_ccw=0 stride=%u",
        m_cullMode == D3D12_CULL_MODE_NONE ? "none" : "back",
        kVertexStride);
    return true;
}

bool GameDx12FourVisual12::CreateConstants(ID3D12Device* device,
                                           std::string& reason)
{
    if (device == nullptr)
        return Fail(reason, "M23 invalid device for constants");

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    const D3D12_RESOURCE_DESC desc = BufferDesc(
        kConstantsBytes * (1 + GameDx12NativeFrame12::kFrameCount));
    HRESULT hr = device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(m_constants.GetAddressOf()));
    if (FAILED(hr))
        return Fail(reason, "M23 constant buffer creation failed");
    m23probe12::MarkCreated();

    D3D12_RANGE noRead = {0, 0};
    hr = m_constants->Map(0, &noRead, &m_constantsMapped);
    if (FAILED(hr) || m_constantsMapped == nullptr)
    {
        m_constantsMapped = nullptr;
        m_constants.Reset();
        return Fail(reason, "M23 constant buffer map failed");
    }
    std::memset(m_constantsMapped, 0, static_cast<size_t>(kConstantsBytes));
    m_frameCBVA = m_constants->GetGPUVirtualAddress();
    for (uint32_t slot = 0; slot < m_sourceCount; ++slot)
    {
        m_objectCBVA[slot] =
            m_frameCBVA + dx12object12::ObjectConstantsOffset(slot);
    }
    return true;
}

bool GameDx12FourVisual12::CreateGeometryBuffers(
    ID3D12Device* device, ID3D12GraphicsCommandList* list,
    std::string& reason)
{
    if (device == nullptr || list == nullptr)
        return Fail(reason, "M23 invalid device/list for geometry");

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_BARRIER barriers[kSceneCapacity * 2] = {};
    uint32_t barrierCount = 0;
    uint64_t vertexBytesTotal = 0;
    uint64_t indexBytesTotal = 0;

    for (uint32_t slot = 0; slot < m_sourceCount; ++slot)
    {
        SlotGpu12& gpu = m_slots[slot];
        const uint64_t vertexBytes =
            static_cast<uint64_t>(gpu.asset.vertices.size()) *
            sizeof(XrayVertex12);
        const uint64_t indexBytes =
            static_cast<uint64_t>(gpu.asset.indices.size()) *
            sizeof(uint16_t);
        if (vertexBytes == 0 || indexBytes == 0)
            return Fail(reason, "M23 empty geometry asset");
        if (slot == 2)
        {
            // Test-only faults after two complete slots have been recorded.
            if (m23fault12::Is("upload-oom"))
                throw std::bad_alloc();
            if (m23fault12::Is("upload-error"))
                return Fail(reason, "M23 injected upload failure");
        }
        vertexBytesTotal += vertexBytes;
        indexBytesTotal += indexBytes;

        const D3D12_RESOURCE_DESC vertexDesc = BufferDesc(vertexBytes);
        const D3D12_RESOURCE_DESC indexDesc = BufferDesc(indexBytes);

        HRESULT hr = device->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(gpu.vertexBuffer.GetAddressOf()));
        if (FAILED(hr))
            return Fail(reason, "M23 vertex buffer creation failed");
        m23probe12::MarkCreated();
        hr = device->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &indexDesc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(gpu.indexBuffer.GetAddressOf()));
        if (FAILED(hr))
            return Fail(reason, "M23 index buffer creation failed");
        m23probe12::MarkCreated();

        hr = device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(gpu.uploadVertex.GetAddressOf()));
        if (FAILED(hr))
            return Fail(reason, "M23 upload vertex creation failed");
        m23probe12::MarkCreated();
        hr = device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &indexDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(gpu.uploadIndex.GetAddressOf()));
        if (FAILED(hr))
            return Fail(reason, "M23 upload index creation failed");
        m23probe12::MarkCreated();

        D3D12_RANGE noRead = {0, 0};
        void* mapped = nullptr;
        hr = gpu.uploadVertex->Map(0, &noRead, &mapped);
        if (FAILED(hr) || mapped == nullptr)
            return Fail(reason, "M23 upload vertex map failed");
        std::memcpy(mapped, gpu.asset.vertices.data(),
                    static_cast<size_t>(vertexBytes));
        gpu.uploadVertex->Unmap(0, nullptr);

        mapped = nullptr;
        hr = gpu.uploadIndex->Map(0, &noRead, &mapped);
        if (FAILED(hr) || mapped == nullptr)
            return Fail(reason, "M23 upload index map failed");
        std::memcpy(mapped, gpu.asset.indices.data(),
                    static_cast<size_t>(indexBytes));
        gpu.uploadIndex->Unmap(0, nullptr);

        D3D12_RESOURCE_BARRIER toCopy[2] = {};
        toCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy[0].Transition.pResource = gpu.vertexBuffer.Get();
        toCopy[0].Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        toCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        toCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy[1].Transition.pResource = gpu.indexBuffer.Get();
        toCopy[1].Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        toCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(2, toCopy);

        list->CopyBufferRegion(gpu.vertexBuffer.Get(), 0,
                               gpu.uploadVertex.Get(), 0, vertexBytes);
        list->CopyBufferRegion(gpu.indexBuffer.Get(), 0,
                               gpu.uploadIndex.Get(), 0, indexBytes);

        D3D12_RESOURCE_BARRIER& vbBarrier = barriers[barrierCount++];        vbBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        vbBarrier.Transition.pResource = gpu.vertexBuffer.Get();
        vbBarrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        vbBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        vbBarrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

        D3D12_RESOURCE_BARRIER& ibBarrier = barriers[barrierCount++];
        ibBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ibBarrier.Transition.pResource = gpu.indexBuffer.Get();
        ibBarrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ibBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        ibBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;

        gpu.vertexView.BufferLocation =
            gpu.vertexBuffer->GetGPUVirtualAddress();
        gpu.vertexView.SizeInBytes = static_cast<UINT>(vertexBytes);
        gpu.vertexView.StrideInBytes = kVertexStride;
        gpu.indexView.BufferLocation =
            gpu.indexBuffer->GetGPUVirtualAddress();
        gpu.indexView.SizeInBytes = static_cast<UINT>(indexBytes);
        gpu.indexView.Format = DXGI_FORMAT_R16_UINT;
    }
    if (barrierCount != m_sourceCount * 2)
        return Fail(reason, "M23 geometry barrier count mismatch");
    list->ResourceBarrier(barrierCount, barriers);

    // Distinct, non-zero GPU handles are a hard part of the contribution
    // contract: one asset cannot silently alias another.
    for (uint32_t a = 0; a < m_sourceCount; ++a)
    {
        if (m_slots[a].vertexView.BufferLocation == 0 ||
            m_slots[a].indexView.BufferLocation == 0)
            return Fail(reason, "M23 geometry handle is null");
        for (uint32_t b = a + 1; b < m_sourceCount; ++b)
        {
            if (m_slots[a].vertexView.BufferLocation ==
                    m_slots[b].vertexView.BufferLocation ||
                m_slots[a].indexView.BufferLocation ==
                    m_slots[b].indexView.BufferLocation)
                return Fail(reason, "M23 geometry handle aliases another slot");
        }
        Msg("ph_dx12_m23: slot=%u visual=%u vertex_count=%u index_count=%u "
            "vb=0x%llX vb_bytes=%u stride=%u ib=0x%llX ib_bytes=%u "
            "ib_format=R16_UINT generation=%llu",
            a, m_slots[a].visualId, m_slots[a].vertexCount,
            m_slots[a].indexCount,
            static_cast<unsigned long long>(
                m_slots[a].vertexView.BufferLocation),
            m_slots[a].vertexView.SizeInBytes,
            m_slots[a].vertexView.StrideInBytes,
            static_cast<unsigned long long>(
                m_slots[a].indexView.BufferLocation),
            m_slots[a].indexView.SizeInBytes,
            static_cast<unsigned long long>(m_slots[a].generation));
    }
    Msg("ph_dx12_m23: geometry handles distinct=1 vertex_bytes=%llu "
        "index_bytes=%llu",
        static_cast<unsigned long long>(vertexBytesTotal),
        static_cast<unsigned long long>(indexBytesTotal));
    return true;
}

bool GameDx12FourVisual12::LoadFromBatch(
    const GameDx12FourVisualOwner12& owner, const char* levelName,
    const j2bbatch12::Identity12& live, j2bbatch12::BatchResult12&& batch,
    std::string& reason)
{
    try
    {
        return LoadFromBatchImpl(owner, levelName, live, batch, reason);
    }
    catch (const std::bad_alloc&)
    {
        return HandleLoadException(reason);
    }
    catch (const std::length_error&)
    {
        return HandleLoadException(reason);
    }
    catch (...)
    {
        return HandleLoadException(reason);
    }
}

bool GameDx12FourVisual12::HandleLoadException(std::string& reason) noexcept
{
    try
    {
        Msg("ph_dx12_m23: upload exception boundary recording_closed=%d "
            "poisoned=%d",
            m_lastRecordingClosed ? 1 : 0, m_listPoisoned ? 1 : 0);
    }
    catch (...)
    {
    }
    if (m_gpuUnknown || m_quarantined || m_listPoisoned ||
        (m_uploadSubmitted && !m_uploadConfirmed))
        QuarantineGpu();
    else
        ReleaseGpu();
    InvalidateStaging();
    m_loaded = false;
    reason.clear();
    return false;
}

bool GameDx12FourVisual12::CanLoad(
    const GameDx12FourVisualOwner12& owner, const char* levelName,
    std::string& reason)
{
    m_lastError.clear();
    if (owner.device == nullptr || owner.queue == nullptr ||
        owner.fence == nullptr || owner.fenceEvent == nullptr ||
        owner.allocator == nullptr || owner.list == nullptr ||
        owner.fenceValue == nullptr)
        return Fail(reason, "M23 invalid owner context");
    if (levelName == nullptr || levelName[0] == '\0')
        return Fail(reason, "M23 missing level name");
    if (m_gpuUnknown)
        return Fail(reason, "M23 registry is quarantined");
    if (m_listPoisoned)
        return Fail(reason, "M23 command list is poisoned");
    if (m_loaded || m_slots[0].vertexBuffer != nullptr ||
        m_uploadSubmitted)
        return Fail(reason, "M23 registry is already loaded");
    if (*owner.fenceValue == std::numeric_limits<uint64_t>::max())
        return Fail(reason, "M23 owner fence overflow");
    return true;
}

bool GameDx12FourVisual12::LoadFromBatchImpl(
    const GameDx12FourVisualOwner12& owner, const char* levelName,
    const j2bbatch12::Identity12& live, j2bbatch12::BatchResult12& batch,
    std::string& reason)
{
    static_assert(kSlotCount == j2bbatch12::kTargetVisualCount);
    if (!CanLoad(owner, levelName, reason))
        return false;
    if (!fourvisual12::ValidatePlan12(fourvisual12::kPlan, kSlotCount,
                                      reason))
        return Fail(reason, "M23 plan rejected");

    const auto status = j2bbatch12::AcceptBatch12(batch, live);
    if (status != j2bbatch12::AcceptStatus12::Accepted)
    {
        char message[80] = {};
        std::snprintf(message, sizeof(message), "M23 CPU batch rejected status=%u",
                      static_cast<unsigned>(status));
        return Fail(reason, message);
    }

    for (uint32_t slot = 0; slot < kSlotCount; ++slot)
    {
        const fourvisual12::SlotPlan12& plan = fourvisual12::kPlan[slot];
        const j2bbatch12::Slot12& cpu = batch.slots[slot];
        if (cpu.source.visualId != plan.visualId)
            return Fail(reason, "M23 CPU batch visual does not match plan");
        if (cpu.vertexCount != plan.expectedVertexCount ||
            cpu.indexCount != plan.expectedIndexCount)
        {
            char message[192] = {};
            std::snprintf(message, sizeof(message),
                          "M23 visual %u count mismatch verts=%u/%u "
                          "indices=%u/%u",
                          plan.visualId, cpu.vertexCount,
                          plan.expectedVertexCount, cpu.indexCount,
                          plan.expectedIndexCount);
            return Fail(reason, message);
        }
    }

    const uint64_t levelSession = live.levelSession;
    const uint64_t levelEpoch = live.levelEpoch;
    Msg("ph_dx12_m23: accepted CPU batch sector=%u root=%u hash=%016llX",
        batch.sectorId, batch.rootVisualId,
        static_cast<unsigned long long>(batch.stableHash));
    m_generation = NextGeneration(levelSession, levelEpoch, 0);
    for (uint32_t slot = 0; slot < kSlotCount; ++slot)
    {
        j2bbatch12::Slot12& cpu = batch.slots[slot];
        SlotGpu12& gpu = m_slots[slot];
        gpu.asset = std::move(cpu.asset);
        gpu.visualId = cpu.source.visualId;
        gpu.vertexCount = cpu.vertexCount;
        gpu.indexCount = cpu.indexCount;
        gpu.generation = m_generation;
        Msg("ph_dx12_m23: asset slot=%u visual=%u verts=%u indices=%u "
            "min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f) radius=%.3f",
            slot, gpu.visualId, gpu.vertexCount, gpu.indexCount,
            gpu.asset.minimum.x, gpu.asset.minimum.y,
            gpu.asset.minimum.z, gpu.asset.maximum.x,
            gpu.asset.maximum.y, gpu.asset.maximum.z,
            gpu.asset.sphere.radius);
    }

    m_sourceCount = kSlotCount;
    return UploadGeometry(owner, levelName, live, reason);
}

bool GameDx12FourVisual12::LoadScene(
    const GameDx12FourVisualOwner12& owner, const char* levelName,
    const j2bbatch12::Identity12& live, j2bbatch12::SceneBatch12&& scene,
    const std::vector<GameDx12SceneTexture12>* textures, std::string& reason)
{
    try
    {
        if (!CanLoad(owner, levelName, reason))
            return false;
        if (live.levelSession == 0 || live.levelEpoch == 0 || scene.identity != live ||
            scene.error.code != j2bbatch12::Error12::None ||
            scene.sources.empty() || scene.sources.size() > kSceneCapacity ||
            scene.instances.empty() || scene.instances.size() > kSceneCapacity ||
            (textures && textures->size() > kSceneCapacity))
            return Fail(reason, "B1 invalid scene batch");
        for (const auto& instance : scene.instances)
        {
            if (instance.sourceIndex >= scene.sources.size() ||
                scene.sources[instance.sourceIndex].source.visualId != instance.identity.visualId)
                return Fail(reason, "B1 invalid scene instance source");
        }
        for (const auto& source : scene.sources)
        {
            if (!source.valid || source.source.levelSession != live.levelSession ||
                source.vertexCount == 0 || source.indexCount == 0 ||
                source.asset.vertices.size() != source.vertexCount ||
                source.asset.indices.size() != source.indexCount)
                return Fail(reason, "B1 invalid scene source");
        }

        m_generation = NextGeneration(live.levelSession, live.levelEpoch, 0);
        m_textured = textures != nullptr;
        m_sourceCount = static_cast<uint32_t>(scene.sources.size());
        for (uint32_t index = 0; index < m_sourceCount; ++index)
        {
            auto& source = scene.sources[index];
            auto& gpu = m_slots[index];
            gpu.asset = std::move(source.asset);
            gpu.visualId = source.source.visualId;
            gpu.vertexCount = source.vertexCount;
            gpu.indexCount = source.indexCount;
            gpu.generation = m_generation;
        }
        m_worldInstances = std::move(scene.instances);
        Msg("ph_dx12_b1: sources=%u instances=%zu unsupported=%u asset_bytes=%llu",
            m_sourceCount, m_worldInstances.size(), scene.unsupportedInstances,
            static_cast<unsigned long long>(scene.assetBytes));
        return UploadGeometry(owner, levelName, live, reason, textures);
    }
    catch (...)
    {
        return HandleLoadException(reason);
    }
}

bool GameDx12FourVisual12::UploadGeometry(
    const GameDx12FourVisualOwner12& owner, const char* levelName,
    const j2bbatch12::Identity12& live, std::string& reason,
    const std::vector<GameDx12SceneTexture12>* textures)
{
    const uint64_t levelSession = live.levelSession;
    const uint64_t levelEpoch = live.levelEpoch;
    if (!CreatePipeline(owner.device, reason))
    {
        ReleaseGpu();
        return false;
    }
    if (!CreateConstants(owner.device, reason))
    {
        ReleaseGpu();
        return false;
    }

    HRESULT hr = owner.allocator->Reset();
    if (FAILED(hr))
    {
        m_listPoisoned = true;
        ReleaseGpu();
        return Fail(reason, "M23 upload allocator reset failed");
    }
    hr = owner.list->Reset(owner.allocator, nullptr);
    if (FAILED(hr))
    {
        m_listPoisoned = true;
        ReleaseGpu();
        return Fail(reason, "M23 upload list reset failed");
    }

    M23RecordingGuard12 recording(owner.list, m_listPoisoned,
                                  m_lastRecordingClosed,
                                  "close-failure");
    if (!CreateGeometryBuffers(owner.device, owner.list, reason) ||
        (textures && !CreateSceneTextures(owner.device, owner.list, *textures, reason)))
    {
        const bool closed = recording.Abandon();
        Msg("ph_dx12_m23: upload recording failed list_closed=%d "
            "poisoned=%d",
            closed ? 1 : 0, m_listPoisoned ? 1 : 0);
        if (!closed || m_listPoisoned)
            QuarantineGpu();
        else
            ReleaseGpu();
        return false;
    }

    if (!recording.Close())
    {
        Msg("ph_dx12_m23: upload list close failed poisoned=1");
        QuarantineGpu();
        return Fail(reason, "M23 upload list close failed");
    }

    const uint64_t expected = *owner.fenceValue + 1;
    if (expected == 0)
    {
        ReleaseGpu();
        return Fail(reason, "M23 upload fence overflow");
    }

    m_uploadSubmitted = true;
    ID3D12CommandList* lists[] = {owner.list};
    owner.queue->ExecuteCommandLists(1, lists);

    hr = owner.queue->Signal(owner.fence, expected);
    if (FAILED(hr))
    {
        QuarantineGpu();
        return Fail(reason, "M23 upload fence signal failed");
    }
    *owner.fenceValue = expected;
    if (owner.fence->GetCompletedValue() < expected)
    {
        hr = owner.fence->SetEventOnCompletion(expected, owner.fenceEvent);
        if (FAILED(hr))
        {
            QuarantineGpu();
            return Fail(reason, "M23 upload fence event failed");
        }
        if (WaitForSingleObject(owner.fenceEvent, kFenceTimeoutMs) !=
            WAIT_OBJECT_0)
        {
            QuarantineGpu();
            return Fail(reason, "M23 upload fence wait failed");
        }
    }
    if (owner.fence->GetCompletedValue() < expected ||
        owner.device->GetDeviceRemovedReason() != S_OK)
    {
        QuarantineGpu();
        return Fail(reason, "M23 upload completion unconfirmed");
    }

    m_uploadFenceValue = expected;
    m_uploadConfirmed = true;
    m_uploadSubmitted = false;
    for (auto& texture : m_textures)
        texture.upload.Reset();
    m_loaded = true;
    m_levelSession = levelSession;
    m_levelEpoch = levelEpoch;
    Msg("ph_dx12_m23: upload complete level='%s' session=%llu epoch=%llu "
        "slots=%u upload_fence=%llu generation=%llu",
        levelName, static_cast<unsigned long long>(levelSession),
        static_cast<unsigned long long>(levelEpoch), m_sourceCount,
        static_cast<unsigned long long>(m_uploadFenceValue),
        static_cast<unsigned long long>(m_generation));
    return true;
}

bool GameDx12FourVisual12::CreateSceneTextures(
    ID3D12Device* device, ID3D12GraphicsCommandList* list,
    const std::vector<GameDx12SceneTexture12>& textures, std::string& reason)
{
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = static_cast<UINT>(textures.size() + 1);
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(m_textureHeap.GetAddressOf()))))
        return Fail(reason, "B2 descriptor heap allocation failed");
    m23probe12::MarkCreated();
    m_textureDescriptorSize = device->GetDescriptorHandleIncrementSize(heap.Type);

    auto cpu = m_textureHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_BC1_UNORM_SRGB;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(nullptr, &srv, cpu);

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    constexpr UINT64 uploadBudget = 32ull * 1024 * 1024;
    UINT64 usedUpload = 0;

    for (uint32_t index = 0; index < textures.size(); ++index)
    {
        cpu.ptr += m_textureDescriptorSize;
        const auto& source = textures[index];
        const auto& dds = source.dds;
        if (dds.data.empty())
            continue;
        const char* copyReason = nullptr;
        const bool bc3 = dds.format == DXGI_FORMAT_BC3_UNORM;
        if (!ValidateXrayTextureSource(dds, &copyReason) ||
            (!bc3 && dds.format != DXGI_FORMAT_BC1_UNORM))
            return Fail(reason, "B2 invalid compressed texture source");
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = dds.width;
        desc.Height = dds.height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = static_cast<UINT16>(dds.mipCount);
        desc.Format = dds.format;
        desc.SampleDesc.Count = 1;

        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layout(dds.mipCount);
        std::vector<UINT> rows(dds.mipCount);
        std::vector<UINT64> rowBytes(dds.mipCount);
        UINT64 bytes = 0;
        device->GetCopyableFootprints(&desc, 0, dds.mipCount, 0, layout.data(),
                                      rows.data(), rowBytes.data(), &bytes);
        if (!ValidateXrayTextureCopy(dds, layout.data(), rows.data(), rowBytes.data(),
                                     dds.mipCount, bytes, &copyReason))
            return Fail(reason, copyReason);
        if (bytes > uploadBudget - usedUpload)
        {
            Msg("ph_dx12_b2: texture fallback name='%s' reason='upload budget'", source.name.c_str());
            continue;
        }
        usedUpload += bytes;

        auto& texture = m_textures[index + 1];
        desc.Format = bc3 ? DXGI_FORMAT_BC3_TYPELESS : DXGI_FORMAT_BC1_TYPELESS;
        if (FAILED(device->CreateCommittedResource(
                &defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(texture.image.GetAddressOf()))))
            return Fail(reason, "B2 texture allocation failed");
        m23probe12::MarkCreated();
        const auto uploadDesc = BufferDesc(bytes);
        if (FAILED(device->CreateCommittedResource(
                &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(texture.upload.GetAddressOf()))))
            return Fail(reason, "B2 texture upload allocation failed");
        m23probe12::MarkCreated();
        const D3D12_RANGE noRead{};
        void* mapped = nullptr;
        if (FAILED(texture.upload->Map(0, &noRead, &mapped)))
            return Fail(reason, "B2 texture upload mapping failed");
        for (uint32_t mip = 0; mip < dds.mipCount; ++mip)
        {
            const auto& level = dds.mips[mip];
            for (UINT row = 0; row < rows[mip]; ++row)
                std::memcpy(static_cast<uint8_t*>(mapped) + layout[mip].Offset +
                                static_cast<UINT64>(row) * layout[mip].Footprint.RowPitch,
                            dds.data.data() + level.dataOffset +
                                static_cast<UINT64>(row) * level.rowPitch,
                            level.rowPitch);
        }
        texture.upload->Unmap(0, nullptr);
        for (uint32_t mip = 0; mip < dds.mipCount; ++mip)
        {
            D3D12_TEXTURE_COPY_LOCATION from{};
            from.pResource = texture.upload.Get();
            from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint = layout[mip];
            D3D12_TEXTURE_COPY_LOCATION into{};
            into.pResource = texture.image.Get();
            into.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            into.SubresourceIndex = mip;
            list->CopyTextureRegion(&into, 0, 0, 0, &from, nullptr);
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.image.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1, &barrier);

        srv.Format = bc3 ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM_SRGB;
        srv.Texture2D.MipLevels = dds.mipCount;
        device->CreateShaderResourceView(texture.image.Get(), &srv, cpu);
        for (uint32_t slot = 0; slot < m_sourceCount; ++slot)
        {
            auto& geometry = m_slots[slot];
            if (_stricmp(source.name.c_str(), geometry.asset.textureName.c_str()) != 0)
                continue;
            const bool cutout = geometry.asset.shaderName.find("aref") != std::string::npos;
            if (cutout && !source.alphaTest)
            {
                Msg("ph_dx12_b2: visual=%u material=%u texture='%s' fallback=missing_cutout_metadata",
                    geometry.visualId, slot, source.name.c_str());
                continue;
            }
            geometry.textureIndex = index + 1;
            geometry.alphaTest = cutout;
            Msg("ph_dx12_b2: visual=%u material=%u texture='%s' shader='%s' alpha_test=%d",
                geometry.visualId, slot, source.name.c_str(),
                geometry.asset.shaderName.c_str(), geometry.alphaTest ? 1 : 0);
        }
    }
    return true;
}

void GameDx12FourVisual12::InvalidateStaging()
{
    m_stagingPrepared = false;
    m_visibleCount = 0;
    for (uint32_t slot = 0; slot < kSlotCount; ++slot)
        m_staged[slot] = {};
}

bool GameDx12FourVisual12::PrepareNativeVisibility(
    const DirectX::XMFLOAT4X4& camera, NativeTimings12& timings, std::string& reason)
{
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    const uint32_t catalogGeneration = static_cast<uint32_t>(m_generation) | 1u;
    std::array<renderprep12::Object12, kSceneCapacity> objects{};
    std::array<renderprep12::Resource12, kSceneCapacity * 2 + 1> resources{};
    size_t resourceCount = 0;
    DirectX::XMFLOAT4X4 world;
    DirectX::XMStoreFloat4x4(&world, DirectX::XMMatrixIdentity());
    const renderprep12::Resource12 pipeline{0x30000, catalogGeneration};
    resources[resourceCount++] = pipeline;
    for (size_t index = 0; index < m_worldInstances.size(); ++index)
    {
        const auto& instance = m_worldInstances[index];
        const auto& source = m_slots[instance.sourceIndex];
        if (source.generation != m_generation || source.visualId != instance.identity.visualId)
            return Fail(reason, "B4 stale geometry source");
        auto& object = objects[index];
        object.id = index + 1;
        object.indexCount = source.indexCount;
        object.world = world;
        object.minimum = source.asset.minimum;
        object.maximum = source.asset.maximum;
        object.sphere = source.asset.sphere;
        object.visibility.sectorId = instance.identity.sectorId;
        object.visual = {0x10000u + instance.sourceIndex, catalogGeneration};
        object.material = {0x20000u + source.textureIndex, catalogGeneration};
        object.pso = pipeline;
        resources[resourceCount++] = object.visual;
        resources[resourceCount++] = object.material;
    }
    const renderprep12::RenderSceneSnapshot12 snapshot(
        camera, {objects.data(), m_worldInstances.size()}, m_generation);
    timings.snapshotMs = std::chrono::duration<double, std::milli>(Clock::now() - started).count();

    static const renderprep12::Mode selected = []
    {
        const char* value = std::getenv("OGSR_DX12_GAME_GR");
        if (value != nullptr && std::strcmp(value, "parallel") == 0)
            return renderprep12::Mode::Parallel;
        if (value != nullptr && std::strcmp(value, "verify") == 0)
            return renderprep12::Mode::Verify;
        return renderprep12::Mode::Serial;
    }();
    renderprep12::PreparedFrame12 prepared;
    renderprep12::VisibilityTimings12 times;
    if (!GameDx12J2A_PrepareSceneFrame(snapshot, m_generation, selected,
           {resources.data(), resourceCount}, prepared, times, timings.serialFallback, reason))
        return false;
    timings.mode = timings.serialFallback ? "serial-fallback" :
                   selected == renderprep12::Mode::Parallel ? "parallel" :
                   selected == renderprep12::Mode::Verify ? "verify" : "serial";
    timings.serialMs = times.serial.total;
    timings.parallelMs = times.parallel.total;
    if (prepared.generation != m_generation || prepared.packets.size() > kSceneCapacity)
        return Fail(reason, "B4 invalid frame generation or draw budget");
    for (const auto& packet : prepared.packets)
    {
        const uint64_t id = packet.object.id;
        if (id == 0 || id > m_worldInstances.size())
            return Fail(reason, "B4 invalid instance ID");
        const uint32_t sourceIndex = m_worldInstances[id - 1].sourceIndex;
        if (packet.object.visual != renderprep12::Resource12{0x10000u + sourceIndex, catalogGeneration})
            return Fail(reason, "B4 invalid packet geometry binding");
        m_visibleSources[m_visibleCount++] = sourceIndex;
    }
    return true;
}

bool GameDx12FourVisual12::PrepareStaging(
    const DirectX::XMFLOAT4X4& logicalViewProjection,
    const float cameraPosition[3], std::string& reason, NativeTimings12* nativeTimings)
{
    InvalidateStaging();
    if (!FiniteMatrix(logicalViewProjection))
        return Fail(reason, "M23 non-finite ViewProjection");
    if (cameraPosition == nullptr)
        return Fail(reason, "M23 missing camera position");

    const DirectX::XMFLOAT4X4 shaderCamera =
        Transpose(logicalViewProjection);
    auto* frameConstants = static_cast<uint8_t*>(m_constantsMapped) + m_frameConstantOffset;
    std::memcpy(frameConstants, &shaderCamera, sizeof(shaderCamera));

    if (!m_worldInstances.empty())
    {
        if (nativeTimings != nullptr)
        {
            if (!PrepareNativeVisibility(logicalViewProjection, *nativeTimings, reason))
                return false;
        }
        else
        {
            const auto viewProjection = DirectX::XMLoadFloat4x4(&logicalViewProjection);
            for (const auto& instance : m_worldInstances)
            {
                const auto& bounds = m_slots[instance.sourceIndex].asset;
                unsigned outside = 0x3f;
                for (unsigned corner = 0; corner < 8 && outside != 0; ++corner)
                {
                    const auto point = DirectX::XMVectorSet(
                        corner & 1 ? bounds.maximum.x : bounds.minimum.x,
                        corner & 2 ? bounds.maximum.y : bounds.minimum.y,
                        corner & 4 ? bounds.maximum.z : bounds.minimum.z, 1.0f);
                    DirectX::XMFLOAT4 clip;
                    DirectX::XMStoreFloat4(&clip, DirectX::XMVector4Transform(point, viewProjection));
                    // Отбрасываем AABB, только если все углы снаружи одной плоскости D3D.
                    unsigned mask = 0;
                    if (clip.x < -clip.w) mask |= 1;
                    if (clip.x > clip.w) mask |= 2;
                    if (clip.y < -clip.w) mask |= 4;
                    if (clip.y > clip.w) mask |= 8;
                    if (clip.z < 0) mask |= 16;
                    if (clip.z > clip.w) mask |= 32;
                    outside &= mask;
                }
                if (outside == 0)
                    m_visibleSources[m_visibleCount++] = instance.sourceIndex;
            }
        }
        DirectX::XMFLOAT4X4 world;
        DirectX::XMStoreFloat4x4(&world, DirectX::XMMatrixIdentity());
        dx12object12::ObjectConstants12 constants{};
        if (!dx12object12::BuildObjectConstants12(
                world, {0.8f, 0.8f, 0.8f, 1.0f}, constants))
            return Fail(reason, "B1 object constants rejected");
        std::memcpy(frameConstants + dx12object12::ObjectConstantsOffset(0),
                    &constants, sizeof(constants));
        m_stagingPrepared = true;
        return true;
    }

    for (uint32_t slot = 0; slot < kSlotCount; ++slot)
    {
        const fourvisual12::SlotPlan12& plan = fourvisual12::kPlan[slot];
        const SlotGpu12& gpu = m_slots[slot];
        const float localMinimum[3] = {gpu.asset.minimum.x,
                                       gpu.asset.minimum.y,
                                       gpu.asset.minimum.z};
        const float localMaximum[3] = {gpu.asset.maximum.x,
                                       gpu.asset.maximum.y,
                                       gpu.asset.maximum.z};
        if (!fourvisual12::BuildStagedTransform12(
                logicalViewProjection, cameraPosition, localMinimum,
                localMaximum, plan.ndcX, plan.ndcY, m_staged[slot], reason))
        {
            std::string message = "M23 visual ";
            message += std::to_string(plan.visualId);
            message += " staging failed: ";
            message += reason;
            return Fail(reason, message);
        }

        dx12object12::ObjectConstants12 objectConstants{};
        const DirectX::XMFLOAT4 tint = {plan.tint[0], plan.tint[1],
                                        plan.tint[2], plan.tint[3]};
        if (!dx12object12::BuildObjectConstants12(m_staged[slot].world,
                                                  tint, objectConstants))
            return Fail(reason, "M23 object constants rejected");
        std::memcpy(
            static_cast<uint8_t*>(m_constantsMapped) +
                dx12object12::ObjectConstantsOffset(slot),
            &objectConstants, sizeof(objectConstants));

        const fourvisual12::ProjectedBox12& box = m_staged[slot].projected;
        Msg("ph_dx12_m23: staged slot=%u visual=%u scale=%.6f yaw=%.1f "
            "distance=%.3f ndc_center=(%.3f,%.3f) "
            "ndc_box=(%.3f,%.3f)-(%.3f,%.3f) ndc_z=(%.4f,%.4f) "
            "tint=(%.3f,%.3f,%.3f,%.3f) "
            "world=(%.6f,%.6f,%.6f,%.6f;%.6f,%.6f,%.6f,%.6f;"
            "%.6f,%.6f,%.6f,%.6f;%.6f,%.6f,%.6f,%.6f)",
            slot, plan.visualId, m_staged[slot].scale,
            m_staged[slot].yawDegrees, m_staged[slot].distance, plan.ndcX,
            plan.ndcY, box.minX, box.minY, box.maxX, box.maxY, box.minZ,
            box.maxZ, tint.x, tint.y, tint.z, tint.w,
            m_staged[slot].world.m[0][0],
            m_staged[slot].world.m[0][1], m_staged[slot].world.m[0][2],
            m_staged[slot].world.m[0][3], m_staged[slot].world.m[1][0],
            m_staged[slot].world.m[1][1], m_staged[slot].world.m[1][2],
            m_staged[slot].world.m[1][3], m_staged[slot].world.m[2][0],
            m_staged[slot].world.m[2][1], m_staged[slot].world.m[2][2],
            m_staged[slot].world.m[2][3], m_staged[slot].world.m[3][0],
            m_staged[slot].world.m[3][1], m_staged[slot].world.m[3][2],
            m_staged[slot].world.m[3][3]);
    }

    for (uint32_t a = 0; a < kSlotCount; ++a)
    {
        for (uint32_t b = a + 1; b < kSlotCount; ++b)
        {
            if (!fourvisual12::BoxesDisjoint(m_staged[a].projected,
                                             m_staged[b].projected))
                return Fail(reason,
                            "M23 staged visual boxes overlap");
        }
    }

    m_stagingPrepared = true;
    return true;
}

bool GameDx12FourVisual12::RecordDraws(ID3D12GraphicsCommandList* list,
                                       uint32_t slotMask, bool combined,
                                       std::string& reason)
{
    if (list == nullptr)
        return Fail(reason, "M23 null command list");
    if (!m_stagingPrepared)
        return Fail(reason, "M23 staging is not prepared");
    if (combined)
    {
        if (slotMask != ((1u << kSlotCount) - 1u))
            return Fail(reason, "M23 combined pass slot mask is not 0xF");
    }
    else if (PopCount(slotMask) != 1u)
    {
        return Fail(reason, "M23 isolated pass must select one slot");
    }
    if ((slotMask >> kSlotCount) != 0)
        return Fail(reason, "M23 slot mask exceeds the bounded registry");

    list->SetGraphicsRootSignature(m_rootSignature.Get());
    list->SetPipelineState(m_pipelineState.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (!m_worldInstances.empty())
    {
        if (m_textured)
        {
            ID3D12DescriptorHeap* heaps[]{m_textureHeap.Get()};
            list->SetDescriptorHeaps(1, heaps);
        }
        list->SetGraphicsRootConstantBufferView(0, m_frameCBVA + m_frameConstantOffset);
        list->SetGraphicsRootConstantBufferView(1, m_objectCBVA[0] + m_frameConstantOffset);
        for (uint32_t visible = 0; visible < m_visibleCount; ++visible)
        {
            const SlotGpu12& source = m_slots[m_visibleSources[visible]];
            if (m_textured)
            {
                auto gpu = m_textureHeap->GetGPUDescriptorHandleForHeapStart();
                gpu.ptr += static_cast<UINT64>(source.textureIndex) * m_textureDescriptorSize;
                list->SetGraphicsRootDescriptorTable(2, gpu);
                const uint32_t mode = source.textureIndex == 0 ? 0u :
                                      (source.alphaTest ? 2u : 1u);
                const uint32_t material[]{mode, m_visibleSources[visible], m_materialView,
                                          std::bit_cast<uint32_t>(ps_r_alphatest_threshold)};
                list->SetGraphicsRoot32BitConstants(3, 4, material, 0);
            }
            list->IASetVertexBuffers(0, 1, &source.vertexView);
            list->IASetIndexBuffer(&source.indexView);
            list->DrawIndexedInstanced(source.indexCount, 1, 0, 0, 0);
        }
        return true;
    }
    for (uint32_t slot = 0; slot < kSlotCount; ++slot)
    {
        if ((slotMask & (1u << slot)) == 0)
            continue;
        const SlotGpu12& gpu = m_slots[slot];
        if (gpu.indexCount == 0 || gpu.visualId == 0)
            return Fail(reason, "M23 slot has no drawable geometry");
        list->IASetVertexBuffers(0, 1, &gpu.vertexView);
        list->IASetIndexBuffer(&gpu.indexView);
        list->SetGraphicsRootConstantBufferView(0, m_frameCBVA);
        list->SetGraphicsRootConstantBufferView(1, m_objectCBVA[slot]);
        list->DrawIndexedInstanced(gpu.indexCount, 1, 0, 0, 0);
        Msg("ph_dx12_m23: draw combined=%d slot=%u visual=%u indices=%u "
            "instances=1 first_index=0 base_vertex=0 vb=0x%llX ib=0x%llX "
            "object_cb=0x%llX",
            combined ? 1 : 0, slot, gpu.visualId, gpu.indexCount,
            static_cast<unsigned long long>(
                gpu.vertexView.BufferLocation),
            static_cast<unsigned long long>(
                gpu.indexView.BufferLocation),
            static_cast<unsigned long long>(m_objectCBVA[slot]));
    }
    return true;
}

bool GameDx12FourVisual12::RecordNativeFrame(
    ID3D12GraphicsCommandList* list, uint32_t frameSlot,
    const DirectX::XMFLOAT4X4& logicalViewProjection, const float cameraPosition[3],
    uint32_t& drawCount, NativeTimings12& timings, std::string& reason)
{
    drawCount = 0;
    if (frameSlot >= GameDx12NativeFrame12::kFrameCount || !m_loaded ||
        !m_uploadConfirmed || m_worldInstances.empty() || m_gpuUnknown)
        return Fail(reason, "B3 scene unavailable");
    // Native frame владеет ожиданием fence этого слота до вызова.
    m_frameConstantOffset = (static_cast<uint64_t>(frameSlot) + 1) * kConstantsBytes;
    if (!PrepareStaging(logicalViewProjection, cameraPosition, reason, &timings))
        return false;
    const auto recordStart = std::chrono::steady_clock::now();
    if (!RecordDraws(list, (1u << kSlotCount) - 1u, true, reason))
        return false;
    timings.recordMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - recordStart).count();
    drawCount = m_visibleCount;
    return true;
}

bool GameDx12FourVisual12::MarkPassSubmitted(uint32_t slotMask,
                                             uint64_t fenceValue,
                                             ID3D12Fence* fence,
                                             ID3D12Device* device,
                                             std::string& reason)
{
    if (!m_passRecorded || m_passSubmitted)
        return Fail(reason, "M23 no recorded pass awaiting submission");
    if (fence == nullptr || device == nullptr)
        return Fail(reason, "M23 missing owner fence or device");
    if (m_passFence != nullptr || m_passDevice != nullptr)
        return Fail(reason, "M23 pass already owns submission objects");
    if (!m_uploadConfirmed || fenceValue == 0 ||
        fenceValue <= m_uploadFenceValue ||
        fenceValue <= m_lastDrawUseFenceValue)
        return Fail(reason, "M23 invalid pass fence value");
    if (slotMask == 0 || (slotMask >> kSlotCount) != 0)
        return Fail(reason, "M23 invalid pass slot mask");

    m_passFence = fence;
    m_passDevice = device;
    m_passSubmitted = true;
    m_passFenceValue = fenceValue;
    m_passSlotMask = slotMask;
    return true;
}

bool GameDx12FourVisual12::OnPassCompleted(uint64_t fenceValue,
                                           ID3D12Fence* fence,
                                           ID3D12Device* device,
                                           std::string& reason)
{
    if (!m_passRecorded || !m_passSubmitted)
        return Fail(reason, "M23 no submitted pass awaiting completion");
    if (fence == nullptr || device == nullptr)
        return Fail(reason, "M23 missing owner fence or device");
    if (fence != m_passFence.Get() || device != m_passDevice.Get())
        return Fail(reason, "M23 fence/device identity mismatch");
    if (fenceValue == 0 || fenceValue != m_passFenceValue ||
        fenceValue <= m_uploadFenceValue)
        return Fail(reason, "M23 pass fence value mismatch");
    if (fence->GetCompletedValue() < fenceValue)
        return Fail(reason, "M23 pass fence is not complete");
    if (device->GetDeviceRemovedReason() != S_OK)
        return Fail(reason, "M23 device removed before pass completion");

    m_passFence.Reset();
    m_passDevice.Reset();
    m_lastDrawUseFenceValue =
        std::max(m_lastDrawUseFenceValue, fenceValue);
    m_passRecorded = false;
    m_passSubmitted = false;
    m_passSlotMask = 0;
    m_passFenceValue = 0;
    return true;
}

void GameDx12FourVisual12::AbortRecordedPass() noexcept
{
    if (m_passSubmitted)
        return;
    m_passFence.Reset();
    m_passDevice.Reset();
    m_passRecorded = false;
    m_passFenceValue = 0;
    m_passSlotMask = 0;
}

void GameDx12FourVisual12::FailPassUnknown(const char* stage,
                                           std::string& reason) noexcept
{
    // Detach every M23 GPU object before any owner/destructor path can release
    // it. QuarantineGpu is nonallocating and idempotent.
    QuarantineGpu();
    try
    {
        Msg("ph_dx12_m23: GPU completion unknown stage='%s'; M23 resources "
            "are quarantined for process lifetime",
            stage != nullptr ? stage : "?");
    }
    catch (...)
    {
    }
    try
    {
        reason = "M23 GPU completion unconfirmed";
    }
    catch (...)
    {
    }
    LogProbe("unknown-draw");
}

bool GameDx12FourVisual12::RunPass(
    const GameDx12FourVisualOwner12& owner,
    GameDx12OffscreenPass12& offscreen, uint32_t slotMask, bool combined,
    bool wantPng, const wchar_t* pngPath, PassResult12& passResult,
    std::string& reason)
{
    m_lastError.clear();
    passResult = {};
    if (m_passRecorded || m_passSubmitted)
        return Fail(reason, "M23 pass is already recorded or submitted");
    if (!m_stagingPrepared)
        return Fail(reason, "M23 staging is not prepared");
    if (owner.allocator == nullptr || owner.list == nullptr ||
        owner.queue == nullptr || owner.fence == nullptr ||
        owner.fenceValue == nullptr)
        return Fail(reason, "M23 invalid owner context for pass");
    if (*owner.fenceValue == std::numeric_limits<uint64_t>::max())
        return Fail(reason, "M23 owner fence overflow");

    HRESULT hr = owner.allocator->Reset();
    if (FAILED(hr))
    {
        m_listPoisoned = true;
        return Fail(reason, "M23 pass allocator reset failed");
    }
    hr = owner.list->Reset(owner.allocator, nullptr);
    if (FAILED(hr))
    {
        m_listPoisoned = true;
        return Fail(reason, "M23 pass list reset failed");
    }

    m_passRecorded = true;
    M23RecordingGuard12 recording(owner.list, m_listPoisoned,
                                  m_lastRecordingClosed,
                                  "draw-close-failure");
    if (!offscreen.RecordBegin(owner.list, fourvisual12::kClearColor,
                               reason))
    {
        offscreen.AbortUnsubmittedFrame();
        AbortRecordedPass();
        recording.Abandon();
        if (m_listPoisoned)
            QuarantineGpu();
        return false;
    }
    if (!RecordDraws(owner.list, slotMask, combined, reason))
    {
        offscreen.AbortUnsubmittedFrame();
        AbortRecordedPass();
        recording.Abandon();
        if (m_listPoisoned)
            QuarantineGpu();
        return false;
    }
    if (!offscreen.RecordEnd(owner.list, reason))
    {
        offscreen.AbortUnsubmittedFrame();
        AbortRecordedPass();
        recording.Abandon();
        if (m_listPoisoned)
            QuarantineGpu();
        return false;
    }

    if (!recording.Close())
    {
        offscreen.AbortUnsubmittedFrame();
        AbortRecordedPass();
        QuarantineGpu();
        return Fail(reason, "M23 pass list close failed");
    }

    const uint64_t expected = *owner.fenceValue + 1;
    if (expected == 0)
    {
        AbortRecordedPass();
        return Fail(reason, "M23 pass fence overflow");
    }

    ID3D12CommandList* lists[] = {owner.list};
    owner.queue->ExecuteCommandLists(1, lists);

#ifdef OGSR_M23_FAULT_INJECTION
    if (m23fault12::Is("forced-unknown"))
    {
        // The submission is real and in flight, but completion is
        // deliberately not claimed; the owner must retain every M23 object
        // until process exit.
        FailPassUnknown("forced-fault", reason);
        return false;
    }
#endif

    if (!MarkPassSubmitted(slotMask, expected, owner.fence, owner.device,
                           reason))
    {
        FailPassUnknown("mark-submitted", reason);
        return false;
    }
    *owner.fenceValue = expected;
#ifdef OGSR_M23_FAULT_INJECTION
    if (m23fault12::Is("signal-failure"))
    {
        FailPassUnknown("signal", reason);
        return false;
    }
#endif
    hr = owner.queue->Signal(owner.fence, expected);
    if (FAILED(hr))
    {
        FailPassUnknown("signal", reason);
        return false;
    }
#ifdef OGSR_M23_FAULT_INJECTION
    if (m23fault12::Is("fence-event-failure"))
    {
        FailPassUnknown("fence-event", reason);
        return false;
    }
    if (m23fault12::Is("fence-wait-failure"))
    {
        FailPassUnknown("fence-wait", reason);
        return false;
    }
#endif
    if (owner.fence->GetCompletedValue() < expected)
    {
        hr = owner.fence->SetEventOnCompletion(expected, owner.fenceEvent);
        if (FAILED(hr))
        {
            FailPassUnknown("fence-event", reason);
            return false;
        }
        if (WaitForSingleObject(owner.fenceEvent, kFenceTimeoutMs) !=
            WAIT_OBJECT_0)
        {
            FailPassUnknown("fence-wait", reason);
            return false;
        }
    }
    bool forceDeviceRemoved = false;
#ifdef OGSR_M23_FAULT_INJECTION
    forceDeviceRemoved = m23fault12::Is("device-removed");
#endif
    if (owner.fence->GetCompletedValue() < expected ||
        forceDeviceRemoved ||
        owner.device->GetDeviceRemovedReason() != S_OK)
    {
        FailPassUnknown("completion", reason);
        return false;
    }
    if (!OnPassCompleted(expected, owner.fence, owner.device, reason))
    {
        FailPassUnknown("identity", reason);
        return false;
    }

    passResult.gpuConfirmed = true;
    passResult.fence = expected;
    offscreen.CommitSubmittedFrame();
    if (!offscreen.Readback(reason))
        return false;
    passResult.coverage = offscreen.EvaluateCoverage();
    if (wantPng && pngPath != nullptr)
    {
        if (!offscreen.SavePng(pngPath, reason))
            return false;
    }
    passResult.completed = true;
    return true;
}

bool GameDx12FourVisual12::RenderDiagnostic(
    const GameDx12FourVisualOwner12& owner,
    const DirectX::XMFLOAT4X4& logicalViewProjection,
    const float cameraPosition[3],
    GameDx12OffscreenPass12& offscreen, uint32_t width, uint32_t height,
    const wchar_t* pngDirectory, Result12* result, std::string& reason)
{
    try
    {
        return RenderDiagnosticImpl(owner, logicalViewProjection,
                                    cameraPosition, offscreen, width,
                                    height, pngDirectory, result, reason);
    }
    catch (const std::bad_alloc&)
    {
        return HandleRenderException(reason);
    }
    catch (const std::length_error&)
    {
        return HandleRenderException(reason);
    }
    catch (...)
    {
        return HandleRenderException(reason);
    }
}

bool GameDx12FourVisual12::HandleRenderException(
    std::string& reason) noexcept
{
    // A recorded pass may have reached ExecuteCommandLists before the
    // exception; there is no way to prove completion, so retain all M23 GPU
    // objects. If no pass was recorded, no GPU submission happened and the
    // already-uploaded registry may stay loaded.
    if (m_gpuUnknown || m_quarantined || m_listPoisoned ||
        m_passRecorded || m_passSubmitted)
        QuarantineGpu();
    try
    {
        reason = "M23 render boundary exception";
    }
    catch (...)
    {
    }
    return false;
}

bool GameDx12FourVisual12::RenderDiagnosticImpl(
    const GameDx12FourVisualOwner12& owner,
    const DirectX::XMFLOAT4X4& logicalViewProjection,
    const float cameraPosition[3],
    GameDx12OffscreenPass12& offscreen, uint32_t width, uint32_t height,
    const wchar_t* pngDirectory, Result12* result, std::string& reason)
{
    GameDx12FourVisual12::Result12 localResult;
    if (result == nullptr)
        result = &localResult;
    *result = {};
    m_frameConstantOffset = 0;

    m_lastError.clear();
    if (!m_loaded || !m_uploadConfirmed || m_gpuUnknown)
        return Fail(reason, "M23 registry is not loaded");
    if (m_passRecorded || m_passSubmitted)
        return Fail(reason, "M23 previous pass is not confirmed");
    if (width == 0 || height == 0)
        return Fail(reason, "M23 viewport is unavailable");
    if (m_listPoisoned)
        return Fail(reason, "M23 command list is poisoned");

    if (offscreen.IsCreated() &&
        (offscreen.Width() != width || offscreen.Height() != height))
    {
        if (!m_uploadConfirmed || m_passRecorded || m_passSubmitted)
            return Fail(reason, "M23 cannot resize with pending GPU work");
        offscreen.Release();
    }
    if (!offscreen.IsCreated() &&
        !offscreen.Create(owner.device, width, height, reason))
        return false;

    if (!PrepareStaging(logicalViewProjection, cameraPosition, reason))
        return false;

    const bool wantPng =
        pngDirectory != nullptr && pngDirectory[0] != L'\0';
    if (!m_worldInstances.empty())
    {
        const std::wstring path = wantPng
            ? JoinPath(pngDirectory, m_textured ? L"b2_jupiter_textured.png"
                                               : L"b1_jupiter_world.png")
            : std::wstring{};
        PassResult12 pass;
        m_materialView = 0;
        const bool completed = RunPass(owner, offscreen,
            (1u << kSlotCount) - 1u, true, wantPng, path.c_str(), pass, reason);
        result->gpuCompleted = pass.gpuConfirmed;
        result->combinedFence = pass.fence;
        result->lastDrawUseFence = m_lastDrawUseFenceValue;
        result->combinedCoverage = pass.coverage;
        result->combinedDrawCount = completed ? m_visibleCount : 0;
        result->pngSaved = completed && wantPng;
        result->imageGatePass = completed && pass.coverage.nonClearPixels > 0;
        if (m_textured && result->imageGatePass)
        {
            uint32_t texturedDraws = 0;
            for (uint32_t i = 0; i < m_visibleCount; ++i)
                texturedDraws += m_slots[m_visibleSources[i]].textureIndex != 0;
            Msg("ph_dx12_b2: frame textured_draws=%u fallback_draws=%u fence=%llu",
                texturedDraws, m_visibleCount - texturedDraws,
                static_cast<unsigned long long>(pass.fence));
            result->imageGatePass = texturedDraws != 0;
            if (result->imageGatePass && wantPng)
            {
                const wchar_t* const names[] = {
                    L"b2_jupiter_material_ids.png", L"b2_jupiter_uv_normals.png",
                    L"b2_jupiter_unsupported_mask.png"};
                for (uint32_t i = 0; i < 3; ++i)
                {
                    m_materialView = i + 1;
                    PassResult12 view;
                    const std::wstring debugPath = JoinPath(pngDirectory, names[i]);
                    if (!RunPass(owner, offscreen, (1u << kSlotCount) - 1u, true,
                                 true, debugPath.c_str(), view, reason))
                    {
                        result->gpuCompleted = view.gpuConfirmed;
                        result->imageGatePass = false;
                        return false;
                    }
                    result->lastDrawUseFence = view.fence;
                }
                m_materialView = 0;
            }
        }
        return result->imageGatePass;
    }
    std::wstring combinedPath;
    std::wstring isolatedPath[kSlotCount];
    if (wantPng)
    {
        combinedPath = JoinPath(
            pngDirectory, L"m23_combined_visuals_9_105_106_107.png");
        static const wchar_t* const kIsolatedNames[kSlotCount] = {
            L"m23_isolated_visual_9.png",
            L"m23_isolated_visual_105.png",
            L"m23_isolated_visual_106.png",
            L"m23_isolated_visual_107.png"};
        for (uint32_t slot = 0; slot < kSlotCount; ++slot)
            isolatedPath[slot] =
                JoinPath(pngDirectory, kIsolatedNames[slot]);
    }

    result->cullMode = m_cullMode;
    result->uploadFence = m_uploadFenceValue;

    PassResult12 combinedPass;
    if (!RunPass(owner, offscreen, (1u << kSlotCount) - 1u, true, wantPng,
                 wantPng ? combinedPath.c_str() : nullptr, combinedPass,
                 reason))
    {
        if (combinedPass.gpuConfirmed)
            result->gpuCompleted = true;
        return false;
    }
    result->combinedFence = combinedPass.fence;
    result->combinedCoverage = combinedPass.coverage;
    result->combinedDrawCount = kSlotCount;

    uint8_t tintBytes[kSlotCount][4] = {};
    GameDx12ColorMask12 masks[kSlotCount];
    for (uint32_t slot = 0; slot < kSlotCount; ++slot)
    {
        GameDx12TintBytes(fourvisual12::kPlan[slot].tint,
                          tintBytes[slot]);
        masks[slot] = EvaluateGameDx12ColorMask(
            offscreen.Pixels().data(), width, height, offscreen.RowPitch(),
            tintBytes[slot]);
        result->slots[slot].visualId = m_slots[slot].visualId;
        result->slots[slot].indexCount = m_slots[slot].indexCount;
        result->slots[slot].vertexCount = m_slots[slot].vertexCount;
        result->slots[slot].combinedPixels = masks[slot].pixels;
        result->slots[slot].combinedMinX = masks[slot].minX;
        result->slots[slot].combinedMinY = masks[slot].minY;
        result->slots[slot].combinedMaxX = masks[slot].maxX;
        result->slots[slot].combinedMaxY = masks[slot].maxY;
    }

    bool isolatedPurity = true;
    for (uint32_t slot = 0; slot < kSlotCount; ++slot)
    {
        PassResult12 isolatedPass;
        if (!RunPass(owner, offscreen, 1u << slot, false, wantPng,
                     wantPng ? isolatedPath[slot].c_str() : nullptr,
                     isolatedPass, reason))
        {
            if (isolatedPass.gpuConfirmed)
                result->gpuCompleted = true;
            return false;
        }
        result->isolatedFences[slot] = isolatedPass.fence;
        // The isolated target must contain only this slot's tint. Counting
        // tinted pixels separately from non-clear pixels makes a foreign or
        // partially written pixel a hard gate failure.
        const GameDx12ColorMask12 isolatedMask =
            EvaluateGameDx12ColorMask(
                offscreen.Pixels().data(), width, height,
                offscreen.RowPitch(), tintBytes[slot]);
        result->slots[slot].isolatedPixels = isolatedMask.pixels;
        if (isolatedMask.pixels !=
            isolatedPass.coverage.nonClearPixels)
            isolatedPurity = false;
    }
    result->isolatedDrawCount = kSlotCount;
    result->lastDrawUseFence = m_lastDrawUseFenceValue;
    result->gpuCompleted = true;

    uint64_t combinedMaskSum = 0;
    for (uint32_t slot = 0; slot < kSlotCount; ++slot)
        combinedMaskSum += result->slots[slot].combinedPixels;

    bool gate = combinedPass.coverage.HasVisiblePixels() &&
                isolatedPurity &&
                combinedMaskSum ==
                    combinedPass.coverage.nonClearPixels;
    for (uint32_t slot = 0; slot < kSlotCount; ++slot)
    {
        const uint64_t isolated =
            result->slots[slot].isolatedPixels;
        const uint64_t combined =
            result->slots[slot].combinedPixels;
        if (combined == 0 || isolated == 0 || combined != isolated)
            gate = false;
        result->slots[slot].pngSaved = wantPng;
        Msg("ph_dx12_m23: slot=%u visual=%u combined_pixels=%llu "
            "isolated_pixels=%llu bbox=(%u,%u)-(%u,%u) indices=%u",
            slot, result->slots[slot].visualId,
            static_cast<unsigned long long>(combined),
            static_cast<unsigned long long>(isolated),
            result->slots[slot].combinedMinX,
            result->slots[slot].combinedMinY,
            result->slots[slot].combinedMaxX,
            result->slots[slot].combinedMaxY,
            result->slots[slot].indexCount);
    }
    for (uint32_t a = 0; a < kSlotCount; ++a)
    {
        for (uint32_t b = a + 1; b < kSlotCount; ++b)
        {
            if (!GameDx12ColorMasksDisjoint(masks[a], masks[b]))
                gate = false;
        }
    }

    result->pngSaved = wantPng;
    result->imageGatePass = gate;

    Msg("ph_dx12_m23: four-visual frame %s level_visuals=9,105,106,107 "
        "draws=%u isolated_draws=%u upload_fence=%llu combined_fence=%llu "
        "last_draw_use_fence=%llu cull=%s rt=R8G8B8A8_UNORM depth=D32_FLOAT "
        "readback=1 image_gate=%d mask_sum=%llu nonclear=%llu/%llu "
        "isolated_purity=%d png_saved=%d",
        gate ? "PASS" : "FAIL", result->combinedDrawCount,
        result->isolatedDrawCount,
        static_cast<unsigned long long>(result->uploadFence),
        static_cast<unsigned long long>(result->combinedFence),
        static_cast<unsigned long long>(result->lastDrawUseFence),
        result->cullMode == D3D12_CULL_MODE_NONE ? "none" : "back",
        gate ? 1 : 0,
        static_cast<unsigned long long>(combinedMaskSum),
        static_cast<unsigned long long>(
            combinedPass.coverage.nonClearPixels),
        static_cast<unsigned long long>(
            combinedPass.coverage.totalPixels),
        isolatedPurity ? 1 : 0,
        result->pngSaved ? 1 : 0);

    if (!gate)
    {
        if (reason.empty())
            reason = "M23 per-visual pixel identity gate failed";
        return false;
    }
    return true;
}

void GameDx12FourVisual12::OnLevelTeardown(uint64_t nextEpoch)
{
    m_levelEpoch = nextEpoch;
    InvalidateStaging();
    m_loaded = false;
    if (m_gpuUnknown || m_quarantined || m_listPoisoned)
    {
        QuarantineGpu();
        return;
    }
    if (m_passRecorded || m_passSubmitted)
    {
        // A recorded/submitted pass has no verified completion here.
        m_generation = 0;
        QuarantineGpu();
        return;
    }
    if (m_uploadSubmitted && !m_uploadConfirmed)
    {
        QuarantineGpu();
        return;
    }
    if (m_uploadConfirmed)
    {
        ReleaseGpu();
        return;
    }
    for (uint32_t slot = 0; slot < kSceneCapacity; ++slot)
        m_slots[slot] = SlotGpu12{};
    m_worldInstances.clear();
    m_sourceCount = 0;
    m_levelSession = 0;
    m_generation = 0;
}

void GameDx12FourVisual12::OnDeviceReset(uint64_t nextEpoch)
{
    m_levelEpoch = nextEpoch;
    InvalidateStaging();
    if (m_gpuUnknown || m_quarantined || m_listPoisoned)
    {
        QuarantineGpu();
        return;
    }
    if (m_passRecorded || m_passSubmitted)
    {
        // The old pass has no verified completion on this reset path.
        m_generation = 0;
        m_loaded = false;
        QuarantineGpu();
        return;
    }
    if (m_loaded && m_uploadConfirmed)
        m_generation = NextGeneration(m_levelSession, nextEpoch,
                                      m_generation);
    else
        m_generation = 0;
    for (uint32_t slot = 0; slot < kSceneCapacity; ++slot)
        m_slots[slot].generation = m_generation;
}

void GameDx12FourVisual12::ReleaseGpu()
{
    if (m_gpuUnknown || m_quarantined || m_listPoisoned)
    {
        QuarantineGpu();
        return;
    }
    if (m_constantsMapped != nullptr && m_constants != nullptr)
        m_constants->Unmap(0, nullptr);
    m_constantsMapped = nullptr;
    m_constants.Reset();
    m_rootSignature.Reset();
    m_pipelineState.Reset();
    m_textureHeap.Reset();
    for (auto& texture : m_textures)
        texture = {};
    m_textured = false;
    for (uint32_t slot = 0; slot < kSceneCapacity; ++slot)
        m_slots[slot] = SlotGpu12{};
    InvalidateStaging();
    m_passFence.Reset();
    m_passDevice.Reset();
    m_passRecorded = false;
    m_passSubmitted = false;
    m_passSlotMask = 0;
    m_passFenceValue = 0;
    m_uploadSubmitted = false;
    m_uploadConfirmed = false;
    m_uploadFenceValue = 0;
    m_lastDrawUseFenceValue = 0;
    m_loaded = false;
    m_worldInstances.clear();
    m_sourceCount = 0;
    m_levelSession = 0;
    m_levelEpoch = 0;
    m_generation = 0;
    m_frameCBVA = 0;
    for (uint32_t slot = 0; slot < kSceneCapacity; ++slot)
        m_objectCBVA[slot] = 0;
}

void GameDx12FourVisual12::QuarantineGpu() noexcept
{
    ++g_quarantineCalls;
    const bool occupiedAtEntry = g_fourVisualQuarantine.occupied;
    g_quarantineOccupiedAtEntry = occupiedAtEntry ? 1u : 0u;

    if (m_constantsMapped != nullptr && m_constants != nullptr)
        m_constants->Unmap(0, nullptr);
    m_constantsMapped = nullptr;

    if (!occupiedAtEntry)
    {
        g_fourVisualQuarantine.occupied = true;
        for (uint32_t slot = 0; slot < kSceneCapacity; ++slot)
        {
            DetachToQuarantine(g_fourVisualQuarantine.slots[slot][0],
                               m_slots[slot].vertexBuffer);
            DetachToQuarantine(g_fourVisualQuarantine.slots[slot][1],
                               m_slots[slot].indexBuffer);
            DetachToQuarantine(g_fourVisualQuarantine.slots[slot][2],
                               m_slots[slot].uploadVertex);
            DetachToQuarantine(g_fourVisualQuarantine.slots[slot][3],
                               m_slots[slot].uploadIndex);
        }
        for (uint32_t i = 0; i <= kSceneCapacity; ++i)
        {
            DetachToQuarantine(g_fourVisualQuarantine.textures[i][0], m_textures[i].image);
            DetachToQuarantine(g_fourVisualQuarantine.textures[i][1], m_textures[i].upload);
        }
        DetachToQuarantine(g_fourVisualQuarantine.textureHeap, m_textureHeap);
        DetachToQuarantine(g_fourVisualQuarantine.constants, m_constants);
        DetachToQuarantine(g_fourVisualQuarantine.rootSignature,
                           m_rootSignature);
        DetachToQuarantine(g_fourVisualQuarantine.pipelineState,
                           m_pipelineState);
        DetachToQuarantine(g_fourVisualQuarantine.passFence, m_passFence);
        DetachToQuarantine(g_fourVisualQuarantine.passDevice, m_passDevice);
    }
    else
    {
        // The process-lifetime quarantine is already occupied. Drop the
        // references without Release: intentionally leaked until exit, never
        // released while completion is unknown.
        for (uint32_t slot = 0; slot < kSceneCapacity; ++slot)
        {
            DetachAndDrop(m_slots[slot].vertexBuffer);
            DetachAndDrop(m_slots[slot].indexBuffer);
            DetachAndDrop(m_slots[slot].uploadVertex);
            DetachAndDrop(m_slots[slot].uploadIndex);
        }
        for (auto& texture : m_textures)
        {
            DetachAndDrop(texture.image);
            DetachAndDrop(texture.upload);
        }
        DetachAndDrop(m_textureHeap);
        DetachAndDrop(m_constants);
        DetachAndDrop(m_rootSignature);
        DetachAndDrop(m_pipelineState);
        DetachAndDrop(m_passFence);
        DetachAndDrop(m_passDevice);
    }

    for (uint32_t slot = 0; slot < kSceneCapacity; ++slot)
        m_slots[slot] = SlotGpu12{};
    InvalidateStaging();
    m_loaded = false;
    m_gpuUnknown = true;
    m_quarantined = true;
    m_uploadSubmitted = false;
    m_uploadConfirmed = false;
    m_uploadFenceValue = 0;
    m_passRecorded = false;
    m_passSubmitted = false;
    m_passSlotMask = 0;
    m_passFenceValue = 0;
    m_worldInstances.clear();
    m_textured = false;
    m_sourceCount = 0;
    m_levelSession = 0;
    m_levelEpoch = 0;
    m_generation = 0;
    m_frameCBVA = 0;
    for (uint32_t slot = 0; slot < kSceneCapacity; ++slot)
        m_objectCBVA[slot] = 0;
}

void GameDx12FourVisual12::LogProbe(const char* stage) noexcept
{
#ifdef OGSR_M23_LIFETIME_PROBE
    const bool shutdown =
        stage != nullptr && std::strcmp(stage, "shutdown") == 0;
    if (shutdown)
    {
        if (m_shutdownProbeLogged)
            return;
        m_shutdownProbeLogged = true;
    }
    else
    {
        if (m_unknownProbeLogged)
            return;
        m_unknownProbeLogged = true;
    }
    try
    {
        Msg("ph_dx12_m23_probe: stage=%s created=%llu captured=%llu "
            "released=%llu detached=%llu live=%lld quarantine_calls=%llu "
            "quarantine_occupied_last=%llu quarantine_detached=%llu "
            "gpu_unknown=%d quarantined=%d poisoned=%d",
            stage != nullptr ? stage : "?",
            static_cast<unsigned long long>(
                m23probe12::CreatedCounter().load()),
            static_cast<unsigned long long>(
                m23probe12::CapturedCounter().load()),
            static_cast<unsigned long long>(
                m23probe12::ReleasedCounter().load()),
            static_cast<unsigned long long>(
                m23probe12::DetachedCounter().load()),
            static_cast<long long>(m23probe12::LiveCount()),
            static_cast<unsigned long long>(g_quarantineCalls),
            static_cast<unsigned long long>(g_quarantineOccupiedAtEntry),
            static_cast<unsigned long long>(g_quarantineDetachedResources),
            m_gpuUnknown ? 1 : 0, m_quarantined ? 1 : 0,
            m_listPoisoned ? 1 : 0);
    }
    catch (...)
    {
    }
#else
    (void)stage;
#endif
}

void GameDx12FourVisual12::Shutdown(bool gpuCompletionConfirmed)
{
    if (m_gpuUnknown || m_quarantined || m_listPoisoned)
    {
        QuarantineGpu();
        LogProbe("shutdown");
        return;
    }
    if (gpuCompletionConfirmed && m_uploadConfirmed && !m_passRecorded &&
        !m_passSubmitted)
    {
        ReleaseGpu();
        LogProbe("shutdown");
        return;
    }
    if (m_uploadSubmitted || m_uploadConfirmed ||
        m_slots[0].vertexBuffer != nullptr || m_constants != nullptr ||
        m_rootSignature != nullptr || m_pipelineState != nullptr)
    {
        QuarantineGpu();
        LogProbe("shutdown");
        return;
    }
    InvalidateStaging();
    m_loaded = false;
    m_worldInstances.clear();
    m_sourceCount = 0;
    m_levelSession = 0;
    m_levelEpoch = 0;
    m_generation = 0;
    m_lastDrawUseFenceValue = 0;
    LogProbe("shutdown");
}

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
