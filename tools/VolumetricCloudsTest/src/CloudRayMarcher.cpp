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
                                 uint32_t swapHeight, uint32_t outputDivisor)
{
    if (initialized_)
        return;

    renderer_ = &renderer;
    swapWidth_ = std::max(1u, swapWidth);
    swapHeight_ = std::max(1u, swapHeight);
    outputDivisor_ = std::clamp(outputDivisor, 1u, 4u);
    outputWidth_ = std::max(1u, swapWidth_ / outputDivisor_);
    outputHeight_ = std::max(1u, swapHeight_ / outputDivisor_);

    SelectFormat();
    CreatePipeline();
    CreateConstants();
    CreateTextures();
    CreateProbeBuffer();
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

    D3D12_DESCRIPTOR_RANGE weatherRange{};
    weatherRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    weatherRange.NumDescriptors = 1;
    weatherRange.BaseShaderRegister = 1;
    weatherRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE probeRange{};
    probeRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    probeRange.NumDescriptors = 1;
    probeRange.BaseShaderRegister = 1;
    probeRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER parameters[5]{};
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

    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[3].DescriptorTable.NumDescriptorRanges = 1;
    parameters[3].DescriptorTable.pDescriptorRanges = &weatherRange;
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[4].DescriptorTable.NumDescriptorRanges = 1;
    parameters[4].DescriptorTable.pDescriptorRanges = &probeRange;
    parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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
    rootDesc.NumParameters = 5;
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

    const auto createPipeline = [&](const wchar_t* file,
                                    ComPtr<ID3D12PipelineState>& state,
                                    const char* label)
    {
        const auto shader = CompileShaderFile(
            ShaderDirectory() / file, "CloudRayMarch", "cs_5_1");
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSignature_.Get();
        psoDesc.CS = { shader->GetBufferPointer(), shader->GetBufferSize() };
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        ThrowIfFailed(renderer_->Device()->CreateComputePipelineState(
                          &psoDesc, IID_PPV_ARGS(&state)),
                      label);
    };

    createPipeline(L"CloudRayMarch.hlsl", pipelineState_, 
                   "CreateComputePipelineState (cloud)");
    createPipeline(L"CloudRayMarchProbe.hlsl", pipelineStateProbe_,
                   "CreateComputePipelineState (cloud probe)");
    createPipeline(L"CloudRayMarchProbeFormulaF1.hlsl", pipelineStateProbeFormula_[0],
                   "CreateComputePipelineState (cloud probe formula F1)");
    createPipeline(L"CloudRayMarchProbeFormulaF2.hlsl", pipelineStateProbeFormula_[1],
                   "CreateComputePipelineState (cloud probe formula F2)");
    createPipeline(L"CloudRayMarchProbeFormulaF3.hlsl", pipelineStateProbeFormula_[2],
                   "CreateComputePipelineState (cloud probe formula F3)");
    createPipeline(L"CloudRayMarchProbeFormulaF4.hlsl", pipelineStateProbeFormula_[3],
                   "CreateComputePipelineState (cloud probe formula F4)");
    createPipeline(L"CloudRayMarchProbeFormulaF5.hlsl", pipelineStateProbeFormula_[4],
                   "CreateComputePipelineState (cloud probe formula F5)");
    createPipeline(L"CloudRayMarchProbePrototypeA.hlsl", pipelineStateProbeProtoA_,
                   "CreateComputePipelineState (cloud probe prototype A)");
    createPipeline(L"CloudRayMarchProbePrototypeB.hlsl", pipelineStateProbeProtoB_,
                   "CreateComputePipelineState (cloud probe prototype B)");
    createPipeline(L"CloudRayMarchReference.hlsl", pipelineStateReference_,
                   "CreateComputePipelineState (reference density)");
    createPipeline(L"CloudRayMarchReferenceA03a.hlsl",
                   pipelineStateReferenceA03a_,
                   "CreateComputePipelineState (reference density A03a)");
    createPipeline(L"CloudRayMarchReferenceA03b.hlsl",
                   pipelineStateReferenceA03b_,
                   "CreateComputePipelineState (reference density A03b)");
    createPipeline(L"CloudRayMarchProbeReference.hlsl", pipelineStateProbeReference_,
                   "CreateComputePipelineState (reference density probe)");
    createPipeline(L"CloudRayMarchProbeReferenceA03a.hlsl",
                   pipelineStateProbeReferenceA03a_,
                   "CreateComputePipelineState (reference density probe A03a)");
    createPipeline(L"CloudRayMarchProbeReferenceA03b.hlsl",
                   pipelineStateProbeReferenceA03b_,
                   "CreateComputePipelineState (reference density probe A03b)");

    const auto createPipelineEntry = [&](const wchar_t* file,
                                          const char* entry,
                                          ComPtr<ID3D12PipelineState>& state,
                                          const char* label)
    {
        const auto shader = CompileShaderFile(
            ShaderDirectory() / file, entry, "cs_5_1");
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSignature_.Get();
        psoDesc.CS = { shader->GetBufferPointer(), shader->GetBufferSize() };
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        ThrowIfFailed(renderer_->Device()->CreateComputePipelineState(
                          &psoDesc, IID_PPV_ARGS(&state)),
                      label);
    };
    createPipelineEntry(L"CloudDensityVolume.hlsl", "DensityVolume",
                        pipelineStateDensityVolume_,
                        "CreateComputePipelineState (density volume)");
    createPipelineEntry(L"CloudDensityVolumeA03a.hlsl", "DensityVolume",
                        pipelineStateDensityVolumeA03a_,
                        "CreateComputePipelineState (density volume A03a)");
    createPipelineEntry(L"CloudDensityVolumeA03a.hlsl", "StormBoundary",
                        pipelineStateStormBoundaryA03a_,
                        "CreateComputePipelineState (storm boundary A03a)");
    createPipelineEntry(L"CloudDensityVolumeA03a.hlsl", "DensityColumns",
                        pipelineStateDensityColumnsA03a_,
                        "CreateComputePipelineState (density columns A03a)");
    createPipelineEntry(L"CloudDensityVolumeA03b.hlsl", "DensityVolume",
                        pipelineStateDensityVolumeA03b_,
                        "CreateComputePipelineState (density volume A03b)");
    createPipelineEntry(L"CloudDensityVolumeA03b.hlsl", "StormBoundary",
                        pipelineStateStormBoundaryA03b_,
                        "CreateComputePipelineState (storm boundary A03b)");
    createPipelineEntry(L"CloudDensityVolumeA03b.hlsl", "DensityColumns",
                        pipelineStateDensityColumnsA03b_,
                        "CreateComputePipelineState (density columns A03b)");
    createPipelineEntry(L"CloudDensityVolume.hlsl", "StormBoundary",
                        pipelineStateStormBoundary_,
                        "CreateComputePipelineState (storm boundary)");
    createPipelineEntry(L"CloudDensityVolume.hlsl", "DensityColumns",
                        pipelineStateDensityColumns_,
                        "CreateComputePipelineState (density columns)");
    createPipeline(L"CloudRayMarchPrototypeA.hlsl", pipelineStateProtoA_,
                   "CreateComputePipelineState (cloud prototype A)");
    createPipeline(L"CloudRayMarchPrototypeB.hlsl", pipelineStateProtoB_,
                   "CreateComputePipelineState (cloud prototype B)");
    createPipeline(L"CloudRayMarchFormulaF2.hlsl", pipelineStateFormula_[1],
                   "CreateComputePipelineState (cloud formula F2)");
    createPipeline(L"CloudRayMarchFormulaF1.hlsl", pipelineStateFormula_[0],
                   "CreateComputePipelineState (cloud formula F1)");
    createPipeline(L"CloudRayMarchFormulaF3.hlsl", pipelineStateFormula_[2],
                   "CreateComputePipelineState (cloud formula F3)");
    createPipeline(L"CloudRayMarchFormulaF4.hlsl", pipelineStateFormula_[3],
                   "CreateComputePipelineState (cloud formula F4)");
    createPipeline(L"CloudRayMarchFormulaF5.hlsl", pipelineStateFormula_[4],
                   "CreateComputePipelineState (cloud formula F5)");

    Log("cloud ray marching pipeline initialized");
}

void CloudRayMarcher::CreateConstants()
{
    // The cloud constant buffer used to reuse CloudProfiler::kReadbackStride
    // (256 bytes). V2.3 added Lighting/Atmosphere/LightingFlags, so the size
    // must follow the struct or GBV reports a CBV out-of-bounds read.
    constexpr UINT kConstantBufferSize =
        (static_cast<UINT>(sizeof(clouds::CloudFrameConstants)) + 255u) &
        ~255u;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kConstantBufferSize;
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
        std::memset(mappedConstants_[i], 0, kConstantBufferSize);
    }
}

void CloudRayMarcher::CreateTextures()
{
    for (auto& texture : textures_)
        texture.Reset();
    for (auto& state : states_)
        state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

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
                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
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

void CloudRayMarcher::CreateProbeBuffer()
{
    probeBuffer_.Reset();
    probeCapacityRows_ = std::max(1u, outputHeight_);
    const uint32_t normalRecords =
        probeCapacityRows_ * kProbeMaxSteps * 3u;
    probeCapacityRecords_ = std::max(probeCapacityRecords_, normalRecords);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = static_cast<UINT64>(probeCapacityRecords_) * 16ull;
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
                  "CreateCommittedResource (cloud probe)");

    // Buffers are effectively created in COMMON. Transition explicitly so the
    // first probe dispatch is valid and GBV does not report warning #1328.
    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_COMMON,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        },
        "cloud probe initial transition");

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.FirstElement = 0;
    uav.Buffer.NumElements = probeCapacityRecords_;
    uav.Buffer.StructureByteStride = 16;
    uav.Buffer.CounterOffsetInBytes = 0;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    renderer_->Device()->CreateUnorderedAccessView(
        probeBuffer_.Get(), nullptr, &uav,
        renderer_->SrvCpuHandle(kProbeUavSlot));

    Log("cloud probe buffer created: %.2f MB (%u records, %u rows x %u "
        "steps x 3)",
        static_cast<double>(desc.Width) / (1024.0 * 1024.0),
        probeCapacityRecords_, probeCapacityRows_, kProbeMaxSteps);
}

void CloudRayMarcher::EnsureProbeCapacity(uint32_t records)
{
    if (records == 0 || records <= probeCapacityRecords_)
        return;
    if (!initialized_ || !renderer_)
        Fail("EnsureProbeCapacity called before initialization");
    renderer_->WaitForGpu();
    probeCapacityRecords_ = records;
    CreateProbeBuffer();
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
    D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE weatherSrv,
    bool probe,
    clouds::ShapePrototype prototype,
    clouds::CloudFormula formula,
    clouds::CloudDensityModel densityModel,
    bool a03a, bool a03b)
{
    list->SetComputeRootSignature(rootSignature_.Get());
    ID3D12PipelineState* pipeline = nullptr;
    const char* psoName = nullptr;

    if (densityModel == clouds::CloudDensityModel::Reference)
    {
        // Reference has its own render/probe permutation. Never fall back to
        // legacy or to an F0 probe silently. A03a uses separate shader
        // permutations so the accepted A03 bytecode is not perturbed.
        if (a03b)
        {
            pipeline = probe ? pipelineStateProbeReferenceA03b_.Get()
                             : pipelineStateReferenceA03b_.Get();
            psoName = probe
                ? "CloudRayMarchProbeReferenceA03b.hlsl"
                : "CloudRayMarchReferenceA03b.hlsl";
        }
        else if (a03a)
        {
            pipeline = probe ? pipelineStateProbeReferenceA03a_.Get()
                             : pipelineStateReferenceA03a_.Get();
            psoName = probe
                ? "CloudRayMarchProbeReferenceA03a.hlsl"
                : "CloudRayMarchReferenceA03a.hlsl";
        }
        else
        {
            pipeline = probe ? pipelineStateProbeReference_.Get()
                             : pipelineStateReference_.Get();
            psoName = probe ? "CloudRayMarchProbeReference.hlsl"
                            : "CloudRayMarchReference.hlsl";
        }
        if (!pipeline)
            Fail("reference density PSO is unavailable");
    }
    else if (probe)
    {
        // Probe mode must use the same formula/shape permutation as the
        // render path. The old unconditional probe PSO silently sampled F0.
        const char* variant = "CloudRayMarchProbe.hlsl (F0)";
        if (prototype == clouds::ShapePrototype::A)
        {
            pipeline = pipelineStateProbeProtoA_.Get();
            variant = "CloudRayMarchProbePrototypeA.hlsl";
        }
        else if (prototype == clouds::ShapePrototype::B)
        {
            pipeline = pipelineStateProbeProtoB_.Get();
            variant = "CloudRayMarchProbePrototypeB.hlsl";
        }
        else if (formula != clouds::CloudFormula::None)
        {
            const uint32_t index = static_cast<uint32_t>(formula) - 1u;
            if (index >= 5u || !pipelineStateProbeFormula_[index])
                Fail("requested probe formula shader was not compiled");
            pipeline = pipelineStateProbeFormula_[index].Get();
            static const char* kProbeFormulaNames[5] = {
                "CloudRayMarchProbeFormulaF1.hlsl",
                "CloudRayMarchProbeFormulaF2.hlsl",
                "CloudRayMarchProbeFormulaF3.hlsl",
                "CloudRayMarchProbeFormulaF4.hlsl",
                "CloudRayMarchProbeFormulaF5.hlsl"
            };
            variant = kProbeFormulaNames[index];
        }
        else
        {
            pipeline = pipelineStateProbe_.Get();
        }
        if (!pipeline)
            Fail("selected cloud probe PSO is unavailable");
        psoName = variant;
    }
    else if (!probe)
    {
        psoName = "CloudRayMarch.hlsl";
        pipeline = pipelineState_.Get();
        if (prototype == clouds::ShapePrototype::A)
        {
            pipeline = pipelineStateProtoA_.Get();
            psoName = "CloudRayMarchPrototypeA.hlsl";
        }
        else if (prototype == clouds::ShapePrototype::B)
        {
            pipeline = pipelineStateProtoB_.Get();
            psoName = "CloudRayMarchPrototypeB.hlsl";
        }
        else if (formula != clouds::CloudFormula::None)
        {
            const uint32_t index = static_cast<uint32_t>(formula) - 1u;
            if (index >= 5u || !pipelineStateFormula_[index])
                Fail("requested cloud formula shader was not compiled");
            pipeline = pipelineStateFormula_[index].Get();
            static const char* kRenderFormulaNames[5] = {
                "CloudRayMarchFormulaF1.hlsl",
                "CloudRayMarchFormulaF2.hlsl",
                "CloudRayMarchFormulaF3.hlsl",
                "CloudRayMarchFormulaF4.hlsl",
                "CloudRayMarchFormulaF5.hlsl"
            };
            psoName = kRenderFormulaNames[index];
        }
    }
    if (!pipeline || !psoName)
        Fail("selected cloud render PSO is unavailable");

    if (probe)
    {
        // Log once per PSO transition, rather than on every cloud frame.
        if (lastLoggedProbePipeline_ != pipeline)
        {
            Log("cloud probe PSO selected: %s", psoName);
            lastLoggedProbePipeline_ = pipeline;
        }
    }
    else if (lastLoggedRenderPipeline_ != pipeline)
    {
        Log("cloud render PSO selected: %s", psoName);
        lastLoggedRenderPipeline_ = pipeline;
    }
    list->SetPipelineState(pipeline);
    list->SetComputeRootConstantBufferView(0, ConstantsAddress(frameIndex));
    list->SetComputeRootDescriptorTable(
        1, renderer_->SrvGpuHandle(kUavSlotBase + frameIndex));
    list->SetComputeRootDescriptorTable(2, noiseSrv);
    list->SetComputeRootDescriptorTable(3, weatherSrv);
    list->SetComputeRootDescriptorTable(
        4, renderer_->SrvGpuHandle(kProbeUavSlot));

    const UINT groupsX = DispatchGroupCount(outputWidth_, kThreadGroupX);
    const UINT groupsY = DispatchGroupCount(outputHeight_, kThreadGroupY);
    list->Dispatch(groupsX, groupsY, 1);
}

void CloudRayMarcher::Render(
    ID3D12GraphicsCommandList* list, uint32_t frameIndex,
    const clouds::CloudFrameConstants& constants,
    D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE weatherSrv,
    clouds::ShapePrototype prototype,
    clouds::CloudFormula formula,
    clouds::CloudDensityModel densityModel)
{
    if (!initialized_ || !list || frameIndex >= kFrameCount)
        Fail("CloudRayMarcher::Render called with invalid state");

    UploadConstants(frameIndex, constants);
    TransitionCloud(list, frameIndex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const uint32_t debugMode = (uint32_t)constants.shaping.w;
    const bool probe =
        debugMode == (uint32_t)clouds::CloudDebugMode::DensitySlice ||
        debugMode == (uint32_t)clouds::CloudDebugMode::DensityHorizontal ||
        debugMode == (uint32_t)clouds::CloudDebugMode::RayProfile ||
        debugMode == (uint32_t)clouds::CloudDebugMode::DensityVerticalZ ||
        debugMode == (uint32_t)clouds::CloudDebugMode::MacroShape ||
        debugMode == (uint32_t)clouds::CloudDebugMode::NoiseField ||
        debugMode == (uint32_t)clouds::CloudDebugMode::ViewOpticalDepth;
    const uint32_t macroFlags =
        static_cast<uint32_t>(constants.referenceMacro3.y);
    Dispatch(list, frameIndex, noiseSrv, weatherSrv, probe, prototype, formula,
             densityModel, (macroFlags & 127u) != 0u,
             (macroFlags & 128u) != 0u);
    InsertUavBarrier(list, textures_[frameIndex].Get());
    TransitionCloud(list, frameIndex,
                    D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}

CloudReadback CloudRayMarcher::Readback(
    const clouds::CloudFrameConstants& constants,
    D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE weatherSrv,
    clouds::CloudFormula formula,
    clouds::CloudDensityModel densityModel,
    clouds::ReferenceStage referenceStage)
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
            const uint32_t macroFlags =
                static_cast<uint32_t>(constants.referenceMacro3.y);
            Dispatch(list, 0, noiseSrv, weatherSrv, false,
                     clouds::ShapePrototype::None, formula, densityModel,
                     (macroFlags & 127u) != 0u, (macroFlags & 128u) != 0u);
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
    result.formula = formula;
    result.densityModel = densityModel;
    result.referenceStage = densityModel == clouds::CloudDensityModel::Reference
        ? referenceStage : clouds::ReferenceStage::None;
    if (densityModel == clouds::CloudDensityModel::Reference)
    {
        const uint32_t macroFlags =
            static_cast<uint32_t>(constants.referenceMacro3.y);
        const bool a03b = (macroFlags & 128u) != 0u;
        const bool a03a = !a03b && (macroFlags & 127u) != 0u;
        result.shaderVariant = a03b
            ? "CloudRayMarchReferenceA03b.hlsl"
            : (a03a ? "CloudRayMarchReferenceA03a.hlsl"
                    : "CloudRayMarchReference.hlsl");
        result.renderPso = result.shaderVariant;
        result.probePso = a03b
            ? "CloudRayMarchProbeReferenceA03b.hlsl"
            : (a03a ? "CloudRayMarchProbeReferenceA03a.hlsl"
                    : "CloudRayMarchProbeReference.hlsl");
    }
    else
    {
        switch (formula)
        {
        case clouds::CloudFormula::F1:
            result.shaderVariant = "CloudRayMarchFormulaF1.hlsl";
            break;
        case clouds::CloudFormula::F2:
            result.shaderVariant = "CloudRayMarchFormulaF2.hlsl";
            break;
        case clouds::CloudFormula::F3:
            result.shaderVariant = "CloudRayMarchFormulaF3.hlsl";
            break;
        case clouds::CloudFormula::F4:
            result.shaderVariant = "CloudRayMarchFormulaF4.hlsl";
            break;
        case clouds::CloudFormula::F5:
            result.shaderVariant = "CloudRayMarchFormulaF5.hlsl";
            break;
        default:
            result.shaderVariant = "CloudRayMarch.hlsl";
            break;
        }
        result.renderPso = result.shaderVariant;
        result.probePso = formula == clouds::CloudFormula::None
            ? "CloudRayMarchProbe.hlsl (F0)"
            : (std::string("CloudRayMarchProbeFormula") +
               clouds::CloudFormulaName(formula) + ".hlsl");
    }
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

CloudProbeReadback CloudRayMarcher::ReadbackProbe(uint32_t rows,
                                                  uint32_t steps)
{
    if (!initialized_ || !renderer_ || !probeBuffer_)
        Fail("CloudRayMarcher::ReadbackProbe called before initialization");

    rows = std::min(rows, probeCapacityRows_);
    steps = std::min(steps, kProbeMaxSteps);
    if (rows == 0 || steps == 0)
        Fail("CloudRayMarcher::ReadbackProbe has zero extent");
    if (static_cast<uint64_t>(rows) * steps * 3ull > probeCapacityRecords_)
        Fail("CloudRayMarcher::ReadbackProbe exceeds probe capacity");

    const UINT64 copyBytes =
        static_cast<UINT64>(rows) * steps * 3ull * 16ull;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = copyBytes;
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
                  "CreateCommittedResource (cloud probe readback)");

    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyBufferRegion(readback.Get(), 0, probeBuffer_.Get(), 0,
                                   copyBytes);
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        },
        "cloud probe readback");

    CloudProbeReadback result{};
    result.rows = rows;
    result.steps = steps;
    result.records = 3;
    result.values.resize(static_cast<size_t>(copyBytes / sizeof(float)));

    D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(copyBytes) };
    void* mapped = nullptr;
    ThrowIfFailed(readback->Map(0, &readRange, &mapped),
                  "Map cloud probe readback");
    std::memcpy(result.values.data(), mapped,
                static_cast<size_t>(copyBytes));
    const D3D12_RANGE writtenRange{ 0, 0 };
    readback->Unmap(0, &writtenRange);
    return result;
}

std::vector<float> CloudRayMarcher::ReadbackRawRecords(uint32_t count)
{
    if (!initialized_ || !renderer_ || !probeBuffer_)
        Fail("CloudRayMarcher::ReadbackRawRecords called before "
             "initialization");
    if (count == 0 || count > probeCapacityRecords_)
        Fail("CloudRayMarcher::ReadbackRawRecords invalid record count");

    const UINT64 copyBytes = static_cast<UINT64>(count) * 16ull;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = copyBytes;
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
                  "CreateCommittedResource (raw probe readback)");

    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyBufferRegion(readback.Get(), 0, probeBuffer_.Get(), 0,
                                   copyBytes);
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        },
        "raw probe readback");

    std::vector<float> values(static_cast<size_t>(copyBytes / sizeof(float)));
    D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(copyBytes) };
    void* mapped = nullptr;
    ThrowIfFailed(readback->Map(0, &readRange, &mapped),
                  "Map raw probe readback");
    std::memcpy(values.data(), mapped, static_cast<size_t>(copyBytes));
    const D3D12_RANGE writtenRange{ 0, 0 };
    readback->Unmap(0, &writtenRange);
    return values;
}

void CloudRayMarcher::DispatchVolume(
    ID3D12GraphicsCommandList* list,
    D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE weatherSrv,
    uint32_t gridX, uint32_t gridY, uint32_t gridZ, uint32_t mode,
    bool a03a, bool a03b)
{
    if (!list || gridX == 0 || gridY == 0 || gridZ == 0)
        Fail("DispatchVolume called with an invalid grid");
    if (mode == 0)
    {
        if (a03b)
        {
            if (!pipelineStateDensityVolumeA03b_)
                Fail("density volume A03b PSO is unavailable");
            list->SetPipelineState(
                pipelineStateDensityVolumeA03b_.Get());
        }
        else if (a03a)
        {
            if (!pipelineStateDensityVolumeA03a_)
                Fail("density volume A03a PSO is unavailable");
            list->SetPipelineState(
                pipelineStateDensityVolumeA03a_.Get());
        }
        else
        {
            if (!pipelineStateDensityVolume_)
                Fail("density volume PSO is unavailable");
            list->SetPipelineState(pipelineStateDensityVolume_.Get());
        }
    }
    else if (mode == 1)
    {
        ID3D12PipelineState* state = a03b
            ? pipelineStateStormBoundaryA03b_.Get()
            : (a03a ? pipelineStateStormBoundaryA03a_.Get()
                    : pipelineStateStormBoundary_.Get());
        if (!state)
            Fail("storm boundary PSO is unavailable");
        list->SetPipelineState(state);
    }
    else if (mode == 2)
    {
        ID3D12PipelineState* state = a03b
            ? pipelineStateDensityColumnsA03b_.Get()
            : (a03a ? pipelineStateDensityColumnsA03a_.Get()
                    : pipelineStateDensityColumns_.Get());
        if (!state)
            Fail("density columns PSO is unavailable");
        list->SetPipelineState(state);
    }
    else
    {
        Fail("DispatchVolume called with an unknown mode");
    }
    list->SetComputeRootSignature(rootSignature_.Get());
    list->SetComputeRootConstantBufferView(0, ConstantsAddress(0));
    list->SetComputeRootDescriptorTable(
        1, renderer_->SrvGpuHandle(kUavSlotBase));
    list->SetComputeRootDescriptorTable(2, noiseSrv);
    list->SetComputeRootDescriptorTable(3, weatherSrv);
    list->SetComputeRootDescriptorTable(
        4, renderer_->SrvGpuHandle(kProbeUavSlot));

    // StormBoundary and DensityColumns use numthreads(..., 1) in Z, so the
    // Z dispatch is one group per column, not gridZ/8. DensityColumns uses
    // 8 threads in Y; the boundary has a single Y row.
    const uint32_t groupsX = mode == 0 ? (gridX + 3u) / 4u
                                       : (gridX + 7u) / 8u;
    const uint32_t groupsY = mode == 0 ? (gridY + 3u) / 4u
                                       : (mode == 2 ? (gridY + 7u) / 8u
                                                    : 1u);
    const uint32_t groupsZ = mode == 0 ? (gridZ + 3u) / 4u : gridZ;
    list->Dispatch(groupsX, groupsY, groupsZ);
}

std::vector<float> CloudRayMarcher::ReadbackDensityVolume(
    const clouds::CloudFrameConstants& constants,
    D3D12_GPU_DESCRIPTOR_HANDLE noiseSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE weatherSrv,
    uint32_t gridX, uint32_t gridY, uint32_t gridZ, uint32_t mode)
{
    if (!initialized_ || !renderer_)
        Fail("CloudRayMarcher::ReadbackDensityVolume called before "
             "initialization");
    if (gridX == 0 || gridY == 0 || gridZ == 0 || mode > 2)
        Fail("CloudRayMarcher::ReadbackDensityVolume has invalid extent");

    const uint64_t records = mode == 1
        ? static_cast<uint64_t>(gridX) * gridZ
        : static_cast<uint64_t>(gridX) * gridY * gridZ;
    if (records > 0xffffffffull)
        Fail("density volume grid is too large");
    EnsureProbeCapacity(static_cast<uint32_t>(records));

    UploadConstants(0, constants);
    const UINT64 copyBytes = records * 16ull;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = copyBytes;
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
                  "CreateCommittedResource (density volume readback)");

    renderer_->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* list)
        {
            const uint32_t macroFlags =
                static_cast<uint32_t>(constants.referenceMacro3.y);
            const bool a03b = (macroFlags & 128u) != 0u;
            const bool a03a = !a03b &&
                ((macroFlags & 127u) != 0u ||
                 constants.referenceGrid.w > 2.5f);
            DispatchVolume(list, noiseSrv, weatherSrv, gridX, gridY, gridZ,
                           mode, a03a, a03b);
            InsertUavBarrier(list, probeBuffer_.Get());
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyBufferRegion(readback.Get(), 0, probeBuffer_.Get(), 0,
                                   copyBytes);
            TransitionResource(list, probeBuffer_.Get(),
                               D3D12_RESOURCE_STATE_COPY_SOURCE,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        },
        mode == 1 ? "storm boundary probe"
                  : (mode == 2 ? "density column probe"
                               : "density volume probe"));

    std::vector<float> values(static_cast<size_t>(copyBytes / sizeof(float)));
    D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(copyBytes) };
    void* mapped = nullptr;
    ThrowIfFailed(readback->Map(0, &readRange, &mapped),
                  "Map density volume readback");
    std::memcpy(values.data(), mapped, static_cast<size_t>(copyBytes));
    const D3D12_RANGE writtenRange{ 0, 0 };
    readback->Unmap(0, &writtenRange);
    return values;
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
    const uint32_t newWidth = std::max(1u, swapWidth / outputDivisor_);
    const uint32_t newHeight = std::max(1u, swapHeight / outputDivisor_);
    if (newWidth == outputWidth_ && newHeight == outputHeight_)
        return;

    renderer_->WaitForGpu();
    swapWidth_ = std::max(1u, swapWidth);
    swapHeight_ = std::max(1u, swapHeight);
    outputWidth_ = newWidth;
    outputHeight_ = newHeight;
    CreateTextures();
    CreateProbeBuffer();
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
    probeBuffer_.Reset();
    probeCapacityRows_ = 0;
    pipelineStateProbe_.Reset();
    for (auto& state : pipelineStateProbeFormula_)
        state.Reset();
    pipelineStateProbeProtoA_.Reset();
    pipelineStateProbeProtoB_.Reset();
    pipelineStateReference_.Reset();
    pipelineStateReferenceA03a_.Reset();
    pipelineStateReferenceA03b_.Reset();
    pipelineStateProbeReference_.Reset();
    pipelineStateProbeReferenceA03a_.Reset();
    pipelineStateProbeReferenceA03b_.Reset();
    pipelineStateDensityVolume_.Reset();
    pipelineStateDensityVolumeA03a_.Reset();
    pipelineStateStormBoundaryA03a_.Reset();
    pipelineStateDensityColumnsA03a_.Reset();
    pipelineStateDensityVolumeA03b_.Reset();
    pipelineStateStormBoundaryA03b_.Reset();
    pipelineStateDensityColumnsA03b_.Reset();
    pipelineStateStormBoundary_.Reset();
    pipelineStateDensityColumns_.Reset();
    probeCapacityRecords_ = 0;
    lastLoggedProbePipeline_ = nullptr;
    lastLoggedRenderPipeline_ = nullptr;
    pipelineStateProtoA_.Reset();
    pipelineStateProtoB_.Reset();
    for (auto& state : pipelineStateFormula_)
        state.Reset();
    pipelineState_.Reset();
    rootSignature_.Reset();
    renderer_ = nullptr;
    initialized_ = false;
    Log("cloud ray marcher shutdown: complete");
}
