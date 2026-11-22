#include "WeatherMap.h"

#include "CloudFaultInjection.h"
#include "Renderer.h"

#include <DirectXPackedVector.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace
{
constexpr UINT kThreadGroupX = 8;
constexpr UINT kThreadGroupY = 8;

const char* FormatText(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    default: return "unknown";
    }
}

bool IsFormatUsable(ID3D12Device* device, DXGI_FORMAT format)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    support.Format = format;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                           &support, sizeof(support))))
        return false;

    const D3D12_FORMAT_SUPPORT1 required1 =
        D3D12_FORMAT_SUPPORT1_TEXTURE2D |
        D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW |
        D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
    const D3D12_FORMAT_SUPPORT2 required2 =
        D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    return (support.Support1 & required1) == required1 &&
           (support.Support2 & required2) == required2;
}
} // namespace

WeatherMap::WeatherMap() = default;

WeatherMap::~WeatherMap()
{
    Shutdown();
}

void WeatherMap::Initialize(Renderer& renderer, uint32_t seed)
{
    if (initialized_)
        return;

    renderer_ = &renderer;
    seed_ = seed;
    SelectFormat();
    CreatePipeline();
    CreateResource();
    CreateDescriptors();
    CreateProbeBuffer();
    Generate(seed_);
    initialized_ = true;
}

void WeatherMap::SelectFormat()
{
    const DXGI_FORMAT candidates[] = {
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
    };
    for (DXGI_FORMAT candidate : candidates)
    {
        const bool usable = IsFormatUsable(renderer_->Device(), candidate);
        Log("weather map format %s: %s", FormatText(candidate),
            usable ? "supported" : "unsupported");
        if (usable)
        {
            format_ = candidate;
            return;
        }
    }
    Fail("the device does not support a usable weather map format");
}

void WeatherMap::CreateResource()
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kResolution;
    desc.Height = kResolution;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format_;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ThrowIfFailed(renderer_->Device()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_COMMON, nullptr,
                      IID_PPV_ARGS(&texture_)),
                  "CreateCommittedResource (weather map)");
    state_ = D3D12_RESOURCE_STATE_COMMON;
}

void WeatherMap::CreateDescriptors()
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = format_;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Texture2D.MipSlice = 0;
    uav.Texture2D.PlaneSlice = 0;
    renderer_->Device()->CreateUnorderedAccessView(
        texture_.Get(), nullptr, &uav, renderer_->SrvCpuHandle(kUavSlot));

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = format_;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.PlaneSlice = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;
    renderer_->Device()->CreateShaderResourceView(
        texture_.Get(), &srv, renderer_->SrvCpuHandle(kSrvSlot));
}

void WeatherMap::CreateProbeBuffer()
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 16 * sizeof(float) * 4;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ThrowIfFailed(renderer_->Device()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_COMMON, nullptr,
                      IID_PPV_ARGS(&probeBuffer_)),
                  "CreateCommittedResource (weather probe)");
    probeState_ = D3D12_RESOURCE_STATE_COMMON;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.FirstElement = 0;
    uav.Buffer.NumElements = 16;
    uav.Buffer.StructureByteStride = sizeof(float) * 4;
    uav.Buffer.CounterOffsetInBytes = 0;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    renderer_->Device()->CreateUnorderedAccessView(
        probeBuffer_.Get(), nullptr, &uav,
        renderer_->SrvCpuHandle(kProbeUavSlot));
}

void WeatherMap::CreatePipeline()
{
    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &uavRange;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.RegisterSpace = 0;
    parameters[1].Constants.Num32BitValues = 4;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = parameters;
    rootDesc.NumStaticSamplers = 0;
    rootDesc.pStaticSamplers = nullptr;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3D12SerializeRootSignature(
        &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serialized.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr))
    {
        if (errors)
        {
            LogError("weather root signature error: %s",
                     static_cast<const char*>(errors->GetBufferPointer()));
        }
        ThrowIfFailed(hr, "D3D12SerializeRootSignature (weather)");
    }

    ThrowIfFailed(renderer_->Device()->CreateRootSignature(
                      0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature (weather)");

    const auto shader = CompileShaderFile(
        ShaderDirectory() / L"WeatherGenerate.hlsl", "GenerateWeather", "cs_5_1");

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.CS = { shader->GetBufferPointer(), shader->GetBufferSize() };
    psoDesc.NodeMask = 0;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ThrowIfFailed(renderer_->Device()->CreateComputePipelineState(
                      &psoDesc, IID_PPV_ARGS(&pipelineState_)),
                  "CreateComputePipelineState (weather)");

    Log("weather map generation pipeline initialized");

    // Sampler-seam probe pipeline. It samples the generated map both normally
    // and after an exact +1 tile shift; the WRAP sampler must return the same
    // RGBA values.
    D3D12_DESCRIPTOR_RANGE probeSrvRange{};
    probeSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    probeSrvRange.NumDescriptors = 1;
    probeSrvRange.BaseShaderRegister = 0;
    probeSrvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE probeUavRange{};
    probeUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    probeUavRange.NumDescriptors = 1;
    probeUavRange.BaseShaderRegister = 1;
    probeUavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER probeParameters[2]{};
    probeParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    probeParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    probeParameters[0].DescriptorTable.pDescriptorRanges = &probeSrvRange;
    probeParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    probeParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    probeParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    probeParameters[1].DescriptorTable.pDescriptorRanges = &probeUavRange;
    probeParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC probeSampler{};
    probeSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    probeSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    probeSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    probeSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    probeSampler.MipLODBias = 0.0f;
    probeSampler.MaxAnisotropy = 1;
    probeSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    probeSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    probeSampler.MinLOD = 0.0f;
    probeSampler.MaxLOD = D3D12_FLOAT32_MAX;
    probeSampler.ShaderRegister = 0;
    probeSampler.RegisterSpace = 0;
    probeSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC probeDesc{};
    probeDesc.NumParameters = 2;
    probeDesc.pParameters = probeParameters;
    probeDesc.NumStaticSamplers = 1;
    probeDesc.pStaticSamplers = &probeSampler;
    probeDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> probeSerialized;
    ComPtr<ID3DBlob> probeErrors;
    const HRESULT probeHr = D3D12SerializeRootSignature(
        &probeDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        probeSerialized.GetAddressOf(), probeErrors.GetAddressOf());
    if (FAILED(probeHr))
    {
        if (probeErrors)
        {
            LogError("weather probe root signature error: %s",
                     static_cast<const char*>(probeErrors->GetBufferPointer()));
        }
        ThrowIfFailed(probeHr, "D3D12SerializeRootSignature (weather probe)");
    }
    ThrowIfFailed(renderer_->Device()->CreateRootSignature(
                      0, probeSerialized->GetBufferPointer(),
                      probeSerialized->GetBufferSize(),
                      IID_PPV_ARGS(&probeRootSignature_)),
                  "CreateRootSignature (weather probe)");

    const auto probeShader = CompileShaderFile(
        ShaderDirectory() / L"WeatherGenerate.hlsl", "ProbeWeatherSampler",
        "cs_5_1");
    D3D12_COMPUTE_PIPELINE_STATE_DESC probePsoDesc{};
    probePsoDesc.pRootSignature = probeRootSignature_.Get();
    probePsoDesc.CS = { probeShader->GetBufferPointer(),
                        probeShader->GetBufferSize() };
    probePsoDesc.NodeMask = 0;
    probePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ThrowIfFailed(renderer_->Device()->CreateComputePipelineState(
                      &probePsoDesc, IID_PPV_ARGS(&probePipelineState_)),
                  "CreateComputePipelineState (weather probe)");
    Log("weather map sampler probe pipeline initialized");
}

void WeatherMap::Generate(uint32_t seed)
{
    if (!renderer_ || !pipelineState_ || !texture_)
        Fail("WeatherMap::Generate called before initialization");

    seed_ = seed;
    const auto start = std::chrono::steady_clock::now();
    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            Transition(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            list->SetComputeRootSignature(rootSignature_.Get());
            list->SetPipelineState(pipelineState_.Get());
            list->SetComputeRootDescriptorTable(
                0, renderer_->SrvGpuHandle(kUavSlot));
            const UINT values[4] = { kResolution, seed_, 0u, 0u };
            list->SetComputeRoot32BitConstants(1, 4, values, 0);
            list->Dispatch(DispatchGroupCount(kResolution, kThreadGroupX),
                           DispatchGroupCount(kResolution, kThreadGroupY), 1);
            InsertUavBarrier(list, texture_.Get());
            Transition(list, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        },
        "weather map generation");

    const auto end = std::chrono::steady_clock::now();
    lastGenerateMs_ = std::chrono::duration<double, std::milli>(end - start).count();
    Log("weather map generated: %ux%u %s, seed %u, %.3f ms wall",
        kResolution, kResolution, FormatName(), seed_, lastGenerateMs_);
}

std::vector<float> WeatherMap::ReadbackRgba()
{
    if (!renderer_ || !texture_)
        Fail("WeatherMap::ReadbackRgba called before initialization");

    D3D12_RESOURCE_DESC textureDesc = texture_->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rowCount = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    renderer_->Device()->GetCopyableFootprints(
        &textureDesc, 0, 1, 0, &footprint, &rowCount, &rowSize, &totalBytes);

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    ComPtr<ID3D12Resource> readback;
    ThrowIfFailed(renderer_->Device()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&readback)),
                  "CreateCommittedResource (weather readback)");

    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            Transition(list, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = readback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = footprint;

            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = texture_.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = 0;
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            Transition(list, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        },
        "weather map readback");

    std::vector<float> result(
        static_cast<size_t>(kResolution) * kResolution * 4, 0.0f);
    D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(totalBytes) };
    void* mapped = nullptr;
    ThrowIfFailed(readback->Map(0, &readRange, &mapped),
                  "Map weather readback");
    const auto* bytes = static_cast<const uint8_t*>(mapped);
    const UINT rowPitch = footprint.Footprint.RowPitch;

    for (UINT y = 0; y < kResolution; ++y)
    {
        const uint8_t* row = bytes + static_cast<size_t>(y) * rowPitch;
        for (UINT x = 0; x < kResolution; ++x)
        {
            const size_t dst = (static_cast<size_t>(y) * kResolution + x) * 4;
            if (format_ == DXGI_FORMAT_R16G16B16A16_FLOAT)
            {
                const auto* halfValues =
                    reinterpret_cast<const uint16_t*>(row + x * 8);
                for (UINT c = 0; c < 4; ++c)
                {
                    result[dst + c] =
                        DirectX::PackedVector::XMConvertHalfToFloat(
                            halfValues[c]);
                }
            }
            else
            {
                const uint8_t* rgba = row + static_cast<size_t>(x) * 4;
                for (UINT c = 0; c < 4; ++c)
                    result[dst + c] = static_cast<float>(rgba[c]) / 255.0f;
            }
        }
    }

    const D3D12_RANGE writtenRange{ 0, 0 };
    readback->Unmap(0, &writtenRange);
    return result;
}

D3D12_GPU_DESCRIPTOR_HANDLE WeatherMap::SrvGpuHandle() const
{
    if (!renderer_)
        return {};
    return renderer_->SrvGpuHandle(kSrvSlot);
}

WeatherMap::SamplerWrapProbe WeatherMap::ProbeSamplerWrap()
{
    SamplerWrapProbe result{};
    if (!renderer_ || !texture_ || !probeBuffer_ || !probePipelineState_)
        return result;

    const UINT probeBytes = 16 * sizeof(float) * 4;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = probeBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    ComPtr<ID3D12Resource> readback;
    ThrowIfFailed(renderer_->Device()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&readback)),
                  "CreateCommittedResource (weather sampler probe)");

    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            Transition(list, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
            TransitionResource(list, probeBuffer_.Get(), probeState_,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            probeState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

            list->SetComputeRootSignature(probeRootSignature_.Get());
            list->SetPipelineState(probePipelineState_.Get());
            list->SetComputeRootDescriptorTable(
                0, renderer_->SrvGpuHandle(kSrvSlot));
            list->SetComputeRootDescriptorTable(
                1, renderer_->SrvGpuHandle(kProbeUavSlot));
            list->Dispatch(1, 1, 1);
            InsertUavBarrier(list, probeBuffer_.Get());
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
            probeState_ = D3D12_RESOURCE_STATE_COPY_SOURCE;
            list->CopyBufferRegion(readback.Get(), 0, probeBuffer_.Get(), 0,
                                   probeBytes);
        },
        "weather sampler seam probe");

    D3D12_RANGE readRange{ 0, probeBytes };
    void* mapped = nullptr;
    ThrowIfFailed(readback->Map(0, &readRange, &mapped),
                  "Map weather sampler probe");
    const auto* values = static_cast<const float*>(mapped);
    float maximum = 0.0f;
    for (UINT i = 0; i < 16 * 4; ++i)
        maximum = std::max(maximum, std::fabs(values[i]));
    const D3D12_RANGE writtenRange{ 0, 0 };
    readback->Unmap(0, &writtenRange);
    result.maxWrapDifference = maximum;
    result.maxRgbaDifference = maximum;
    return result;
}

const char* WeatherMap::FormatName() const
{
    return FormatText(format_);
}

void WeatherMap::Transition(ID3D12GraphicsCommandList* list,
                            D3D12_RESOURCE_STATES after)
{
    if (state_ == after)
        return;
    TransitionResource(list, texture_.Get(), state_, after);
    state_ = after;
}

void WeatherMap::Shutdown()
{
#if defined(OGSR_CLOUDS_FAULT_INJECTION)
    if (false)
        clouds::MaybeActivateBlockedPipes("weather-shutdown");
#endif
    if (!renderer_ && !texture_ && !pipelineState_ && !rootSignature_)
        return;

    clouds::LifetimeWitness ownerWitness(clouds::WitnessCode::ArmWeatherMapOwners);

    if (renderer_ && renderer_->Device())
    {
        if (renderer_->DeviceRemoved())
        {
            TerminalFailClosed(
                "weather map shutdown: device removed; GPU completion is "
                "UNKNOWN; TERMINAL fail-closed before releasing the texture or "
                "PSOs");
        }
        if (!renderer_->TryWaitForGpu())
        {
            TerminalFailClosed(
                "weather map shutdown: GPU completion is UNKNOWN; TERMINAL "
                "fail-closed before releasing the texture or PSOs");
        }
    }

    texture_.Reset();
    probeBuffer_.Reset();
    probePipelineState_.Reset();
    probeRootSignature_.Reset();
    pipelineState_.Reset();
    rootSignature_.Reset();
    state_ = D3D12_RESOURCE_STATE_COMMON;
    probeState_ = D3D12_RESOURCE_STATE_COMMON;
    renderer_ = nullptr;
    initialized_ = false;
    LogBestEffort("weather map shutdown: complete");
}
