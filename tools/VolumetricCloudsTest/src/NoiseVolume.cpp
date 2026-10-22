#include "NoiseVolume.h"

#include "Renderer.h"

#include <DirectXPackedVector.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <sstream>

namespace
{
constexpr UINT kThreadGroupX = 8;
constexpr UINT kThreadGroupY = 8;
constexpr UINT kThreadGroupZ = 4;

const char* FormatText(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
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
        D3D12_FORMAT_SUPPORT1_TEXTURE3D |
        D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW |
        D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;

    const D3D12_FORMAT_SUPPORT2 required2 =
        D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;

    return (support.Support1 & required1) == required1 &&
           (support.Support2 & required2) == required2;
}

uint32_t WrapIndex(int32_t index, uint32_t size)
{
    int32_t wrapped = index % static_cast<int32_t>(size);
    wrapped = (wrapped + static_cast<int32_t>(size)) % static_cast<int32_t>(size);
    return static_cast<uint32_t>(wrapped);
}

float CpuTexel(const std::vector<float>& rgba, uint32_t resolution,
               uint32_t x, uint32_t y, uint32_t z, uint32_t channel)
{
    const size_t index =
        ((static_cast<size_t>(z) * resolution + y) * resolution + x) * 4 + channel;
    return rgba[index];
}

// Reference implementation of D3D linear filtering with either WRAP or CLAMP
// addressing. It is used only by the audit probe; it mirrors the hardware rule
// that texel i is centered at (i + 0.5) / N.
float CpuTrilinear(const std::vector<float>& rgba, uint32_t resolution,
                   const float position[3], uint32_t channel, bool wrap)
{
    float q[3];
    int32_t base[3];
    float fraction[3];
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        q[axis] = position[axis] * static_cast<float>(resolution) - 0.5f;
        base[axis] = static_cast<int32_t>(std::floor(q[axis]));
        fraction[axis] = q[axis] - static_cast<float>(base[axis]);
    }

    float result = 0.0f;
    for (int32_t dz = 0; dz <= 1; ++dz)
    {
        for (int32_t dy = 0; dy <= 1; ++dy)
        {
            for (int32_t dx = 0; dx <= 1; ++dx)
            {
                auto address = [&](int32_t value) -> uint32_t
                {
                    if (wrap)
                        return WrapIndex(value, resolution);
                    return static_cast<uint32_t>(
                        std::clamp(value, 0, static_cast<int32_t>(resolution) - 1));
                };

                const uint32_t x = address(base[0] + dx);
                const uint32_t y = address(base[1] + dy);
                const uint32_t z = address(base[2] + dz);

                const float wx = dx ? fraction[0] : 1.0f - fraction[0];
                const float wy = dy ? fraction[1] : 1.0f - fraction[1];
                const float wz = dz ? fraction[2] : 1.0f - fraction[2];
                result += CpuTexel(rgba, resolution, x, y, z, channel) * wx * wy * wz;
            }
        }
    }
    return result;
}
} // namespace

NoiseVolume::NoiseVolume() = default;

NoiseVolume::~NoiseVolume()
{
    Shutdown();
}

void NoiseVolume::Initialize(Renderer& renderer, const NoiseParams& params,
                             DXGI_FORMAT preferredFormat)
{
    if (initialized_)
        return;

    const std::string validation = params.Validate();
    if (!validation.empty())
        Fail("invalid noise parameters: " + validation);

    renderer_ = &renderer;
    params_ = params;
    SelectFormat(preferredFormat);
    CreatePipeline();
    CreateResource(params_.resolution);
    CreateProbeBuffer();

    Log("noise volume format: %s (requested preference: %s, default order: 16F then 8-bit UNORM)",
        FormatName(),
        preferredFormat == DXGI_FORMAT_UNKNOWN ? "auto" : FormatText(preferredFormat));
    Log("noise volume initialized: %s", params_.Describe().c_str());

    Generate(params_);
    initialized_ = true;
}

void NoiseVolume::SelectFormat(DXGI_FORMAT preferredFormat)
{
    DXGI_FORMAT candidates[2]{};
    UINT candidateCount = 0;
    if (preferredFormat != DXGI_FORMAT_UNKNOWN)
    {
        candidates[candidateCount++] = preferredFormat;
    }
    else
    {
        candidates[candidateCount++] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        candidates[candidateCount++] = DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    for (UINT i = 0; i < candidateCount; ++i)
    {
        const DXGI_FORMAT candidate = candidates[i];
        const bool usable = IsFormatUsable(renderer_->Device(), candidate);
        Log("3D UAV format %s: %s", FormatText(candidate), usable ? "supported" : "unsupported");
        if (usable)
        {
            format_ = candidate;
            return;
        }
    }

    Fail("the requested 3D UAV format is not supported by this device");
}

void NoiseVolume::CreatePipeline()
{
    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE probeRange{};
    probeRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    probeRange.NumDescriptors = 1;
    probeRange.BaseShaderRegister = 1;
    probeRange.RegisterSpace = 0;
    probeRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER parameters[4]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.ShaderRegister = 0;
    parameters[0].Constants.RegisterSpace = 0;
    parameters[0].Constants.Num32BitValues = sizeof(NoiseConstants) / sizeof(uint32_t);
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &uavRange;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &probeRange;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[3].DescriptorTable.NumDescriptorRanges = 1;
    parameters[3].DescriptorTable.pDescriptorRanges = &srvRange;
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 4;
    rootDesc.pParameters = parameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
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
            LogError("compute root signature error: %s",
                     static_cast<const char*>(errors->GetBufferPointer()));
        }
        ThrowIfFailed(hr, "D3D12SerializeRootSignature (noise)");
    }

    ThrowIfFailed(renderer_->Device()->CreateRootSignature(
                      0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature (noise)");

    const auto shader = CompileShaderFile(
        ShaderDirectory() / L"NoiseGenerate.hlsl", "GenerateNoise", "cs_5_1");

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.CS = { shader->GetBufferPointer(), shader->GetBufferSize() };
    psoDesc.NodeMask = 0;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ThrowIfFailed(renderer_->Device()->CreateComputePipelineState(
                      &psoDesc, IID_PPV_ARGS(&computePso_)),
                  "CreateComputePipelineState (noise)");

    const auto probeShader = CompileShaderFile(
        ShaderDirectory() / L"NoiseGenerate.hlsl", "ProbePeriodicity", "cs_5_1");
    psoDesc.CS = { probeShader->GetBufferPointer(), probeShader->GetBufferSize() };
    ThrowIfFailed(renderer_->Device()->CreateComputePipelineState(
                      &psoDesc, IID_PPV_ARGS(&probePso_)),
                  "CreateComputePipelineState (periodicity probe)");

    const auto samplerProbeShader = CompileShaderFile(
        ShaderDirectory() / L"NoiseGenerate.hlsl", "ProbeSamplerWrap", "cs_5_1");
    psoDesc.CS = { samplerProbeShader->GetBufferPointer(),
                   samplerProbeShader->GetBufferSize() };
    ThrowIfFailed(renderer_->Device()->CreateComputePipelineState(
                      &psoDesc, IID_PPV_ARGS(&samplerProbePso_)),
                  "CreateComputePipelineState (sampler wrap probe)");
}

void NoiseVolume::CreateResource(uint32_t resolution)
{
    const uint64_t voxels = static_cast<uint64_t>(resolution) * resolution * resolution;
    const uint64_t bytesPerVoxel = format_ == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4;
    Log("allocating %u^3 noise volume (%llu MiB, %s)",
        resolution, static_cast<unsigned long long>(voxels * bytesPerVoxel / (1024 * 1024)),
        FormatName());

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Alignment = 0;
    desc.Width = resolution;
    desc.Height = resolution;
    desc.DepthOrArraySize = static_cast<UINT16>(resolution);
    desc.MipLevels = 1;
    desc.Format = format_;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ThrowIfFailed(renderer_->Device()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                      IID_PPV_ARGS(&volume_)),
                  "CreateCommittedResource (noise volume)");

    state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    WriteDescriptors();
}

void NoiseVolume::WriteDescriptors()
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = format_;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uav.Texture3D.MipSlice = 0;
    uav.Texture3D.FirstWSlice = 0;
    uav.Texture3D.WSize = params_.resolution;
    renderer_->Device()->CreateUnorderedAccessView(
        volume_.Get(), nullptr, &uav, renderer_->SrvCpuHandle(kVolumeUavSlot));

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = format_;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture3D.MostDetailedMip = 0;
    srv.Texture3D.MipLevels = 1;
    srv.Texture3D.ResourceMinLODClamp = 0.0f;
    renderer_->Device()->CreateShaderResourceView(
        volume_.Get(), &srv, renderer_->SrvCpuHandle(kVolumeSrvSlot));
}

void NoiseVolume::CreateProbeBuffer()
{
    constexpr UINT kProbeCapacity = 256;
    constexpr UINT kProbeStride = sizeof(float) * 4;
    const UINT64 bytes = static_cast<UINT64>(kProbeCapacity) * kProbeStride;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
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
                  "CreateCommittedResource (periodicity probe)");
    probeState_ = D3D12_RESOURCE_STATE_COMMON;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.FirstElement = 0;
    uav.Buffer.NumElements = kProbeCapacity;
    uav.Buffer.StructureByteStride = kProbeStride;
    uav.Buffer.CounterOffsetInBytes = 0;
    renderer_->Device()->CreateUnorderedAccessView(
        probeBuffer_.Get(), nullptr, &uav,
        renderer_->SrvCpuHandle(kProbeUavSlot));
}

void NoiseVolume::ResizeVolume(uint32_t resolution)
{
    if (!renderer_)
        Fail("ResizeVolume called before Initialize");

    renderer_->WaitForGpu();
    volume_.Reset();
    params_.resolution = resolution;
    CreateResource(resolution);
    Log("noise volume recreated at %u^3; call Generate() next", resolution);
}

void NoiseVolume::Generate(const NoiseParams& params)
{
    if (!renderer_ || !computePso_ || !rootSignature_ || !volume_)
        Fail("Generate called before the noise volume was initialized");

    const std::string validation = params.Validate();
    if (!validation.empty())
        Fail("invalid noise parameters: " + validation);
    if (params.resolution != params_.resolution)
        Fail("Generate resolution does not match the allocated volume");

    const NoiseConstants constants = NoiseConstants::From(params);

    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            if (state_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            {
                TransitionResource(list, volume_.Get(), state_,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }

            list->SetComputeRootSignature(rootSignature_.Get());
            list->SetPipelineState(computePso_.Get());
            list->SetComputeRoot32BitConstants(
                0, sizeof(NoiseConstants) / sizeof(uint32_t), &constants, 0);
            list->SetComputeRootDescriptorTable(
                1, renderer_->SrvGpuHandle(kVolumeUavSlot));

            const UINT groupsX = DispatchGroupCount(params.resolution, kThreadGroupX);
            const UINT groupsY = DispatchGroupCount(params.resolution, kThreadGroupY);
            const UINT groupsZ = DispatchGroupCount(params.resolution, kThreadGroupZ);
            list->Dispatch(groupsX, groupsY, groupsZ);

            InsertUavBarrier(list, volume_.Get());
            TransitionResource(list, volume_.Get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
            state_ = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
        },
        "noise generation");

    params_ = params;
    Log("noise generated: %s", params_.Describe().c_str());
}

std::vector<float> NoiseVolume::ReadbackRgba()
{
    if (!renderer_ || !volume_)
        Fail("ReadbackRgba called before the noise volume was initialized");

    const uint32_t resolution = params_.resolution;
    const size_t voxelCount = static_cast<size_t>(resolution) * resolution * resolution;
    std::vector<float> result(voxelCount * 4, 0.0f);

    D3D12_RESOURCE_DESC textureDesc = volume_->GetDesc();
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
                  "CreateCommittedResource (noise readback)");

    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            TransitionResource(list, volume_.Get(), state_,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = readback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = footprint;

            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = volume_.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = 0;

            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            TransitionResource(list, volume_.Get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE, state_);
        },
        "noise readback");

    D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(totalBytes) };
    void* mapped = nullptr;
    ThrowIfFailed(readback->Map(0, &readRange, &mapped),
                  "Map noise readback buffer");
    const auto* bytes = static_cast<const uint8_t*>(mapped);
    const UINT rowPitch = footprint.Footprint.RowPitch;

    for (uint32_t z = 0; z < resolution; ++z)
    {
        const uint8_t* slice = bytes + static_cast<size_t>(z) * rowPitch * rowCount;
        for (uint32_t y = 0; y < resolution; ++y)
        {
            const uint8_t* row = slice + static_cast<size_t>(y) * rowPitch;
            for (uint32_t x = 0; x < resolution; ++x)
            {
                const size_t dst = ((static_cast<size_t>(z) * resolution + y) *
                                    resolution + x) * 4;
                if (format_ == DXGI_FORMAT_R16G16B16A16_FLOAT)
                {
                    const auto* halfValues = reinterpret_cast<const uint16_t*>(
                        row + static_cast<size_t>(x) * 8);
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        result[dst + c] = DirectX::PackedVector::XMConvertHalfToFloat(
                            halfValues[c]);
                    }
                }
                else
                {
                    const uint8_t* rgba = row + static_cast<size_t>(x) * 4;
                    for (uint32_t c = 0; c < 4; ++c)
                        result[dst + c] = static_cast<float>(rgba[c]) / 255.0f;
                }
            }
        }
    }

    const D3D12_RANGE writtenRange{ 0, 0 };
    readback->Unmap(0, &writtenRange);

    Log("noise readback complete: %u^3 %s (%llu bytes)",
        resolution, FormatName(), static_cast<unsigned long long>(totalBytes));
    return result;
}

NoiseVolume::PeriodicityProbe NoiseVolume::ProbePeriodicity()
{
    if (!renderer_ || !probePso_ || !probeBuffer_)
        Fail("ProbePeriodicity called before the noise volume was initialized");

    constexpr UINT kProbeCount = 64;
    constexpr UINT kProbeStride = sizeof(float) * 4;
    const UINT64 bytes = static_cast<UINT64>(kProbeCount) * kProbeStride;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = bytes;
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
                  "CreateCommittedResource (periodicity probe readback)");

    const NoiseConstants constants = NoiseConstants::From(params_);
    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            if (probeState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            {
                TransitionResource(list, probeBuffer_.Get(), probeState_,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                probeState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }

            list->SetComputeRootSignature(rootSignature_.Get());
            list->SetPipelineState(probePso_.Get());
            list->SetComputeRoot32BitConstants(
                0, sizeof(NoiseConstants) / sizeof(uint32_t), &constants, 0);
            list->SetComputeRootDescriptorTable(
                2, renderer_->SrvGpuHandle(kProbeUavSlot));
            list->Dispatch(1, 1, 1);

            InsertUavBarrier(list, probeBuffer_.Get());
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyBufferRegion(readback.Get(), 0, probeBuffer_.Get(), 0, bytes);
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            probeState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        },
        "periodicity probe");

    D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(bytes) };
    void* mapped = nullptr;
    ThrowIfFailed(readback->Map(0, &readRange, &mapped),
                  "Map periodicity probe readback");
    const auto* values = static_cast<const float*>(mapped);
    PeriodicityProbe probe{};
    for (UINT i = 0; i < kProbeCount; ++i)
    {
        for (UINT axis = 0; axis < 3; ++axis)
            probe.maxDifference[axis] =
                std::max(probe.maxDifference[axis], values[i * 4 + axis]);
        probe.negativeControl =
            std::max(probe.negativeControl, values[i * 4 + 3]);
    }
    const D3D12_RANGE writtenRange{ 0, 0 };
    readback->Unmap(0, &writtenRange);

    Log("periodicity probe max |f(p)-f(p+axis)|: X=%.9g Y=%.9g Z=%.9g; negative control=%.6g",
        probe.maxDifference[0], probe.maxDifference[1], probe.maxDifference[2],
        probe.negativeControl);
    return probe;
}

NoiseVolume::SamplerWrapProbe NoiseVolume::ProbeSamplerWrap(
    const std::vector<float>& rgba)
{
    if (!renderer_ || !samplerProbePso_ || !probeBuffer_ || !volume_)
        Fail("ProbeSamplerWrap called before the noise volume was initialized");
    if (rgba.size() != static_cast<size_t>(params_.resolution) *
                            params_.resolution * params_.resolution * 4)
        Fail("ProbeSamplerWrap received an invalid readback buffer");

    constexpr UINT kSampleCount = 128; // 64 probes * 2 samples per axis crossing
    constexpr UINT kSampleStride = sizeof(float) * 4;
    const UINT64 bytes = static_cast<UINT64>(kSampleCount) * kSampleStride;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = bytes;
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
                  "CreateCommittedResource (sampler wrap probe readback)");

    const NoiseConstants constants = NoiseConstants::From(params_);
    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            if (state_ != D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE)
            {
                TransitionResource(list, volume_.Get(), state_,
                                   D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
                state_ = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            }

            list->SetComputeRootSignature(rootSignature_.Get());
            list->SetPipelineState(samplerProbePso_.Get());
            list->SetComputeRoot32BitConstants(
                0, sizeof(NoiseConstants) / sizeof(uint32_t), &constants, 0);
            list->SetComputeRootDescriptorTable(
                2, renderer_->SrvGpuHandle(kProbeUavSlot));
            list->SetComputeRootDescriptorTable(
                3, renderer_->SrvGpuHandle(kVolumeSrvSlot));
            list->Dispatch(1, 1, 1);

            InsertUavBarrier(list, probeBuffer_.Get());
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyBufferRegion(readback.Get(), 0, probeBuffer_.Get(), 0, bytes);
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            probeState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        },
        "sampler WRAP probe");

    D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(bytes) };
    void* mapped = nullptr;
    ThrowIfFailed(readback->Map(0, &readRange, &mapped),
                  "Map sampler wrap probe readback");
    const auto* values = static_cast<const float*>(mapped);

    SamplerWrapProbe result{};
    const uint32_t resolution = params_.resolution;
    for (UINT id = 0; id < 64; ++id)
    {
        const UINT axis = id % 3;
        const UINT sampleIndex = id / 3;
        const float t = static_cast<float>(sampleIndex) / 21.0f;
        const float seam = (0.05f + 0.4f * t) / static_cast<float>(resolution);

        float uv[3] = {
            static_cast<float>((id * 13u) & 63u) / 64.0f,
            static_cast<float>((id * 29u) & 63u) / 64.0f,
            static_cast<float>((id * 47u) & 63u) / 64.0f,
        };
        uv[axis] = seam;

        float uvWrapped[3] = { uv[0], uv[1], uv[2] };
        uvWrapped[axis] += 1.0f;

        const float* gpuA = values + id * 2 * 4;
        const float* gpuB = values + (id * 2 + 1) * 4;

        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            const float expectedA = CpuTrilinear(rgba, resolution, uv, channel, true);
            const float expectedB = CpuTrilinear(rgba, resolution, uvWrapped, channel, true);
            const float expectedClampA = CpuTrilinear(rgba, resolution, uv, channel, false);
            const float expectedClampB = CpuTrilinear(rgba, resolution, uvWrapped, channel, false);

            result.maxFilterError = std::max(
                result.maxFilterError,
                std::max(std::fabs(gpuA[channel] - expectedA),
                         std::fabs(gpuB[channel] - expectedB)));
            result.maxWrapDifference = std::max(
                result.maxWrapDifference,
                std::fabs(gpuA[channel] - gpuB[channel]));
            result.maxClampDifference = std::max(
                result.maxClampDifference,
                std::max(std::fabs(gpuA[channel] - expectedClampA),
                         std::fabs(gpuB[channel] - expectedClampB)));
        }
    }

    const D3D12_RANGE writtenRange{ 0, 0 };
    readback->Unmap(0, &writtenRange);

    Log("sampler WRAP probe: filter error=%.6g, wrap difference=%.6g, clamp reference difference=%.6g",
        result.maxFilterError, result.maxWrapDifference, result.maxClampDifference);
    return result;
}

D3D12_GPU_DESCRIPTOR_HANDLE NoiseVolume::SrvGpuHandle() const
{
    return renderer_->SrvGpuHandle(kVolumeSrvSlot);
}

D3D12_CPU_DESCRIPTOR_HANDLE NoiseVolume::SrvCpuHandle() const
{
    return renderer_->SrvCpuHandle(kVolumeSrvSlot);
}

const char* NoiseVolume::FormatName() const
{
    return FormatText(format_);
}

void NoiseVolume::Shutdown()
{
    if (!renderer_ && !volume_ && !computePso_ && !rootSignature_ && !probeBuffer_)
        return;

    if (renderer_ && renderer_->Device())
    {
        bool gpuIdle = true;
        if (renderer_->DeviceRemoved())
        {
            gpuIdle = false;
            LogWarn("noise shutdown: device already removed; best-effort cleanup");
        }
        else
        {
            Log("noise shutdown: waiting for GPU");
            try
            {
                renderer_->WaitForGpu();
            }
            catch (const std::exception& e)
            {
                gpuIdle = false;
                LogError("noise volume shutdown wait failed: %s", e.what());
            }
        }

        if (gpuIdle)
            Log("noise shutdown: GPU idle");
        else
            LogWarn("noise shutdown: best-effort cleanup after unsuccessful GPU wait");
    }
    Log("noise shutdown: releasing volume");
    volume_.Reset();
    probeBuffer_.Reset();
    probePso_.Reset();
    samplerProbePso_.Reset();
    computePso_.Reset();
    rootSignature_.Reset();
    renderer_ = nullptr;
    initialized_ = false;
    Log("noise shutdown: complete");
}
