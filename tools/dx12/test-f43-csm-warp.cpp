#include <windows.h>
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <d3d12sdklayers.h>
#include "GameDx12NativeFrame.h"
#include "GameDx12FrameStructuredUpload.h"
#include "gr/jobs/JobGroup.h"
#include "gr/jobs/JobSystem.h"

#include <algorithm>
#include <array>
#include <bit>
#include <string>
#include <DirectXMath.h>
#include "GameDx12Lighting.h"
#include <cstddef>
#include "production-shadow.inc"
#include "test-f43-csm-sampling-reference.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace ref = csm_reference;

void Msg(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vprintf(format, arguments);
    va_end(arguments);
    std::puts("");
}

void Check(bool ok, const char* operation)
{
    if (!ok)
        throw std::runtime_error(operation);
}

enum class Mutation { None, ForceSunZero, SwapSun, ReverseCompare, WrongNext, DisableBlend, WrongLocal };
const char* MutationName(Mutation m)
{
    switch (m)
    {
    case Mutation::ForceSunZero: return "force-sun-slice-zero";
    case Mutation::SwapSun: return "swap-sun-slices";
    case Mutation::ReverseCompare: return "reverse-manual-comparison";
    case Mutation::WrongNext: return "wrong-next-slice";
    case Mutation::DisableBlend: return "disable-blending";
    case Mutation::WrongLocal: return "wrong-local-layer";
    default: return "control";
    }
}
uint32_t RequiredFailure(Mutation m)
{
    switch (m)
    {
    case Mutation::WrongNext:
    case Mutation::DisableBlend: return ref::Overlap;
    case Mutation::WrongLocal: return ref::Local;
    case Mutation::None: return 0;
    default: return ref::Primary;
    }
}
void ReplaceOnce(std::string& source, const std::string& from, const std::string& to)
{
    const auto at = source.find(from);
    Check(at != std::string::npos && source.find(from,at+from.size()) == std::string::npos,
        "mutation anchor missing or ambiguous; update fixture after production changes");
    source.replace(at,from.size(),to);
}
void Mutate(std::string& source, Mutation m)
{
    const std::string sun = "float SunFilterVisibility(float3 coords, float3 planeNormal, uint slice)\n{";
    const std::string local = "float SampleLocalVisibility(float3 coords, uint layer)\n{";
    switch (m)
    {
    case Mutation::ForceSunZero: ReplaceOnce(source,sun,sun+"\n    slice = 0u;"); break;
    case Mutation::SwapSun: ReplaceOnce(source,sun,sun+"\n    slice = slice == 1u ? 2u : slice == 2u ? 1u : slice;"); break;
    case Mutation::ReverseCompare: ReplaceOnce(source,"<= SunStoredDepth(texel, slice)",">= SunStoredDepth(texel, slice)"); break;
    case Mutation::WrongNext: ReplaceOnce(source,"selected.nextSlice = next;","selected.nextSlice = 0u;"); break;
    case Mutation::DisableBlend: ReplaceOnce(source,"if (selected.nextSlice < 4)","if (false)"); break;
    case Mutation::WrongLocal: ReplaceOnce(source,local,local+"\n    layer = 0u;"); break;
    default: break;
    }
}

void Require(HRESULT hr, const char* operation)
{
    if (FAILED(hr))
    {
        std::fprintf(stderr, "0x%08X: %s\n", static_cast<unsigned>(hr), operation);
        throw std::runtime_error(operation);
    }
}

ComPtr<ID3DBlob> Compile(const char* source, const char* entry, const char* target)
{
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source, std::strlen(source), "csm-sampling.hlsl", nullptr, nullptr,
        entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
    if (FAILED(hr) && errors)
        std::fprintf(stderr, "%s\n", static_cast<const char*>(errors->GetBufferPointer()));
    Require(hr, "compile shadow sampling shader");
    return bytecode;
}

void VerifyFrameAbi(ID3DBlob& bytecode)
{
    ComPtr<ID3D12ShaderReflection> reflection;
    Require(D3DReflect(bytecode.GetBufferPointer(), bytecode.GetBufferSize(),
        IID_ID3D12ShaderReflection, reinterpret_cast<void**>(reflection.GetAddressOf())), "reflect FrameCB");
    auto* cb = reflection->GetConstantBufferByName("FrameCB");
    D3D12_SHADER_BUFFER_DESC buffer{};
    Require(cb->GetDesc(&buffer), "reflect FrameCB description");
    Check(buffer.Size == sizeof(SceneFrameConstants), "FrameCB size mismatch");
    const auto field = [&](const char* name, size_t offset, size_t bytes)
    {
        D3D12_SHADER_VARIABLE_DESC variable{};
        Require(cb->GetVariableByName(name)->GetDesc(&variable), name);
        Check(variable.StartOffset == offset && variable.Size == bytes, name);
    };
#define CHECK_FRAME_FIELD(hlsl, cpp) field(#hlsl, offsetof(SceneFrameConstants, cpp), sizeof(SceneFrameConstants::cpp))
    CHECK_FRAME_FIELD(ViewProjection, viewProjection);
    CHECK_FRAME_FIELD(CameraPosition, cameraPosition);
    CHECK_FRAME_FIELD(SunShadowViewProjection, sunShadowViewProjection);
    CHECK_FRAME_FIELD(Ambient, ambient);
    CHECK_FRAME_FIELD(SunDirection, sunDirection);
    CHECK_FRAME_FIELD(SunColor, sunColor);
    CHECK_FRAME_FIELD(LightingFlags, lightingFlags);
    CHECK_FRAME_FIELD(HemiColor, hemiColor);
    CHECK_FRAME_FIELD(InverseViewProjection, inverseViewProjection);
    CHECK_FRAME_FIELD(WaterWeather, waterWeather);
    CHECK_FRAME_FIELD(WaterSky, waterSky);
    CHECK_FRAME_FIELD(DiagnosticLight, diagnosticLight);
    CHECK_FRAME_FIELD(DiagnosticOrigin, diagnosticOrigin);
    CHECK_FRAME_FIELD(LegacyWaterSsr, legacyWaterSsr);
    CHECK_FRAME_FIELD(SunFilter, sunFilter);
    CHECK_FRAME_FIELD(SunReceiverBiasMask, sunReceiverBiasMask);
    CHECK_FRAME_FIELD(SunShadowResolution, sunShadowResolution);
    CHECK_FRAME_FIELD(SunCameraForward, sunCameraForward);
    CHECK_FRAME_FIELD(SunCascadeData, sunCascadeData);
    CHECK_FRAME_FIELD(SunDepthBias, sunDepthBias);
    CHECK_FRAME_FIELD(SunCasterCounts, sunCasterCounts);
    CHECK_FRAME_FIELD(LocalShadowConfig, localShadowConfig);
#undef CHECK_FRAME_FIELD
    D3D12_SHADER_INPUT_BIND_DESC binding{};
    Require(reflection->GetResourceBindingDescByName("LocalLights",&binding),"reflect local light SRV");
    Check(binding.Type==D3D_SIT_STRUCTURED && binding.BindPoint==13 && binding.BindCount==1,"local light SRV ABI");
    Require(reflection->GetResourceBindingDescByName("LocalShadowMatrices",&binding),"reflect local matrix SRV");
    Check(binding.Type==D3D_SIT_STRUCTURED && binding.BindPoint==14 && binding.BindCount==1,"local matrix SRV ABI");
    Check(kFrameConstantsSize >= buffer.Size && kFrameConstantsSize % 256 == 0, "CB allocation alignment");
}

struct ShadowSamplingPass
{
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> srv;
    ComPtr<ID3D12Resource> constants;
    game_shadow12::FrameStructuredUpload lights, projections;
    UINT stride = 0;

    ShadowSamplingPass(ID3D12Device& device, ID3D12Resource* sun, uint32_t sunSlices,
        ID3D12Resource* local, uint32_t localSlices, Mutation mutation)
    {
        constexpr char vertex[] = R"(
float4 vs(uint id : SV_VertexID) : SV_Position
{
    float2 p[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    return float4(p[id], 0, 1);
}
)";
        std::string pixel = "#define LEGACY_DIAGNOSTIC_LIGHT_SCALE " + std::to_string(gamelight12::LegacyDiagnosticLightScale) +
            "\n" + kFrameShaderConstants + kScenePixelShader + R"(
float4 probe(float4 pixel : SV_Position) : SV_Target
{
    uint x = (uint)pixel.x, y = (uint)pixel.y;
    uint count = LightingFlags.w;
    if (y == 5)
    {
        LocalLightData source = (LocalLightData)0;
        uint layer = LocalShadowConfig.x;
        float3 position = float3(1,0,0);
        source.type.x = 1;
        if (x >= count && x < count + LightingFlags.z)
        {
            layer = x - count;
            const uint offset = layer % 8;
            if (offset >= 2)
            {
                source.type.x = 0;
                const float3 directions[6] = {float3(1,0,0),float3(-1,0,0),float3(0,1,0),
                    float3(0,-1,0),float3(0,0,1),float3(0,0,-1)};
                position = directions[offset-2];
                layer = layer - offset + 2;
            }
        }
        source.type.y = layer + 1;
        float3 coords;
        uint actual;
        uint status = ProjectLocalShadow(source,position,float3(0,0,0),float3(0,0,0),coords,actual);
        return float4(status/255.0,(actual+1)/255.0,coords.z,1);
    }
    if (y == 4)
    {
        const float3 points[8] = {
            float3(.46875,.0625,.5), float3(.53125,.0625,.640625),
            float3(.46875,.265625,.5), float3(.53125,.3125,.5),
            float3(.03125,.1875,.5), float3(.96875,.1875,.5),
            float3(.4375,.4375,.5), float3(.5625,.6875,.5)
        };
        if (x >= 8) return float4(0,0,0,1);
        // Explicit fixture-only probes beyond the legacy CB array limits.
        const float structured = x == 0 ? LocalLights[32].colorInnerCos.w :
            x == 1 ? ProjectLocalMatrix(float4(1,2,3,1),31).x : 0.0f;
        return float4(SunFilterVisibility(points[x],float3(0,0,1),0),
                      SunFilterVisibility(points[x],float3(.25,0,1),0),structured,1);
    }
    if (x >= count && x < count + LightingFlags.z)
    {
        uint layer = x - count;
        float stored = LocalShadow.Load(int4(1,1,layer,0));
        float threshold = .03125 * (layer + 1);
        return float4(stored,SampleLocalVisibility(float3(.25,.25,threshold),layer),
            SampleLocalVisibility(float3(.25,.25,threshold+.015625),layer),1);
    }
    float depth = x + (y == 2 ? .875 : .5);
    float3 position = float3(depth,-.375,.5);
    SunSelection selected = SelectSun(position,float3(0,0,1),1);
    if (selected.slice >= 4)
        return float4(0,0,SunSelectedVisibility(selected,float3(0,0,1)),1);
    float visibility = SunSelectedVisibility(selected,float3(0,0,1));
    if (y == 2)
        return float4(visibility,selected.nextSlice < 4 ? (selected.nextSlice+1)/255.0 : 0,
            selected.blend,selected.fade);
    if (y == 3)
        return float4(SunFilterVisibility(selected.coords,float3(0,0,1),selected.slice),
            selected.coords.z,selected.fade,1);
    return float4(SunStoredDepth(int2(1,5),selected.slice),(selected.slice+1)/255.0,visibility,1);
}
)";
        pixel.erase(std::remove(pixel.begin(),pixel.end(),'\r'),pixel.end());
        Mutate(pixel,mutation);
        const auto vs = Compile(vertex, "vs", "vs_5_0");
        const auto ps = Compile(pixel.c_str(), "probe", "ps_5_0");
        if (mutation == Mutation::None) VerifyFrameAbi(*ps.Get());

        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 2;
        ranges[1] = ranges[0];
        ranges[1].BaseShaderRegister = 6;
        D3D12_ROOT_PARAMETER params[5]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = {1, &ranges[0]};
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable = {1, &ranges[1]};
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[3].Descriptor.ShaderRegister = 13;
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[4].Descriptor.ShaderRegister = 14;
        rootDesc.NumParameters = 5;
        rootDesc.pParameters = params;
        D3D12_STATIC_SAMPLER_DESC samplers[2]{};
        for (uint32_t i = 0; i < 2; ++i)
        {
            auto& smp = samplers[i];
            smp.ShaderRegister = 2 + i;
            smp.Filter = i ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
            smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            smp.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
            smp.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            smp.MaxLOD = D3D12_FLOAT32_MAX;
        }
        rootDesc.NumStaticSamplers = 2;
        rootDesc.pStaticSamplers = samplers;
        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        Require(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized, &errors), "serialize shadow sampling root signature");
        Require(device.CreateRootSignature(0, serialized->GetBufferPointer(),
            serialized->GetBufferSize(), IID_PPV_ARGS(&root)), "create shadow sampling root signature");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root.Get();
        desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        Require(device.CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)),
            "create shadow sampling pipeline");

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 2;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Require(device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&srv)),
            "create shadow sampling SRV heap");
        stride = device.GetDescriptorHandleIncrementSize(heapDesc.Type);
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_FLOAT;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2DArray.MipLevels = 1;
        view.Texture2DArray.ArraySize = sunSlices;
        auto cpu = srv->GetCPUDescriptorHandleForHeapStart();
        device.CreateShaderResourceView(sun, &view, cpu);
        view.Texture2DArray.ArraySize = localSlices;
        cpu.ptr += stride;
        device.CreateShaderResourceView(local, &view, cpu);

        D3D12_HEAP_PROPERTIES upload{};
        upload.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC cb{};
        cb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        cb.Width = kFrameConstantsSize;
        cb.Height = cb.DepthOrArraySize = cb.MipLevels = cb.SampleDesc.Count = 1;
        cb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Require(device.CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &cb,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constants)), "frame CB allocation");
    }

    void Draw(ID3D12GraphicsCommandList& list, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        uint32_t sunSlices, uint32_t localSlices, uint32_t filter)
    {
        SceneFrameConstants frame{};
        frame.lightingFlags[3] = sunSlices;
        frame.lightingFlags[2] = localSlices;
        frame.localShadowConfig[0] = localSlices;
        // Caller waits for GPU completion between fixture frames. Exercise
        // resource growth and accesses beyond 16 lights / 24 local matrices.
        const size_t recordCount = size_t(33) << (filter > 2 ? 2 : filter-1);
        std::vector<LocalLightGpu> localLights(recordCount);
        std::vector<DirectX::XMFLOAT4X4> localProjections(recordCount);
        localLights[32].colorInnerCos.w = .25f;
        localProjections[31]._11 = .25f;
        localProjections[31]._12 = .25f;
        for (uint32_t layer = 0; layer < localSlices; ++layer)
        {
            auto& matrix = localProjections[layer];
            // Fixed interior clip coordinate; independently test bank/face selection.
            matrix._34 = .5f;
            matrix._44 = 1;
        }
        frame.sunCameraForward = {1, 0, 0, 1};
        frame.sunShadowResolution = 8;
        frame.sunFilter = filter;
        for (uint32_t i = 0; i < sunSlices; ++i)
        {
            auto& matrix = frame.sunShadowViewProjection[i];
            // Транспонированная ортопроекция помещает приёмник в левую половину карты.
            matrix._11 = .125f;
            matrix._14 = -.5f;
            matrix._22 = matrix._33 = matrix._44 = 1;
            frame.sunCascadeData[i] = {float(i), float(i+1), float(i)+.75f, .125f};
            frame.sunDepthBias[i] = ref::Bias;
        }
        void* memory;
        const D3D12_RANGE noRead{};
        Require(constants->Map(0, &noRead, &memory), "map frame constants");
        std::memcpy(memory, &frame, sizeof(frame));
        constants->Unmap(0, nullptr);
        ComPtr<ID3D12Device> device;
        Require(constants->GetDevice(IID_PPV_ARGS(device.GetAddressOf())),"fixture buffer device");
        Require(lights.Write(*device.Get(),std::span<const LocalLightGpu>(localLights)),"fixture lights upload");
        Require(projections.Write(*device.Get(),std::span<const DirectX::XMFLOAT4X4>(localProjections)),"fixture matrix upload");
        ID3D12DescriptorHeap* heaps[] = {srv.Get()};
        list.SetDescriptorHeaps(1, heaps);
        list.SetPipelineState(pipeline.Get());
        list.SetGraphicsRootSignature(root.Get());
        list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto gpu = srv->GetGPUDescriptorHandleForHeapStart();
        list.SetGraphicsRootConstantBufferView(0, constants->GetGPUVirtualAddress());
        list.SetGraphicsRootShaderResourceView(3, lights.Address());
        list.SetGraphicsRootShaderResourceView(4, projections.Address());
        list.SetGraphicsRootDescriptorTable(1, gpu);
        gpu.ptr += stride;
        list.SetGraphicsRootDescriptorTable(2, gpu);
        const uint32_t width = (std::max)(sunSlices + localSlices + 1, ref::ProbeCount);
        D3D12_VIEWPORT viewport{0, 1, static_cast<float>(width), 5, 0, 1};
        D3D12_RECT rect{0, 1, static_cast<LONG>(width), 6};
        list.RSSetViewports(1, &viewport);
        list.RSSetScissorRects(1, &rect);
        list.OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list.DrawInstanced(3, 1, 0, 0);
    }
};

void Transition(ID3D12GraphicsCommandList& list, ID3D12Resource& resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
    uint32_t slice = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {&resource, slice, before, after};
    list.ResourceBarrier(1, &barrier);
}

struct Readback
{
    ComPtr<ID3D12Resource> buffer;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, game_shadow12::MaxViews> footprints{};
    uint64_t bytes = 0;

    Readback(ID3D12Device& device, const D3D12_RESOURCE_DESC& source, uint32_t count)
    {
        device.GetCopyableFootprints(&source, 0, count, 0, footprints.data(), nullptr, nullptr, &bytes);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Require(device.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)), "readback allocation");
    }

    void Copy(ID3D12GraphicsCommandList& list, ID3D12Resource& resource, uint32_t index)
    {
        D3D12_TEXTURE_COPY_LOCATION from{};
        from.pResource = &resource;
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.SubresourceIndex = index;
        D3D12_TEXTURE_COPY_LOCATION to{};
        to.pResource = buffer.Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint = footprints[index];
        list.CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    }

    uint32_t Pixel(uint32_t slice, uint32_t x, uint32_t y)
    {
        const size_t offset = footprints[slice].Offset + footprints[slice].Footprint.RowPitch * y + x * 4;
        const D3D12_RANGE read{offset, offset + 4};
        void* mapped = nullptr;
        Require(buffer->Map(0, &read, &mapped), "readback map");
        uint32_t pixel = 0;
        std::memcpy(&pixel, static_cast<const char*>(mapped) + offset, sizeof(pixel));
        const D3D12_RANGE written{};
        buffer->Unmap(0, &written);
        return pixel;
    }
};

using Frame = GameDx12NativeFrame12;

std::vector<uint32_t> Run(HWND window, IDXGIFactory2& factory, const Frame::Owner& owner,
    const char* mode, uint32_t sunCount, uint32_t localCount, uint32_t workers = 0,
    Mutation mutation = Mutation::None)
{
    const bool multi = std::strcmp(mode, "legacy") != 0;
    const bool parallel = std::strcmp(mode, "parallel") == 0;
    Check(_putenv_s("OGSR_DX12_GAME_COMMAND_LISTS", mode) == 0, "set command list mode");
    gr::jobs::JobSystem jobs;
    if (parallel)
    {
        gr::jobs::JobConfig config;
        config.backgroundWorkers = workers;
        config.automaticWorkers = false;
        Check(jobs.Initialize(config), "GR initialize");
    }
    Frame frame;
    Require(frame.Initialize(window, factory, owner), "native initialize");

    auto& device = owner.device;
    constexpr auto sampleState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    constexpr auto depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    constexpr auto copyState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    struct DepthArray
    {
        ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_DESC desc{};
        float clear;
        float step;
    };
    const auto createArray = [&](uint32_t slices, float clearDepth, float step)
    {
        DepthArray maps{};
        maps.clear = clearDepth;
        maps.step = step;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        auto& image = maps.desc;
        image.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        image.Width = image.Height = 8;
        image.DepthOrArraySize = static_cast<UINT16>((std::max)(slices, 1u));
        image.MipLevels = image.SampleDesc.Count = 1;
        image.Format = DXGI_FORMAT_R32_TYPELESS;
        image.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = clearDepth;
        Require(device.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &image, sampleState,
            &clear, IID_PPV_ARGS(&maps.resource)), "shadow allocation");
        return maps;
    };
    // Every depth value stays in [0,1], while each slice remains distinguishable.
    const auto sun = createArray(sunCount, ref::SunDepth(0), 0.25f);
    const auto local = createArray(localCount, ref::LocalDepth(0), 0.03125f);
    ShadowSamplingPass sampling(device, sun.resource.Get(), sunCount, local.resource.Get(),
        (std::max)(localCount, 1u), mutation);
    const uint32_t passCount = sunCount + localCount;
    const auto passView = [sunCount](uint32_t index)
    {
        return game_shadow12::View{index < sunCount ? index : game_shadow12::MaxSunCascades + index - sunCount};
    };
    const auto mapsFor = [&](game_shadow12::View view) -> const DepthArray& { return view.IsSun() ? sun : local; };
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = passCount;
    ComPtr<ID3D12DescriptorHeap> dsv;
    Require(device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsv)), "shadow DSV heap");
    const auto handle = dsv->GetCPUDescriptorHandleForHeapStart();
    const auto stride = device.GetDescriptorHandleIncrementSize(heapDesc.Type);
    for (uint32_t index = 0; index < passCount; ++index)
    {
        const auto view = passView(index);
        D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray = {0, view.Slice(), 1};
        device.CreateDepthStencilView(mapsFor(view).resource.Get(), &desc, {handle.ptr + stride * index});
    }

    if (multi)
    {
        Frame::Recording incomplete;
        Require(frame.Begin(window, owner, incomplete), "begin incomplete frame");
        auto* partial = incomplete.list;
        Require(incomplete.OpenShadowList(0, partial), "open incomplete list");
        if (parallel)
        {
            auto failing = jobs.CreateGroup();
            failing.Submit([] { throw std::runtime_error("synthetic GR record failure"); });
            bool failed = false;
            try { failing.Wait(); }
            catch (const std::runtime_error&) { failed = true; }
            Check(failed, "callback failure observed before cancel");
        }
        Check(FAILED(incomplete.BeginColorList()), "open shadow list cannot enter color phase");
        const auto before = owner.fenceValue;
        Check(FAILED(frame.Present(owner, 0)), "partial frame must fail");
        Check(owner.fenceValue == before, "partial frame cannot signal");
    }

    std::vector<uint32_t> result;
    uint32_t mismatches = 0;
    const uint32_t frameCount = mutation == Mutation::None ? 8u : 1u;
    for (uint32_t index = 0; index < frameCount; ++index)
    {
        const int windowSize = index < 3 ? 48 : 64;
        Check(SetWindowPos(window, nullptr, 0, 0, windowSize, windowSize,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != 0, "resize HWND");
        Frame::Recording recording;
        Require(frame.Begin(window, owner, recording), "begin native frame");
        Check(recording.list && recording.width == static_cast<uint32_t>(windowSize), "resize reached native frame");
        Check(recording.UsesShadowLists() == multi && recording.UsesParallelShadowLists() == parallel,
            "effective command list mode");
        std::array<ID3D12GraphicsCommandList*, game_shadow12::MaxViews> shadowLists{};
        for (uint32_t pass = 0; pass < passCount; ++pass)
        {
            const auto view = passView(pass);
            Transition(*recording.list, *mapsFor(view).resource.Get(), sampleState, depthState, view.Slice());
            shadowLists[pass] = recording.list;
            if (multi)
                Require(recording.OpenShadowList(pass, shadowLists[pass]), "open exclusive shadow list");
        }
        const auto record = [&](uint32_t first, uint32_t last, uint32_t)
        {
            for (uint32_t pass = first; pass < last; ++pass)
            {
                const auto view = passView(pass);
                shadowLists[pass]->ClearDepthStencilView({handle.ptr + stride * pass}, D3D12_CLEAR_FLAG_DEPTH,
                    mapsFor(view).clear + mapsFor(view).step * static_cast<float>(view.Slice()), 0, 0, nullptr);
                if (view.IsSun() && view.Slice() == 0)
                {
                    for (int y = 0; y < 3; ++y)
                        for (int x = 0; x < ref::Size; ++x)
                        {
                            const D3D12_RECT texel{x,y,x+1,y+1};
                            shadowLists[pass]->ClearDepthStencilView({handle.ptr + stride * pass},
                                D3D12_CLEAR_FLAG_DEPTH, ref::Texel(0,x,y), 0, 1, &texel);
                        }
                }
                if (multi)
                    Require(recording.CloseShadowList(pass), "close exclusive shadow list");
            }
        };
        if (parallel)
        {
            auto group = jobs.CreateGroup();
            group.ParallelFor(passCount, 1, record);
            group.Wait();
        }
        else
            record(0, passCount, 0);
        if (multi)
            Require(recording.BeginColorList(), "begin color list");
        Readback sunDepth(device, sun.desc, sunCount);
        Readback localDepth(device, local.desc, (std::max)(localCount, 1u));
        for (uint32_t pass = 0; pass < passCount; ++pass)
        {
            const auto view = passView(pass);
            auto& resource = *mapsFor(view).resource.Get();
            const auto slice = view.Slice();
            Transition(*recording.list, resource, depthState, copyState, slice);
            (view.IsSun() ? sunDepth : localDepth).Copy(*recording.list, resource, slice);
            Transition(*recording.list, resource, copyState, sampleState, slice);
        }

        constexpr auto renderState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        // Sample every production array slice through t2/t6 before the color pass
        // overwrites the control pixels. The readback proves SRV array indexing.
        const uint32_t filter = mutation == Mutation::None ? index + 1 : 4;
        sampling.Draw(*recording.list, recording.rtv, sunCount, localCount, filter);
        Readback sampledColor(device, recording.color->GetDesc(), 1);
        Transition(*recording.list, *recording.color, renderState, copyState);
        sampledColor.Copy(*recording.list, *recording.color, 0);
        Transition(*recording.list, *recording.color, copyState, renderState);

        const D3D12_RECT patch{4, 4, 12, 12};
        const float red[]{1, 0, 0, 1};
        recording.list->ClearRenderTargetView(recording.rtv, red, 1, &patch);
        Readback color(device, recording.color->GetDesc(), 1);
        Transition(*recording.list, *recording.color, renderState, copyState);
        color.Copy(*recording.list, *recording.color, 0);
        Transition(*recording.list, *recording.color, copyState, renderState);

        Require(frame.Present(owner, 0), "present ordered batch");
        Require(owner.fence.SetEventOnCompletion(owner.fenceValue, owner.event), "set fence event");
        Check(WaitForSingleObject(owner.event, 10000) == WAIT_OBJECT_0, "wait GPU fence");
        Require(device.GetDeviceRemovedReason(), "D3D12 device after ordered submission");
        const auto background = color.Pixel(0, 0, 0);
        const auto foreground = color.Pixel(0, 8, 8);
        Check(background == 0xff000000 && foreground == 0xff0000ff, "color clear/order mismatch");
        const uint32_t probeWidth = (std::max)(sunCount+localCount+1,ref::ProbeCount);
        for (uint32_t y = 1; y <= 5; ++y)
            for (uint32_t x = 0; x < probeWidth; ++x)
            {
                const uint32_t pixel = sampledColor.Pixel(0,x,y);
                const auto expected = ref::Pixel(x,y,sunCount,localCount,filter);
                bool ok = true;
                for (int channel = 0; channel < 4; ++channel)
                {
                    const int actual = (pixel >> (8*channel)) & 255;
                    const int wanted = static_cast<int>(std::lround(expected.rgba[channel]*255));
                    const int tolerance = (channel == expected.exactIndexChannel || (y == 5 && channel == 0)) ? 0 : 1;
                    ok = ok && std::abs(actual-wanted) <= tolerance;
                }
                if (!ok)
                {
                    if ((mismatches & expected.category) == 0)
                        std::printf("sampling mismatch mutation=%s mode=%s filter=%u x=%u y=%u category=%u actual=%08x expected=(%.4f,%.4f,%.4f,%.4f)\n",
                            MutationName(mutation),mode,filter,x,y,expected.category,pixel,
                            expected.rgba[0],expected.rgba[1],expected.rgba[2],expected.rgba[3]);
                    mismatches |= expected.category;
                }
                if (mutation == Mutation::None && (index == 0 || y == 4))
                    std::printf("sampling readback mode=%s sun=%u local=%u filter=%u x=%u y=%u rgba=%08x\n",
                        mode,sunCount,localCount,filter,x,y,pixel);
                result.push_back(pixel);
            }
        result.insert(result.end(), {background,foreground});
        for (uint32_t pass = 0; pass < passCount; ++pass)
        {
            const auto view = passView(pass);
            for (int y = 0; y < ref::Size; ++y)
                for (int x = 0; x < ref::Size; ++x)
                {
                    const auto depth = (view.IsSun() ? sunDepth : localDepth).Pixel(view.Slice(),x,y);
                    const float expectedDepth = view.IsSun() ? ref::Texel(view.Slice(),x,y) : ref::LocalDepth(view.Slice());
                    Check(depth == std::bit_cast<uint32_t>(expectedDepth), "shadow depth pattern/order mismatch");
                    result.push_back(depth);
                }
        }
    }
    Require(frame.Drain(owner), "drain native slot work");
    if (parallel)
    {
        const auto stats = jobs.GetStats();
        Check(stats.tasksSubmitted == frameCount + 1 && stats.tasksCompleted == frameCount + 1 && stats.jobFailures == 1 &&
            stats.unobservedFailures == 0, "GR close/drain counters");
        std::printf("GR workers=%u submit=%llu complete=%llu observed_faults=%llu\n", workers,
            static_cast<unsigned long long>(stats.tasksSubmitted), static_cast<unsigned long long>(stats.tasksCompleted),
            static_cast<unsigned long long>(stats.jobFailures));
        jobs.Shutdown();
    }
    if (mutation == Mutation::None)
        Check(mismatches == 0,"control GPU sampling mismatch");
    else
    {
        Check((mismatches & RequiredFailure(mutation)) != 0,"GPU mutation survived targeted validation");
        std::printf("MUTATION KILLED: %s mismatch_mask=%u\n",MutationName(mutation),mismatches);
    }
    return result;
}

int main(int argc, char** argv)
{
    try
    {
        ComPtr<ID3D12Debug> debug;
        Require(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "D3D12 Debug Layer");
        debug->EnableDebugLayer();
        ComPtr<ID3D12Debug1> validation;
        Require(debug.As(&validation), "GPU-based validation capability");
        validation->SetEnableGPUBasedValidation(TRUE);
        ComPtr<IDXGIFactory4> factory;
        Require(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)), "DXGI factory");
        ComPtr<IDXGIAdapter> adapter;
        Require(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "WARP adapter");
        ComPtr<ID3D12Device> device;
        Require(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "WARP device");
        ComPtr<ID3D12InfoQueue> messages;
        Require(device.As(&messages), "D3D12 info queue");
        Require(messages->SetMessageCountLimit(100000), "InfoQueue message limit");
        ComPtr<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC desc{};
        Require(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "direct queue");
        ComPtr<ID3D12Fence> fence;
        Require(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
        const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Check(event != nullptr, "create fence event");
        const HWND window = CreateWindowExW(0, L"STATIC", L"DX12 technical test", WS_POPUP,
            0, 0, 48, 48, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(window != nullptr, "create test HWND");
        uint64_t value = 0;
        const Frame::Owner owner{*device.Get(), *queue.Get(), *fence.Get(), event, value};
        Check(_putenv_s("OGSR_DX12_CLEAR", "0,0,0") == 0, "set clear color");
        const bool sequentialOnly = argc > 1 && std::strcmp(argv[1], "--sequential") == 0;
        const std::array<std::array<uint32_t, 2>, 6> configurations{{
            {1,8}, {2,0}, {3,8}, {4,8}, {4,16}, {4,24}
        }};
        for (const auto& config : configurations)
        {
            const uint32_t cascades = config[0], locals = config[1];
            const auto legacy = Run(window, *factory.Get(), owner, "legacy", cascades, locals);
            const auto multi = Run(window, *factory.Get(), owner, "multi", cascades, locals);
            Check(legacy == multi, "legacy/multi readback parity");
            if (!sequentialOnly)
            {
                const auto parallel = Run(window, *factory.Get(), owner, "parallel", cascades, locals, cascades - 1);
                Check(legacy == parallel, "legacy/parallel readback parity");
            }
            std::printf("PASS: cascades=%u local=%u\n", cascades, locals);
        }
        // Compile and execute each mutated copy; only the targeted GPU mismatch
        // is accepted. Compile, PSO, device and fence failures propagate.
        for (const Mutation mutation : {Mutation::ForceSunZero, Mutation::SwapSun,
            Mutation::ReverseCompare, Mutation::WrongNext, Mutation::DisableBlend, Mutation::WrongLocal})
            Run(window,*factory.Get(),owner,"legacy",4,24,0,mutation);
        DestroyWindow(window);
        CloseHandle(event);
        uint32_t problems = 0;
        uint32_t expectedClearWarnings = 0;
        Check(messages->GetNumMessagesDiscardedByMessageCountLimit() == 0, "InfoQueue discarded messages");
        Check(messages->GetNumMessagesDeniedByStorageFilter() == 0, "InfoQueue storage-filtered messages");
        Check(messages->GetNumStoredMessagesAllowedByRetrievalFilter() == messages->GetNumStoredMessages(),
            "InfoQueue retrieval-filtered messages");
        for (uint64_t index = 0; index < messages->GetNumStoredMessages(); ++index)
        {
            SIZE_T size = 0;
            Require(messages->GetMessage(index, nullptr, &size), "InfoQueue message size");
            Check(size >= sizeof(D3D12_MESSAGE), "InfoQueue invalid message size");
            std::vector<unsigned char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Require(messages->GetMessage(index, message, &size), "InfoQueue read message");
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
            {
                if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING &&
                    message->ID == D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE)
                    ++expectedClearWarnings;
                else
                {
                    std::printf("D3D12 %u: %s\n", message->ID, message->pDescription);
                    ++problems;
                }
            }
        }
        Check(problems == 0, "D3D12 warning/error/corruption");
        std::printf("D3D12 expected clear-value warnings=%u\n", expectedClearWarnings);
        std::puts("PASS: WARP Debug Layer + GBV, sun/local sampling, PCF 1-7, GPU mutations, ABI, readback parity, resize, cancel, GR fault drain");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
