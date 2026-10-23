#include "CloudRayMarcher.h"

#include "CloudProfiler.h"
#include "Renderer.h"

#include <DirectXPackedVector.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace
{
constexpr UINT kThreadGroupX = 8;
constexpr UINT kThreadGroupY = 8;

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
        D3D12_FORMAT_SUPPORT1_TEXTURE2D |
        D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW |
        D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
    const D3D12_FORMAT_SUPPORT2 required2 =
        D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    return (support.Support1 & required1) == required1 &&
           (support.Support2 & required2) == required2;
}
} // namespace

CloudRayMarcher::CloudRayMarcher() = default;

CloudRayMarcher::~CloudRayMarcher()
{
    Shutdown();
}

void CloudRayMarcher::Initialize(Renderer& renderer, uint32_t swapWidth,
                                 uint32_t swapHeight)
{
    if (initialized_)
        return;

    renderer_ = &renderer;
    swapWidth_ = std::max(1u, swapWidth);
    swapHeight_ = std::max(1u, swapHeight);
    outputWidth_ = std::max(1u, swapWidth_ / 2);
    outputHeight_ = std::max(1u, swapHeight_ / 2);

    SelectFormat();
    CreatePipeline();
    CreateConstants();
    CreateTextures();
    initialized_ = true;

    Log("cloud ray marcher initialized: %ux%u output (%s), up to 128 steps",
        outputWidth_, outputHeight_, FormatName());
}

void CloudRayMarcher::SelectFormat()
{
    const DXGI_FORMAT candidates[] = {
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R8G8B8A8_UNORM,
    };
    for (DXGI_FORMAT candidate : candidates)
    {
        const bool usable = IsFormatUsable(renderer_->Device(), candidate);
        Log("cloud output format %s: %s", FormatText(candidate),
            usable ? "supported" : "unsupported");
        if (usable)
        {
            format_ = candidate;
            return;
        }
    }
    Fail("the device does not support a usable half-resolution cloud output format");
}

void CloudRayMarcher::CreatePipeline()
{
    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].Descriptor.RegisterSpace = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &uavRange;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &srvRange;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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
    rootDesc.NumParameters = 3;
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
            LogError("cloud root signature error: %s",
                     static_cast<const char*>(errors->GetBufferPointer()));
        }
        ThrowIfFailed(hr, "D3D12SerializeRootSignature (cloud)");
    }

    ThrowIfFailed(renderer_->Device()->CreateRootSignature(
                      0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature (cloud)");

    const auto shader = CompileShaderFile(
        ShaderDirectory() / L"CloudRayMarch.hlsl", "CloudRayMarch", "cs_5_1");

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.CS = { shader->GetBufferPointer(), shader->GetBufferSize() };
    psoDesc.NodeMask = 0;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ThrowIfFailed(renderer_->Device()->CreateComputePipelineState(
                      &psoDesc, IID_PPV_ARGS(&pipelineState_)),
                  "CreateComputePipelineState (cloud)");

    Log("cloud ray marching pipeline initialized");
}

void CloudRayMarcher::CreateConstants()
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = CloudProfiler::kReadbackStride;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    for (UINT i = 0; i < kFrameCount; ++i)
    {
        ThrowIfFailed(renderer_->Device()->CreateCommittedResource(
                          &heap, D3D12_HEAP_FLAG_NONE, &desc,
                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                          IID_PPV_ARGS(&constantBuffers_[i])),
                      "CreateCommittedResource (cloud constants)");
        const D3D12_RANGE readRange{ 0, 0 };
        ThrowIfFailed(constantBuffers_[i]->Map(
                          0, &readRange,
                          reinterpret_cast<void**>(&mappedConstants_[i])),
                      "Map cloud constants");
        std::memset(mappedConstants_[i], 0, CloudProfiler::kReadbackStride);
    }
}

void CloudRayMarcher::CreateTextures()
{
    for (auto& texture : textures_)
        texture.Reset();
    for (auto& state : states_)
        state = D3D12_RESOURCE_STATE_COMMON;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = outputWidth_;
    desc.Height = outputHeight_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format_;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    for (UINT i = 0; i < kFrameCount; ++i)
    {
        ThrowIfFailed(renderer_->Device()->CreateCommittedResource(
                          &heap, D3D12_HEAP_FLAG_NONE, &desc,
                          D3D12_RESOURCE_STATE_COMMON, nullptr,
                          IID_PPV_ARGS(&textures_[i])),
                      "CreateCommittedResource (cloud output)");

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = format_;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Texture2D.MipSlice = 0;
        uav.Texture2D.PlaneSlice = 0;
        renderer_->Device()->CreateUnorderedAccessView(
            textures_[i].Get(), nullptr, &uav,
            renderer_->SrvCpuHandle(kUavSlotBase + i));

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = format_;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.PlaneSlice = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;
        renderer_->Device()->CreateShaderResourceView(
            textures_[i].Get(), &srv,
            renderer_->SrvCpuHandle(kSrvSlotBase + i));
    }

    Log("cloud output textures created: %ux%u x %u, %s",
        outputWidth_, outputHeight_, kFrameCount, FormatName());
}

void CloudRayMarcher::UploadConstants(
    uint32_t frameIndex, const clouds::CloudFrameConstants& constants)
{
    if (frameIndex >= kFrameCount || !mappedConstants_[frameIndex])
        Fail("cloud constants upload uses an invalid frame index");
    std::memcpy(mappedConstants_[frameIndex], &constants, sizeof(constants));
}

void CloudRayMarcher::TransitionCloud(
    ID3D12GraphicsCommandList* list, uint32_t frameIndex,
    D3D12_RESOURCE_STATES after)
{
    if (states_[frameIndex] == after)
        return;
    TransitionResource(list, textures_[frameIndex].Get(),
                       states_[frameIndex], after);
    states_[frameIndex] = after;
}

void CloudRayMarcher::Dispatch(
    ID3D12GraphicsCommandList* list, uint32_t frameIndex,
    D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv)
{
    list->SetComputeRootSignature(rootSignature_.Get());
    list->SetPipelineState(pipelineState_.Get());
    list->SetComputeRootConstantBufferView(0, ConstantsAddress(frameIndex));
    list->SetComputeRootDescriptorTable(
        1, renderer_->SrvGpuHandle(kUavSlotBase + frameIndex));
    list->SetComputeRootDescriptorTable(2, noiseSrv);

    const UINT groupsX = DispatchGroupCount(outputWidth_, kThreadGroupX);
    const UINT groupsY = DispatchGroupCount(outputHeight_, kThreadGroupY);
    list->Dispatch(groupsX, groupsY, 1);
}

void CloudRayMarcher::Render(
    ID3D12GraphicsCommandList* list, uint32_t frameIndex,
    const clouds::CloudFrameConstants& constants,
    D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv)
{
    if (!initialized_ || !list || frameIndex >= kFrameCount)
        Fail("CloudRayMarcher::Render called with invalid state");

    UploadConstants(frameIndex, constants);
    TransitionCloud(list, frameIndex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Dispatch(list, frameIndex, noiseSrv);
    InsertUavBarrier(list, textures_[frameIndex].Get());
    TransitionCloud(list, frameIndex,
                    D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}

CloudReadback CloudRayMarcher::Readback(
    const clouds::CloudFrameConstants& constants,
    D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv)
{
    if (!initialized_ || !renderer_)
        Fail("CloudRayMarcher::Readback called before initialization");

    UploadConstants(0, constants);

    D3D12_RESOURCE_DESC textureDesc = textures_[0]->GetDesc();
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
                  "CreateCommittedResource (cloud readback)");

    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            TransitionCloud(list, 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Dispatch(list, 0, noiseSrv);
            InsertUavBarrier(list, textures_[0].Get());
            TransitionCloud(list, 0, D3D12_RESOURCE_STATE_COPY_SOURCE);

            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = readback.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = footprint;

            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = textures_[0].Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = 0;
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

            TransitionCloud(list, 0, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        },
        "cloud selftest dispatch/readback");

    CloudReadback result{};
    result.width = outputWidth_;
    result.height = outputHeight_;
    result.rgba.resize(static_cast<size_t>(outputWidth_) * outputHeight_ * 4);

    D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(totalBytes) };
    void* mapped = nullptr;
    ThrowIfFailed(readback->Map(0, &readRange, &mapped),
                  "Map cloud readback");
    const auto* bytes = static_cast<const uint8_t*>(mapped);
    const UINT rowPitch = footprint.Footprint.RowPitch;

    for (uint32_t y = 0; y < outputHeight_; ++y)
    {
        const uint8_t* row = bytes + static_cast<size_t>(y) * rowPitch;
        for (uint32_t x = 0; x < outputWidth_; ++x)
        {
            const size_t dst =
                (static_cast<size_t>(y) * outputWidth_ + x) * 4;
            if (format_ == DXGI_FORMAT_R16G16B16A16_FLOAT)
            {
                const auto* halfValues = reinterpret_cast<const uint16_t*>(
                    row + static_cast<size_t>(x) * 8);
                for (uint32_t c = 0; c < 4; ++c)
                {
                    result.rgba[dst + c] =
                        DirectX::PackedVector::XMConvertHalfToFloat(
                            halfValues[c]);
                }
            }
            else
            {
                const uint8_t* rgba = row + static_cast<size_t>(x) * 4;
                for (uint32_t c = 0; c < 4; ++c)
                    result.rgba[dst + c] =
                        static_cast<float>(rgba[c]) / 255.0f;
            }
        }
    }

    const D3D12_RANGE writtenRange{ 0, 0 };
    readback->Unmap(0, &writtenRange);
    return result;
}

D3D12_GPU_VIRTUAL_ADDRESS CloudRayMarcher::ConstantsAddress(
    uint32_t frameIndex) const
{
    if (frameIndex >= kFrameCount || !constantBuffers_[frameIndex])
        return 0;
    return constantBuffers_[frameIndex]->GetGPUVirtualAddress();
}

D3D12_GPU_DESCRIPTOR_HANDLE CloudRayMarcher::SrvGpuHandle(
    uint32_t frameIndex) const
{
    if (!renderer_ || frameIndex >= kFrameCount)
        return {};
    return renderer_->SrvGpuHandle(kSrvSlotBase + frameIndex);
}

const char* CloudRayMarcher::FormatName() const
{
    return FormatText(format_);
}

void CloudRayMarcher::Resize(uint32_t swapWidth, uint32_t swapHeight)
{
    if (!initialized_)
        return;
    const uint32_t newWidth = std::max(1u, swapWidth / 2);
    const uint32_t newHeight = std::max(1u, swapHeight / 2);
    if (newWidth == outputWidth_ && newHeight == outputHeight_)
        return;

    renderer_->WaitForGpu();
    swapWidth_ = std::max(1u, swapWidth);
    swapHeight_ = std::max(1u, swapHeight);
    outputWidth_ = newWidth;
    outputHeight_ = newHeight;
    CreateTextures();
    Log("cloud output resized: %ux%u", outputWidth_, outputHeight_);
}

void CloudRayMarcher::Shutdown()
{
    if (!renderer_ && !pipelineState_ && !rootSignature_)
        return;

    if (renderer_ && renderer_->Device() && !renderer_->DeviceRemoved())
    {
        try
        {
            renderer_->WaitForGpu();
        }
        catch (const std::exception& e)
        {
            LogWarn("cloud shutdown GPU wait failed: %s", e.what());
        }
    }

    for (UINT i = 0; i < kFrameCount; ++i)
    {
        if (constantBuffers_[i] && mappedConstants_[i])
            constantBuffers_[i]->Unmap(0, nullptr);
        mappedConstants_[i] = nullptr;
        constantBuffers_[i].Reset();
        textures_[i].Reset();
        states_[i] = D3D12_RESOURCE_STATE_COMMON;
    }
    pipelineState_.Reset();
    rootSignature_.Reset();
    renderer_ = nullptr;
    initialized_ = false;
    Log("cloud ray marcher shutdown: complete");
}
