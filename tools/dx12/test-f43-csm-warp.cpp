#include <windows.h>
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <d3d12sdklayers.h>
#include "GameDx12NativeFrame.h"
#include "GameDx12ShadowRecording.h"
#include "GameDx12ShadowContentCache.h"
#include "GameDx12ClusteredLighting.h"
#include "GameDx12StaticShadowCache.h"
#include "GameDx12AdaptiveShadowRuntime.h"
#include "GameDx12FrameStructuredUpload.h"
#include "GameDx12ShadowAddress.h"
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
#include <memory>
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

ComPtr<ID3DBlob> Compile(const char* source, const char* entry, const char* target, UINT flags = 0)
{
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source, std::strlen(source), "csm-sampling.hlsl", nullptr, nullptr,
        entry, target, flags | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
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
    CHECK_FRAME_FIELD(ClusterTiles, clusterTiles);
    CHECK_FRAME_FIELD(ClusterGrid, clusterGrid);
    CHECK_FRAME_FIELD(ClusterDepth, clusterDepth);
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
    bool banked51 = false;

    ShadowSamplingPass(ID3D12Device& device, ID3D12Resource* sun, uint32_t sunSlices,
        ID3D12Resource* local, uint32_t localSlices, Mutation mutation, bool pageTable = false, bool banked = false, ID3D12DescriptorHeap* sharedHeap = nullptr)
        : srv(sharedHeap), banked51(banked)
    {
        const bool sm51 = pageTable || banked;
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
        // Independent expected physical mapping, not a read from metadata.
        const uint physical = layer % 8 < 2 ? layer ^ 1u : layer;
        float stored = LOCAL_SHADOW_PAGE(0).Load(int4(1,1,physical,0));
        float threshold = .03125 * (physical + 1);
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
        if (pageTable)
        {
            pixel.resize(pixel.find("float4 probe(float4 pixel"));
            pixel.insert(0, "#define OGSR_DX12_SHADOW_PAGE_TABLE 1\n");
            pixel += R"(
float4 probe(float4 pixel : SV_Position) : SV_Target
{
    uint layer = uint(pixel.x);
    if (layer < LightingFlags.w)
        return float4(SunFilterVisibility(float3(.5,.5,.5), float3(0,0,1), layer),1,0,1);
    layer -= LightingFlags.w;
    if (layer >= LocalShadowConfig.x)
        return float4(1,0,0,1);
    uint4 address = LocalShadowMatrices[layer].address;
    float visibility = SampleLocalVisibility(float3(.5, .5, LocalLights[0].colorInnerCos.w), layer);
    return float4(visibility, ValidLocalAddress(address) ? 1 : 0, 0, 1);
}
)";
        }
        else
            Mutate(pixel,mutation);
        if (banked)
            pixel.insert(0, "#define OGSR_DX12_SHADOW_PAGE_TABLE 1\n");
        const auto vs = Compile(vertex, "vs", "vs_5_0");
        const auto ps = Compile(pixel.c_str(), "probe", sm51 ? "ps_5_1" : "ps_5_0",
            sm51 ? D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES : 0);
        if (!pageTable && mutation == Mutation::None) VerifyFrameAbi(*ps.Get());

        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 2;
        ranges[1] = ranges[0];
        ranges[1].BaseShaderRegister = sm51 ? 0 : 6;
        ranges[1].RegisterSpace = sm51 ? 1 : 0;
        ranges[1].NumDescriptors = sm51 ? UINT32_MAX : 1;
        D3D12_ROOT_PARAMETER params[6]{};
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
        params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[5].Descriptor.ShaderRegister = 15;
        rootDesc.NumParameters = sm51 ? 6 : 5;
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
        heapDesc.NumDescriptors = pageTable ? 6 : 2;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!srv)
            Require(device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&srv)), "create shadow sampling SRV heap");
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

    void DrawPages(ID3D12Device& device, ID3D12GraphicsCommandList& list, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        game_shadow12::ShadowDescriptors::Binding binding, std::span<const LocalShadowGpuProjection> data,
        uint32_t sunSlices = 0)
    {
        SceneFrameConstants frame{};
        frame.lightingFlags[3] = sunSlices;
        frame.sunFilter = 1;
        frame.sunShadowResolution = 8;
        frame.localShadowConfig[0] = static_cast<uint32_t>(data.size());
        frame.localShadowConfig[1] = binding.pageCount;
        void* mapped = nullptr;
        const D3D12_RANGE noRead{};
        Require(constants->Map(0, &noRead, &mapped), "page probe constants");
        std::memcpy(mapped, &frame, sizeof(frame));
        constants->Unmap(0, nullptr);
        LocalLightGpu light{};
        light.colorInnerCos.w = .5f;
        Require(lights.Write(device, std::span<const LocalLightGpu>(&light, 1)), "page probe light");
        if (!data.empty())
            Require(projections.Write(device, data), "page probe addresses");
        ID3D12DescriptorHeap* heaps[] = {srv.Get()};
        list.SetDescriptorHeaps(1, heaps);
        list.SetPipelineState(pipeline.Get());
        list.SetGraphicsRootSignature(root.Get());
        list.SetGraphicsRootConstantBufferView(0, constants->GetGPUVirtualAddress());
        const auto heapStart = srv->GetGPUDescriptorHandleForHeapStart();
        list.SetGraphicsRootDescriptorTable(1, heapStart);
        list.SetGraphicsRootDescriptorTable(2, binding.table.ptr ? binding.table :
            D3D12_GPU_DESCRIPTOR_HANDLE{heapStart.ptr + stride});
        list.SetGraphicsRootShaderResourceView(5, binding.shapes ? binding.shapes : constants->GetGPUVirtualAddress());
        list.SetGraphicsRootShaderResourceView(3, lights.Address());
        list.SetGraphicsRootShaderResourceView(4, data.empty() ? constants->GetGPUVirtualAddress() : projections.Address());
        list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_VIEWPORT viewport{0, 0, 8, 1, 0, 1};
        const D3D12_RECT rect{0, 0, 8, 1};
        list.RSSetViewports(1, &viewport);
        list.RSSetScissorRects(1, &rect);
        list.OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list.DrawInstanced(3, 1, 0, 0);
    }

    void Draw(ID3D12GraphicsCommandList& list, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        uint32_t sunSlices, uint32_t localSlices, uint32_t filter)
    {
        SceneFrameConstants frame{};
        frame.lightingFlags[3] = sunSlices;
        frame.lightingFlags[2] = localSlices;
        frame.localShadowConfig[0] = localSlices;
        frame.localShadowConfig[1] = localSlices ? 1u : 0u;
        frame.localShadowConfig[2] = localSlices;
        frame.localShadowConfig[3] = banked51 && localSlices ? ref::Size : 0;
        // Caller waits for GPU completion between fixture frames. Exercise
        // resource growth and accesses beyond 16 lights / 24 local matrices.
        const size_t recordCount = size_t(33) << (filter > 2 ? 2 : filter-1);
        std::vector<LocalLightGpu> localLights(recordCount);
        std::vector<LocalShadowGpuProjection> localProjections(recordCount);
        localLights[32].colorInnerCos.w = .25f;
        localProjections[31].matrix._11 = .25f;
        localProjections[31].matrix._12 = .25f;
        for (uint32_t layer = 0; layer < localSlices; ++layer)
        {
            auto& matrix = localProjections[layer].matrix;
            localProjections[layer].address = game_shadow12::LegacyShadowAddress(layer,8);
            if (layer%8<2) localProjections[layer].address.slice ^= 1u;
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
        Require(projections.Write(*device.Get(),std::span<const LocalShadowGpuProjection>(localProjections)),"fixture matrix upload");
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
        if (banked51)
            list.SetGraphicsRootShaderResourceView(5, projections.Address());
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
    Mutation mutation = Mutation::None, bool banked51 = false)
{
    const bool multi = std::strcmp(mode, "legacy") != 0;
    const bool parallel = std::strcmp(mode, "parallel") == 0;
    Check(_putenv_s("OGSR_DX12_GAME_METRICS", banked51 ? "1" : "0") == 0, "banked timestamp mode");
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
    ShadowSamplingPass sampling(device, sun.resource.Get(), sunCount, localCount ? local.resource.Get() : nullptr,
        (std::max)(localCount, 1u), mutation, false, banked51);
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
    bool observedTiming = false;
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
        if (banked51)
        {
            const auto& timing = frame.LastGpuTiming();
            if (timing.present)
            {
                Check(timing.views.size() == passCount && timing.milliseconds >= timing.shadowMilliseconds,
                    "banked CSM/local/frame timestamps");
                for (uint32_t i = 0; i < passCount; ++i)
                    Check(timing.views[i].projection == passView(i).projection, "banked timestamp projection ID");
                observedTiming = true;
            }
            std::array<uint32_t, game_shadow12::MaxViews> ids{};
            for (uint32_t i = 0; i < passCount; ++i)
                ids[i] = passView(i).projection;
            Check(recording.SetShadowTimingViews(std::span(ids.data(), passCount)) == S_OK,
                "enable banked CSM/local timestamps");
        }
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
                recording.StampShadow(*shadowLists[pass], view.projection, false);
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
                recording.StampShadow(*shadowLists[pass], view.projection, true);
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
        recording.StampSceneStart();
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

        Require(frame.Present(owner, banked51 ? 1 : 0), "present ordered batch");
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
    Check(!banked51 || observedTiming, "banked CSM/frame timing observed after slot reuse");
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

void RunPages(HWND window, IDXGIFactory2& factory, const Frame::Owner& owner)
{
    using namespace game_shadow12;
    Check(_putenv_s("OGSR_DX12_GAME_COMMAND_LISTS", "legacy") == 0, "legacy command list");
    auto& device = owner.device;
    std::array<ShadowPageResources, Frame::kFrameCount> pages;
    ShadowSamplingPass pass(device, nullptr, 1, nullptr, 1, Mutation::None, true);
    ShadowDescriptors descriptors;
    Require(descriptors.Initialize(device, owner.fence, *pass.srv.Get(), 2), "page table initialize");
    for (uint32_t i = 0; i < pages.size(); ++i)
        Require(pages[i].Initialize(device, owner.fence, i), "page arena initialize");
    Frame frame;
    Require(frame.Initialize(window, factory, owner), "page probe frame initialize");
    const std::array<ShadowPageShape, 2> shapes{{{128, 6}, {256, 9}}};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, Frame::kFrameCount> tables{};
    constexpr std::array<uint32_t, 8> expected{
        0xff00ff00, 0xff00ffff, 0xff00ffff, 0xff00ff00,
        0xff0000ff, 0xff0000ff, 0xff0000ff, 0xff0000ff};

    for (uint32_t variant = 0; variant < 5; ++variant)
    {
        Frame::Recording recording;
        Check(frame.Begin(window, owner, recording) == S_OK, "begin page probe");
        auto& arena = pages[recording.slot];
        AdaptivePlan logical{1, 1, 1, variant + 1, UINT64_MAX, 0,
            {{1,1,128,128,1},{2,1,256,256,1},{3,1,128,128,1},{4,1,256,256,1}}};
        Require(arena.Prepare(logical, shapes, UINT64_MAX, UINT64_MAX, owner.fenceValue + 1), "prepare two pages");
        ShadowAddressPlan addresses;
        ShadowDescriptors::Binding binding;
        Require(recording.StageShadowPages(descriptors, arena, addresses, binding), "stage native page table");
        Check(binding.pageCount == 2 && addresses.views.size() == 4, "two physical pages and four projections");
        if (variant == 0)
        {
            Require(frame.Cancel(), "cancel staged native page table");
            Check(!arena.PreparedPlan() && arena.CurrentBytes() == 0, "cancel keeps physical arena unpublished");
            continue;
        }
        tables[recording.slot] = binding.table;
        std::array<LocalShadowGpuProjection, 8> gpu{};
        for (size_t i = 0; i < addresses.views.size(); ++i)
        {
            const auto& address = addresses.views[i];
            ShadowPageResources::View physical;
            Check(arena.GetView(addresses.pageIds[address.page], address.slice, physical), "physical shadow view");
            constexpr auto sampled = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            constexpr auto writing = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            Transition(*recording.list, *physical.resource, sampled, writing, address.slice);
            recording.list->ClearDepthStencilView(physical.dsv, D3D12_CLEAR_FLAG_DEPTH,
                (i == 0 || i == 3) ? .25f : .75f, 0, 0, nullptr);
            Transition(*recording.list, *physical.resource, writing, sampled, address.slice);
            gpu[i].address.page = address.page;
            gpu[i].address.slice = address.slice;
            gpu[i].address.resolution = address.resolution;
        }
        gpu[4] = gpu[0]; gpu[4].address.page = binding.pageCount;
        gpu[5] = gpu[0]; gpu[5].address.slice = 6;
        gpu[6] = gpu[1]; gpu[6].address.slice = 9;
        gpu[7] = gpu[0]; gpu[7].address.resolution = 256;
        if (variant == 2)
            gpu[0].address.page ^= 1;
        if (variant == 3)
            gpu[0].address.slice ^= 1;
        if (variant == 4)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE source{};
            Check(arena.GetPreparedPageSrv(addresses.pageIds[gpu[0].address.page ^ 1], source), "wrong descriptor source");
            auto destination = pass.srv->GetCPUDescriptorHandleForHeapStart();
            const auto start = pass.srv->GetGPUDescriptorHandleForHeapStart();
            destination.ptr += static_cast<SIZE_T>(binding.table.ptr - start.ptr) + gpu[0].address.page * pass.stride;
            device.CopyDescriptorsSimple(1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        pass.DrawPages(device, *recording.list, recording.rtv, binding, gpu);
        Readback pixels(device, recording.color->GetDesc(), 1);
        Transition(*recording.list, *recording.color, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        pixels.Copy(*recording.list, *recording.color, 0);
        Transition(*recording.list, *recording.color, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Require(frame.Present(owner, 0), "submit descriptor probe through native frame");
        Require(frame.Drain(owner), "drain page probe");
        Check(!arena.PreparedPlan() && arena.CurrentBytes() > 0, "native Signal publishes physical arena");

        uint32_t mismatchMask = 0;
        for (uint32_t i = 0; i < expected.size(); ++i)
        {
            const auto actual = pixels.Pixel(0, i, 0);
            if (actual != expected[i])
            {
                mismatchMask |= 1u << i;
                std::printf("page variant=%u x=%u addr=%u/%u/%u actual=%08x expected=%08x\n",
                    variant, i, gpu[i].address.page, gpu[i].address.slice, gpu[i].address.resolution,
                    actual, expected[i]);
            }
        }
        const uint32_t targetMask = variant == 1 ? 0 : variant == 4 ? 0b101 : 0b001;
        Check(mismatchMask == targetMask, "production page control/mutation readback mask");
        if (variant > 1)
            std::printf("MUTATION KILLED: %s\n", variant == 2 ? "page" : variant == 3 ? "slice" : "descriptor");
    }
    Check(tables[0].ptr && tables[1].ptr && tables[0].ptr != tables[1].ptr, "distinct ranges for two native slots");
    // Держим реальный Signal в очереди, пока второй слот растёт или откатывается.
    ComPtr<ID3D12Fence> gate;
    Require(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)), "descriptor gate");
    const auto fenceValue = owner.fenceValue + 1;
    AdaptivePlan inFlight{1,1,1,100,UINT64_MAX,0,{{1,1,128,128,1},{2,1,256,256,1}}};
    Require(pages[0].Prepare(inFlight, shapes, UINT64_MAX, UINT64_MAX, fenceValue), "in-flight arena");
    ShadowAddressPlan staged;
    ShadowDescriptors::Binding active;
    Require(descriptors.Stage(owner.fence, 0, pages[0], staged, active), "in-flight range");
    Require(owner.queue.Wait(gate.Get(), 1), "hold descriptor fence");
    struct OpenGate
    {
        ID3D12Fence& fence;
        ~OpenGate() { fence.Signal(1); }
    } openGate{*gate.Get()};
    Require(owner.queue.Signal(&owner.fence, fenceValue), "signal in-flight descriptors");
    owner.fenceValue = fenceValue;
    Require(descriptors.CommitSubmitted(owner.fence, fenceValue), "track queued signal");
    Check(owner.fence.GetCompletedValue() < fenceValue, "descriptor fence still pending");
    ShadowDescriptors::Binding rejected;
    Check(descriptors.Stage(owner.fence, 0, pages[0], staged, rejected) == DXGI_ERROR_WAS_STILL_DRAWING,
        "in-flight slot cannot be overwritten");

    const std::array<ShadowPageShape, 3> growing{{shapes[0], shapes[1], {512, 6}}};
    inFlight.lights.push_back({3,1,512,512,1});
    Require(pages[1].Prepare(inFlight, growing, UINT64_MAX, UINT64_MAX, fenceValue + 1), "grow other slot");
    Check(descriptors.Stage(owner.fence, 1, pages[1], staged, rejected) == E_OUTOFMEMORY && !rejected.table.ptr,
        "growth cannot overlap a live range");
    Require(pages[1].CancelUnsubmitted(), "rollback rejected growth");
    inFlight.lights.pop_back();
    Require(pages[1].Prepare(inFlight, shapes, UINT64_MAX, UINT64_MAX, fenceValue + 1), "fit other slot");
    Require(descriptors.Stage(owner.fence, 1, pages[1], staged, rejected), "other slot fits while first is live");
    Check(active.table.ptr != rejected.table.ptr, "in-flight ranges are disjoint");
    Require(descriptors.CancelUnsubmitted(), "discard unsubmitted descriptor range");
    Require(gate->Signal(1), "release queued descriptor fence");
    Require(frame.Drain(owner), "drain descriptor lifetime probe");
    std::puts("PASS: physical 128x128x6 / 256x256x9, divergent sampling, bounds, two slots, cancel/commit, gated lifetime");
}

void RunGatedDescriptorDraws(const Frame::Owner& owner)
{
    using namespace game_shadow12;
    auto& device = owner.device;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NumDescriptors = 7;
    ComPtr<ID3D12DescriptorHeap> heap;
    Require(device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)), "gated shared material heap");
    std::array<ShadowPageResources, 2> pages;
    ShadowDescriptors descriptors;
    Require(descriptors.Initialize(device, owner.fence, *heap.Get(), 2), "gated descriptor transaction");
    ShadowSamplingPass first(device, nullptr, 1, nullptr, 1, Mutation::None, true, false, heap.Get());
    ShadowSamplingPass second(device, nullptr, 1, nullptr, 1, Mutation::None, true, false, heap.Get());

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 2;
    ComPtr<ID3D12DescriptorHeap> rtvs;
    Require(device.CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvs)), "gated outputs heap");
    const auto stride = device.GetDescriptorHandleIncrementSize(rtvDesc.Type);
    D3D12_RESOURCE_DESC color{};
    color.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    color.Width = 8;
    color.Height = color.DepthOrArraySize = color.MipLevels = 1;
    color.SampleDesc.Count = 1;
    color.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    color.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES defaults{};
    defaults.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaults.CreationNodeMask = defaults.VisibleNodeMask = 1;
    struct Output
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Resource> texture;
        std::unique_ptr<Readback> pixels;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    };
    std::array<Output, 2> outputs;
    for (uint32_t slot = 0; slot < outputs.size(); ++slot)
    {
        auto& output = outputs[slot];
        Require(pages[slot].Initialize(device, owner.fence, slot), "gated slot initialize");
        Require(device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&output.allocator)),
            "gated command allocator");
        Require(device.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, output.allocator.Get(), nullptr,
            IID_PPV_ARGS(&output.list)), "gated command list");
        Require(device.CreateCommittedResource(&defaults, D3D12_HEAP_FLAG_NONE, &color,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&output.texture)), "gated output");
        output.rtv = rtvs->GetCPUDescriptorHandleForHeapStart();
        output.rtv.ptr += slot * stride;
        device.CreateRenderTargetView(output.texture.Get(), nullptr, output.rtv);
        output.pixels = std::make_unique<Readback>(device, color, 1);
    }

    ComPtr<ID3D12Fence> gate;
    Require(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)), "gated draw fence");
    // Слот 1 уже публиковал диапазон из двух страниц до начала задержанного кадра.
    const std::array<ShadowPageShape, 4> shapes{{{64,6},{128,6},{256,6},{512,6}}};
    AdaptivePlan logical{1,1,1,1,UINT64_MAX,0,{{1,1,64,64,1},{2,1,128,128,1}}};
    const auto previousFence = owner.fenceValue + 1;
    ShadowAddressPlan previousAddresses;
    ShadowDescriptors::Binding previousBinding;
    Require(pages[1].Prepare(logical, shapes, UINT64_MAX, UINT64_MAX, previousFence), "prepare prior two-page range");
    Require(descriptors.Stage(owner.fence, 1, pages[1], previousAddresses, previousBinding), "stage prior two-page range");
    Check(previousBinding.pageCount == 2, "previous second-slot descriptor count");
    Require(owner.queue.Signal(&owner.fence, previousFence), "signal prior two-page range");
    owner.fenceValue = previousFence;
    Require(descriptors.CommitSubmitted(owner.fence, previousFence), "commit prior two-page range");
    Require(owner.fence.SetEventOnCompletion(previousFence, owner.event), "wait prior two-page range");
    Check(WaitForSingleObject(owner.event, 10000) == WAIT_OBJECT_0, "prior range completed");
    ++logical.frame;

    const auto oldFence = owner.fenceValue + 1;
    const auto newFence = oldFence + 1;
    Require(owner.queue.Wait(gate.Get(), 1), "gate GPU before either draw");
    struct FinishGated
    {
        const Frame::Owner& owner;
        ID3D12Fence& gate;
        ~FinishGated()
        {
            gate.Signal(1);
            if (owner.fence.GetCompletedValue() < owner.fenceValue &&
                SUCCEEDED(owner.fence.SetEventOnCompletion(owner.fenceValue, owner.event)))
                WaitForSingleObject(owner.event, 10000);
        }
    } cleanup{owner, *gate.Get()};
    std::array<ShadowDescriptors::Binding, 2> bindings{};
    const auto submit = [&](uint32_t slot, ShadowSamplingPass& pass, uint64_t value)
    {
        auto& arena = pages[slot];
        auto& output = outputs[slot];
        ShadowAddressPlan addresses;
        Require(arena.Prepare(logical, shapes, UINT64_MAX, UINT64_MAX, value), "prepare gated pages");
        Require(descriptors.Stage(owner.fence, slot, arena, addresses, bindings[slot]), "stage gated descriptors");
        Check(addresses.views.size() == 2 + slot, "gated page count");
        std::array<LocalShadowGpuProjection, 8> projections{};
        for (uint32_t i = 0; i < addresses.views.size(); ++i)
        {
            const auto& address = addresses.views[i];
            ShadowPageResources::View page;
            Check(arena.GetView(addresses.pageIds[address.page], address.slice, page), "gated physical page");
            auto& list = *output.list.Get();
            Transition(list, *page.resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, page.slice);
            list.ClearDepthStencilView(page.dsv, D3D12_CLEAR_FLAG_DEPTH, (i + slot) % 2 ? .75f : .25f, 0, 0, nullptr);
            Transition(list, *page.resource, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, page.slice);
        }
        for (uint32_t i = 0; i < projections.size(); ++i)
            projections[i].address = addresses.views[i % addresses.views.size()];
        pass.DrawPages(device, *output.list.Get(), output.rtv, bindings[slot], projections);
        Transition(*output.list.Get(), *output.texture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        output.pixels->Copy(*output.list.Get(), *output.texture.Get(), 0);
        Require(output.list->Close(), "close gated draw");
        ID3D12CommandList* lists[]{output.list.Get()};
        owner.queue.ExecuteCommandLists(1, lists);
        const auto signal = owner.queue.Signal(&owner.fence, value);
        if (FAILED(signal))
            descriptors.Quarantine();
        Require(signal, "signal gated draw");
        owner.fenceValue = value;
        Require(descriptors.CommitSubmitted(owner.fence, value), "commit gated draw range");
    };
    submit(0, first, oldFence);
    Check(owner.fence.GetCompletedValue() < oldFence, "first real draw remains queued");
    logical.lights.push_back({3,1,256,256,1});
    logical.lights.push_back({4,1,512,512,1});
    Require(pages[1].Prepare(logical, shapes, UINT64_MAX, UINT64_MAX, newFence), "prepare overflowing second range");
    ShadowAddressPlan rejected;
    Check(descriptors.Stage(owner.fence, 1, pages[1], rejected, bindings[1]) == E_OUTOFMEMORY &&
        !bindings[1].table.ptr && owner.fenceValue == oldFence, "capacity rejection leaves queue unchanged");
    Require(pages[1].CancelUnsubmitted(), "rollback oversized second range");
    logical.lights.pop_back();
    submit(1, second, newFence);
    Check(bindings[0].pageCount == 2 && bindings[1].pageCount == 3 &&
        bindings[0].table.ptr != bindings[1].table.ptr && bindings[1].table.ptr < previousBinding.table.ptr &&
        owner.fence.GetCompletedValue() < oldFence,
        "second range grows while first real draw is still pending");
    Require(gate->Signal(1), "release both real draws");
    Require(owner.fence.SetEventOnCompletion(newFence, owner.event), "wait both range reads");
    Check(WaitForSingleObject(owner.event, 10000) == WAIT_OBJECT_0, "gated range completion");
    Require(device.GetDeviceRemovedReason(), "device survives gated ranges");
    for (uint32_t slot = 0; slot < outputs.size(); ++slot)
        for (uint32_t pixel = 0; pixel < 8; ++pixel)
        {
            const auto page = pixel % (2 + slot);
            const auto expected = (page + slot) % 2 ? 0xff00ffff : 0xff00ff00;
            Check(outputs[slot].pixels->Pixel(0, pixel, 0) == expected, "old/new gated range readback");
        }
    std::puts("PASS: real draws behind fence, live two-page range and successful three-page range, rollback and 16 readbacks");
}

struct DepthPacketPipeline
{
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;

    explicit DepthPacketPipeline(ID3D12Device& device, bool minimumDepth = false)
    {
        auto vertex = Compile(R"(
cbuffer Packet : register(b0) { float Depth; }
float4 main(uint id : SV_VertexID) : SV_Position {
    float2 vertices[3] = {float2(-1,-1), float2(-1,3), float2(3,-1)};
    return float4(vertices[id], Depth, 1);
})", "main", "vs_5_0");
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants.Num32BitValues = 1;
        D3D12_ROOT_SIGNATURE_DESC signature{};
        signature.NumParameters = 1;
        signature.pParameters = &parameter;
        ComPtr<ID3DBlob> blob;
        Require(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr),
            "serialize depth packet signature");
        Require(device.CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)),
            "create depth packet signature");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root.Get();
        desc.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.DepthStencilState.DepthEnable = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthStencilState.DepthFunc = minimumDepth ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS;
        desc.SampleDesc.Count = 1;
        desc.SampleMask = UINT_MAX;
        Require(device.CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)), "create depth packet PSO");
    }
};

std::vector<uint32_t> RunScalable(HWND window, IDXGIFactory2& factory, const Frame::Owner& owner,
    const DepthPacketPipeline& pass, const char* mode, uint32_t lights)
{
    using namespace game_shadow12;
    Check(_putenv_s("OGSR_DX12_GAME_COMMAND_LISTS", mode) == 0, "set scalable mode");
    Check(_putenv_s("OGSR_DX12_GAME_METRICS", "1") == 0, "enable scalable timestamps");
    auto& device = owner.device;
    std::array<ShadowPageResources, Frame::kFrameCount> pages;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NumDescriptors = 24;
    ComPtr<ID3D12DescriptorHeap> heap;
    Require(device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)), "scalable material heap");
    ShadowDescriptors descriptors;
    Require(descriptors.Initialize(device, owner.fence, *heap.Get(), 2), "scalable descriptors");
    for (uint32_t slot = 0; slot < pages.size(); ++slot)
        Require(pages[slot].Initialize(device, owner.fence, slot), "scalable slot arena");
    Frame frame;
    Require(frame.Initialize(window, factory, owner), "initialize scalable frame");
    gr::jobs::JobSystem jobs;
    if (std::strcmp(mode, "parallel") == 0)
    {
        gr::jobs::JobConfig config;
        config.backgroundWorkers = 2;
        config.automaticWorkers = false;
        Check(jobs.Initialize(config), "initialize scalable GR");
    }

    const std::array<ShadowPageShape, 2> shapes{{{128, 12}, {256, 12}}};
    AdaptivePlan logical{1, 1, 0, 1, UINT64_MAX, 0, {}};
    for (uint32_t i = 0; i < lights; ++i)
    {
        const uint32_t size = i % 2 ? 256 : 128;
        logical.lights.push_back({i + 1u, 6, size, size, 1});
    }
    const uint32_t count = lights * 6;
    std::vector<float> depths(count);
    std::vector<ShadowPacketRange> packets(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        depths[i] = static_cast<float>(i + 1) / 64.0f;
        packets[i] = {i, 1};
    }

    std::vector<uint32_t> outcome;
    std::array<std::vector<ID3D12Resource*>, Frame::kFrameCount> previous;
    std::array<std::vector<ID3D12GraphicsCommandList*>, Frame::kFrameCount> previousLists;
    bool timingRead = false, physicalReused = false;
    for (uint32_t iteration = 0; iteration < 4; ++iteration)
    {
        Frame::Recording recording;
        Check(frame.Begin(window, owner, recording, count) == S_OK, "begin scalable frame");
        auto& arena = pages[recording.slot];
        if (iteration && frame.LastGpuTiming().present)
        {
            const auto& timing = frame.LastGpuTiming();
            Check(timing.views.size() == count && timing.milliseconds >= timing.shadowMilliseconds,
                "dynamic shadow timestamps");
            for (uint32_t i = 0; i < count; ++i)
                Check(timing.views[i].projection == ReservedCsmProjections + i, "timestamp projection identity");
            timingRead = true;
        }

        logical.frame = iteration + 1;
        const auto fenceBefore = owner.fenceValue;
        Require(arena.Prepare(logical, shapes, UINT64_MAX, UINT64_MAX, fenceBefore + 1), "prepare scalable pages");
        ShadowAddressPlan addresses;
        ShadowDescriptors::Binding binding;
        Require(recording.StageShadowPages(descriptors, arena, addresses, binding), "stage scalable pages");
        Check(binding.pageCount > 1 && addresses.views.size() == count, "scalable full page placement");
        std::vector<ScheduledShadowView> schedule;
        Check(PlanShadowViews(addresses, packets, depths.size(), schedule), "plan scalable shadow schedule");

        const auto dispatch = [&](uint32_t size, const auto& work) -> HRESULT
        {
            auto group = jobs.CreateGroup();
            try
            {
                group.ParallelFor(size, 1, work);
                group.Wait();
            }
            catch (...)
            {
                group.DrainNoThrow();
                return E_FAIL;
            }
            return S_OK;
        };
        std::vector<ID3D12GraphicsCommandList*> lists(count);
        const auto draw = [&](ID3D12GraphicsCommandList& list, const ScheduledShadowView& view,
                              const ShadowPageResources::View&) -> HRESULT
        {
            if (iteration == 0 && view.projection == ReservedCsmProjections)
                return E_ABORT;
            lists[view.projection - ReservedCsmProjections] = &list;
            list.SetPipelineState(pass.pipeline.Get());
            list.SetGraphicsRootSignature(pass.root.Get());
            list.SetGraphicsRoot32BitConstant(0, std::bit_cast<uint32_t>(depths[view.packets.first]), 0);
            list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list.DrawInstanced(3, 1, 0, 0);
            return S_OK;
        };
        const auto recorded = RecordShadowSchedule(recording, arena, schedule, draw, dispatch);
        if (iteration == 0)
        {
            Check(recorded == E_ABORT && !arena.PreparedPlan() && owner.fenceValue == fenceBefore &&
                frame.Present(owner, 1) == E_FAIL, "partial shadow recording rollback without submission");
            continue;
        }
        Require(recorded, "record scalable shadow schedule");

        std::vector<std::unique_ptr<Readback>> readbacks(binding.pageCount);
        std::vector<ID3D12Resource*> resources(binding.pageCount);
        for (uint32_t i = 0; i < schedule.size(); ++i)
        {
            const auto& view = schedule[i];
            const auto ordinal = addresses.views[i].page;
            ShadowPageResources::View target;
            Check(arena.GetView(view.page, view.slice, target), "read scalable shadow view");
            if (!readbacks[ordinal])
            {
                const auto desc = target.resource->GetDesc();
                readbacks[ordinal] = std::make_unique<Readback>(device, desc, desc.DepthOrArraySize);
                resources[ordinal] = target.resource;
            }
            Transition(*recording.list, *target.resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_SOURCE, view.slice);
            readbacks[ordinal]->Copy(*recording.list, *target.resource, view.slice);
            Transition(*recording.list, *target.resource, D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, view.slice);
        }
        auto& prior = previous[recording.slot];
        if (!prior.empty())
        {
            Check(prior == resources && arena.PreparedPlan()->create.empty(), "fenced reuse of physical pages");
            physicalReused = true;
        }
        prior = resources;
        if (recording.UsesShadowLists())
        {
            const std::unordered_set<ID3D12GraphicsCommandList*> distinct(lists.begin(), lists.end());
            Check(distinct.size() == count, "exclusive command list for each recorded range");
        }
        auto& priorLists = previousLists[recording.slot];
        if (!priorLists.empty())
            Check(priorLists == lists, "fenced reuse of command list pool");
        priorLists = lists;
        recording.StampSceneStart();
        Require(frame.Present(owner, 1), "submit scalable draw packets");
        Require(frame.Drain(owner), "drain scalable shadow frame");
        outcome.clear();
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& view = addresses.views[i];
            const auto actual = readbacks[view.page]->Pixel(view.slice, view.resolution / 2, view.resolution / 2);
            Check(actual == std::bit_cast<uint32_t>(depths[i]), "distinct depth for every shadow projection");
            outcome.push_back(actual);
        }
    }
    Check(timingRead && physicalReused, "timing and reuse observed at recycled slot");
    if (jobs.IsInitialized())
        jobs.Shutdown();
    std::printf("PASS: %s %u depth draws, dynamic timestamps, fence reuse and partial cancellation\n", mode, count);
    return outcome;
}

std::vector<uint32_t> RunMixedRuntime(HWND window, IDXGIFactory2& factory, const Frame::Owner& owner,
    IDXGIAdapter3& adapter, const char* mode, const DepthPacketPipeline& depth)
{
    using namespace game_shadow12;
    constexpr uint32_t sunCount = 2, fullCount = 42;
    auto& device = owner.device;
    Check(_putenv_s("OGSR_DX12_GAME_COMMAND_LISTS", mode) == 0, "mixed recording mode");
    Check(_putenv_s("OGSR_DX12_GAME_METRICS", "1") == 0, "mixed timestamps");
    gr::jobs::JobSystem jobs;
    if (std::strcmp(mode, "parallel") == 0)
    {
        gr::jobs::JobConfig config;
        config.automaticWorkers = false;
        config.backgroundWorkers = 2;
        Check(jobs.Initialize(config), "mixed GR");
    }
    const auto dispatch = [&](uint32_t count, const auto& work) -> HRESULT
    {
        auto group = jobs.CreateGroup();
        try { group.ParallelFor(count, 1, work); group.Wait(); }
        catch (...) { group.DrainNoThrow(); return E_FAIL; }
        return S_OK;
    };

    D3D12_RESOURCE_DESC sunDesc{};
    Require(DescribeShadowPage({8, 6}, sunDesc), "mixed CSM resource description");
    sunDesc.DepthOrArraySize = sunCount;
    D3D12_HEAP_PROPERTIES defaults{};
    defaults.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaults.CreationNodeMask = defaults.VisibleNodeMask = 1;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1;
    ComPtr<ID3D12Resource> sun;
    Require(device.CreateCommittedResource(&defaults, D3D12_HEAP_FLAG_NONE, &sunDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&sun)), "mixed CSM texture");
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = sunCount;
    ComPtr<ID3D12DescriptorHeap> sunDsv;
    Require(device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&sunDsv)), "mixed CSM DSVs");
    const auto stride = device.GetDescriptorHandleIncrementSize(heapDesc.Type);
    std::array<ShadowRecordingPass, sunCount> csm{};
    for (uint32_t i = 0; i < sunCount; ++i)
    {
        auto dsv = sunDsv->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += i * stride;
        D3D12_DEPTH_STENCIL_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_D32_FLOAT;
        view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray = {0, i, 1};
        device.CreateDepthStencilView(sun.Get(), &view, dsv);
        csm[i] = {i, {sun.Get(), dsv, {}, i, 8}};
    }

    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NumDescriptors = 24;
    ComPtr<ID3D12DescriptorHeap> heap;
    Require(device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)), "mixed material heap");
    ShadowSamplingPass sampling(device, sun.Get(), sunCount, nullptr, 1, Mutation::None, true, false, heap.Get());
    AdaptiveShadowRuntime runtime;
    ShadowDescriptors descriptors;
    Require(descriptors.Initialize(device, owner.fence, *heap.Get(), 2), "mixed descriptors");
    Frame frame;
    Require(frame.Initialize(window, factory, owner), "mixed frame");

    ShadowPageSpec low{};
    Require(QueryShadowPageSpec(device, {128,6}, low), "cost of lower tier");
    std::vector<AdaptiveCandidate> candidates;
    std::vector<uint64_t> lightIds;
    for (uint32_t i = 0; i < fullCount / 6; ++i)
    {
        const double res = i % 2 ? 256 : 128;
        candidates.push_back({i + 1, static_cast<double>(fullCount - i), res, 6});
        lightIds.push_back(i + 1);
    }

    std::array<bool, Frame::kFrameCount> used{};
    std::vector<uint32_t> readbacks;
    for (uint32_t iteration = 0; iteration < 5; ++iteration)
    {
        Frame::Recording recording;
        Check(frame.Begin(window, owner, recording) == S_OK, "mixed begin without prior timing hint");
        const auto& arena = runtime.Pages(recording.slot);
        const auto planned = iteration < 3 ? UINT64_MAX : iteration == 3 ? low.bytes + 5 : 0;
        const uint32_t expectedCount = iteration < 3 ? fullCount : iteration == 3 ? 6 : 0;
        const auto previous = owner.fenceValue;
        const auto oldBytes = arena.CurrentBytes();
        ShadowAddressPlan addresses;
        ShadowDescriptors::Binding binding;
        const auto prepare = runtime.Prepare(owner, adapter, recording, descriptors, candidates, 1, 1,
            iteration + 1, sunCount, addresses, binding, planned);
        Require(prepare, "coordinate mixed frame budget and stage");
        Check(prepare == S_OK, "WARP reports a local DXGI budget");
        Check(addresses.views.size() == expectedCount &&
            (expectedCount != 0 || (!binding.table.ptr && !binding.shapes && !binding.pageCount)),
            "budget shrink and empty descriptor binding");
        std::vector<uint32_t> mapping;
        Check(ReconcileAdaptiveAddresses(lightIds, addresses, mapping), "reconcile mixed light addresses");
        const auto admitted = std::count_if(mapping.begin(), mapping.end(), [](auto first) { return first != 0; });
        Check(admitted * 6 == expectedCount, "physical denial omits complete groups");

        std::vector<ShadowPacketRange> ranges(expectedCount);
        for (uint32_t i = 0; i < expectedCount; ++i)
            ranges[i] = {i, 1};
        std::vector<ScheduledShadowView> schedule;
        Check(PlanShadowViews(addresses, ranges, expectedCount, schedule), "mixed local schedule");
        std::vector<ShadowRecordingPass> passes(csm.begin(), csm.end());
        std::vector<float> depths{.25f,.75f};
        for (const auto& view : schedule)
        {
            ShadowPageResources::View target;
            Check(arena.GetView(view.page, view.slice, target), "mixed local page slice");
            passes.push_back({view.projection, target});
            depths.push_back(static_cast<float>((view.light - 1) * 6 + view.face + 1) / 64);
        }

        uint32_t batches = 0;
        const bool batching = std::strcmp(mode, "parallel") == 0;
        const auto result = RecordShadowPasses(recording, passes, [&](ID3D12GraphicsCommandList& list, uint32_t index)
        {
            if (!iteration && index == sunCount)
                return E_ABORT;
            list.SetPipelineState(depth.pipeline.Get());
            list.SetGraphicsRootSignature(depth.root.Get());
            list.SetGraphicsRoot32BitConstant(0, std::bit_cast<uint32_t>(depths[index]), 0);
            list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list.DrawInstanced(3, 1, 0, 0);
            return S_OK;
        }, dispatch, {batching, 3, &batches});
        if (!iteration)
        {
            Check(result == E_ABORT && !arena.PreparedPlan() && arena.CurrentBytes() == oldBytes &&
                owner.fenceValue == previous && frame.Present(owner, 1) == E_FAIL, "mixed cancellation has no partial publication");
            continue;
        }
        Require(result, "record one mixed CSM/local batch");
        Check(batches == (batching ? (std::min)(3u, static_cast<uint32_t>(passes.size())) : passes.size()),
            "actual mixed batch count");
        used[recording.slot] = true;
        if (iteration >= 3)
            Check(frame.LastGpuTiming().views.size() == fullCount + sunCount, "combined timestamp ID count after slot reuse");

        Readback sunRead(device, sunDesc, sunCount);
        std::vector<std::unique_ptr<Readback>> localRead(binding.pageCount);
        for (uint32_t i = 0; i < passes.size(); ++i)
        {
            const auto& view = passes[i].target;
            Readback* destination = &sunRead;
            if (i >= sunCount)
            {
                auto& target = localRead[addresses.views[i - sunCount].page];
                if (!target)
                {
                    const auto desc = view.resource->GetDesc();
                    target = std::make_unique<Readback>(device, desc, desc.DepthOrArraySize);
                }
                destination = target.get();
            }
            Transition(*recording.list, *view.resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_SOURCE, view.slice);
            destination->Copy(*recording.list, *view.resource, view.slice);
            Transition(*recording.list, *view.resource, D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, view.slice);
        }

        std::vector<LocalShadowGpuProjection> probes;
        std::vector<float> probeDepths;
        if (!schedule.empty())
        {
            // Шесть соседних пикселей читают разные страницы в одном draw.
            for (uint32_t i = 0; i < 6; ++i)
            {
                const auto local = static_cast<uint32_t>(i * (schedule.size() - 1) / 5);
                LocalShadowGpuProjection probe{};
                probe.address = addresses.views[local];
                probes.push_back(probe);
                probeDepths.push_back(depths[sunCount + local]);
            }
        }
        recording.StampSceneStart();
        sampling.DrawPages(device, *recording.list, recording.rtv, binding, probes, sunCount);
        Readback colorRead(device, recording.color->GetDesc(), 1);
        Transition(*recording.list, *recording.color, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        colorRead.Copy(*recording.list, *recording.color, 0);
        Transition(*recording.list, *recording.color, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Require(frame.Present(owner, 1), "present mixed schedule");
        Require(frame.Drain(owner), "wait mixed readback");
        Check((arena.CurrentBytes() == 0) == (expectedCount == 0) && !arena.PreparedPlan(), "mixed budget committed on Signal");

        for (uint32_t i = 0; i < passes.size(); ++i)
        {
            const auto& view = passes[i].target;
            auto& source = i < sunCount ? sunRead : *localRead[addresses.views[i - sunCount].page];
            const auto actual = source.Pixel(view.slice, view.resolution / 2, view.resolution / 2);
            Check(actual == std::bit_cast<uint32_t>(depths[i]), "mixed physical depth");
            readbacks.push_back(actual);
        }
        for (uint32_t i = 0; i < 8; ++i)
        {
            const uint32_t wanted = i < sunCount ? (i ? 0xff00ffff : 0xff00ff00) :
                probes.empty() ? 0xff0000ff : probeDepths[i - sunCount] >= .5f ? 0xff00ffff : 0xff00ff00;
            const auto actual = colorRead.Pixel(0, i, 0);
            if (actual != wanted)
                std::fprintf(stderr, "mixed stage=%u x=%u actual=%08x expected=%08x\n", iteration, i, actual, wanted);
            Check(actual == wanted, "production CSM and multi-page sampling");
            readbacks.push_back(actual);
        }
        std::printf("PASS: %s slot=%u CSM=%u locals=%u pages=%u budget=%llu\n", mode, recording.slot,
            sunCount, expectedCount, binding.pageCount, static_cast<unsigned long long>(planned));
    }
    Check(used[0] && used[1], "mixed frame exercises two slots");
    if (jobs.IsInitialized())
        jobs.Shutdown();
    return readbacks;
}

// Cache-off и cache-on записывают собственные карты для каждого slot.
std::vector<uint32_t> RunShadowCache(HWND window, IDXGIFactory2& factory, const Frame::Owner& owner,
    IDXGIAdapter3& adapter, const DepthPacketPipeline& depth, bool enabled, bool staticSplit = false)
{
    using namespace game_shadow12;
    constexpr uint32_t count = 2;
    auto& device = owner.device;
    Check(_putenv_s("OGSR_DX12_GAME_COMMAND_LISTS", "legacy") == 0, "cache recording mode");
    D3D12_RESOURCE_DESC desc{};
    Require(DescribeShadowPage({8,6}, desc), "cache page description");
    desc.DepthOrArraySize = count * 2;
    D3D12_HEAP_PROPERTIES props{};
    props.Type = D3D12_HEAP_TYPE_DEFAULT;
    props.CreationNodeMask = props.VisibleNodeMask = 1;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT; clear.DepthStencil.Depth = 1;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; hd.NumDescriptors = Frame::kFrameCount * count * 2;
    ComPtr<ID3D12DescriptorHeap> dsv;
    Require(device.CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsv)), "cache DSV heap");
    const auto stride = device.GetDescriptorHandleIncrementSize(hd.Type);
    std::array<ComPtr<ID3D12Resource>, Frame::kFrameCount> maps;
    std::array<std::array<ShadowRecordingPass, count>, Frame::kFrameCount> targets{}, sources{};
    std::array<std::unique_ptr<ShadowSamplingPass>, Frame::kFrameCount> sampling;
    std::array<ShadowContentCache, Frame::kFrameCount> caches;
    for (uint32_t slot = 0; slot < Frame::kFrameCount; ++slot)
    {
        Require(device.CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&maps[slot])), "cache depth array");
        for (uint32_t i = 0; i < count * 2; ++i)
        {
            auto handle = dsv->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += (slot * count * 2 + i) * stride;
            D3D12_DEPTH_STENCIL_VIEW_DESC view{};
            view.Format = DXGI_FORMAT_D32_FLOAT; view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray = {0, i, 1};
            device.CreateDepthStencilView(maps[slot].Get(), &view, handle);
            auto& entry = i < count ? targets[slot][i] : sources[slot][i - count];
            entry = {i, {maps[slot].Get(), handle, {}, i, 8}};
        }
        sampling[slot] = std::make_unique<ShadowSamplingPass>(device, maps[slot].Get(), count,
            nullptr, 1, Mutation::None, true);
    }
    hd = {}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; hd.NumDescriptors = 24;
    ComPtr<ID3D12DescriptorHeap> heap;
    Require(device.CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "cache descriptor heap");
    ShadowDescriptors descriptors;
    Require(descriptors.Initialize(device, owner.fence, *heap.Get(), 2), "cache descriptors");
    AdaptiveShadowRuntime runtime;
    Frame frame;
    Require(frame.Initialize(window, factory, owner), "cache frame");
    std::array<bool, Frame::kFrameCount> used{};
    std::vector<uint32_t> observed;
    for (uint32_t iteration = 0; iteration < 12; ++iteration)
    {
        const uint32_t phase = iteration / 2;
        Frame::Recording recording;
        Require(frame.Begin(window, owner, recording), "cache begin");
        const uint32_t slot = recording.slot; used[slot] = true;
        auto& cache = caches[slot];
        std::vector<ShadowContentView> views(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            auto& v = views[i]; v.resource = reinterpret_cast<uint64_t>(maps[slot].Get());
            v.resourceGeneration = v.session = v.epoch = 1; v.slice = i; v.resolution = 8;
            v.light = i + 1; v.lightGeneration = 1; v.matrixBits[0] = 0x3f800000; v.known = true;
            ShadowContentCaster caster; caster.identity = 100 + i; caster.generation = 1; caster.known = true;
            caster.geometry = {1}; caster.material = {1}; caster.transform = {1}; caster.raster = {1};
            v.casters.push_back(std::move(caster));
        }
        if (phase >= 2) views[0].lightGeneration = 2;
        if (phase >= 3) views[0].matrixBits[0] = 0x40000000;
        if (phase >= 4) views[0].casters[0].transform[0] = 2;
        if (phase >= 5) views[0].known = false;
        const float depths[count] = {phase < 2 ? .25f : phase == 2 ? .75f :
            phase == 3 ? .25f : phase == 4 ? .75f : .25f, .75f};
        const auto stage = [&]()
        {
            ShadowAddressPlan addresses; ShadowDescriptors::Binding binding;
            Require(runtime.Prepare(owner, adapter, recording, descriptors, {}, 1, 1, iteration + 1,
                count, addresses, binding), "cache stage descriptor pages");
            Check(cache.Prepare(views, owner.fenceValue + 1), "cache prepare");
            Require(recording.StageShadowContentCache(cache, owner.fenceValue + 1), "cache stage");
        };
        if (enabled && iteration == 4)
        {
            stage();
            Check(cache.Stats().dirty == 1 && cache.Stats().hits == 1, "cache cancellation candidate");
            const uint64_t before = owner.fenceValue;
            Require(frame.Cancel(), "cancel staged cache");
            Check(!cache.Prepared() && owner.fenceValue == before, "cancel leaves cache unpublished");
            Require(frame.Begin(window, owner, recording), "begin after cache cancel");
            Check(recording.slot == slot, "cancel preserves slot");
        }
        std::vector<ShadowRecordingPass> dirty;
        if (enabled)
        {
            stage();
            const uint32_t expected = phase == 0 ? 2 : phase == 1 ? 0 : 1;
            Check(cache.Stats().dirty == expected && cache.Stats().hits == count - expected, "cache hit decisions");
            if (phase >= 2)
            {
                const uint32_t reason = phase == 2 ? ShadowContentLight : phase == 3 ? ShadowContentMatrix :
                    phase == 4 ? ShadowContentCasters : ShadowContentUnknown;
                Check((cache.Reasons()[0] & reason) != 0 && cache.Reasons()[1] == 0, "cache invalidation reason");
            }
            for (uint32_t i = 0; i < count; ++i) if (cache.Reasons()[i]) dirty.push_back(targets[slot][i]);
        }
        else dirty.assign(targets[slot].begin(), targets[slot].end());
        const auto dispatch = [](uint32_t n, const auto& work) -> HRESULT { work(0, n, 0); return S_OK; };
        Require(RecordShadowPasses(recording, dirty, [&](ID3D12GraphicsCommandList& list, uint32_t index)
        {
            const auto draw = [&](float z)
            {
                list.SetPipelineState(depth.pipeline.Get()); list.SetGraphicsRootSignature(depth.root.Get());
                list.SetGraphicsRoot32BitConstant(0, std::bit_cast<uint32_t>(z), 0);
                list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list.DrawInstanced(3, 1, 0, 0);
            };
            const auto projection = dirty[index].projection;
            if (staticSplit)
                CopyStaticShadow(list, sources[slot][projection].target, dirty[index].target, phase == 0,
                    [&] { draw(.75f); });
            else
                draw(.75f);
            if (depths[projection] < .75f)
                draw(depths[projection]);
            return S_OK;
        }, dispatch), "record cached maps");
        Readback depthRead(device, desc, count);
        for (uint32_t i = 0; i < count; ++i)
        {
            Transition(*recording.list, *maps[slot].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_SOURCE, i);
            depthRead.Copy(*recording.list, *maps[slot].Get(), i);
            Transition(*recording.list, *maps[slot].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, i);
        }
        recording.StampSceneStart();
        sampling[slot]->DrawPages(device, *recording.list, recording.rtv, {}, {}, count);
        Readback colorRead(device, recording.color->GetDesc(), 1);
        Transition(*recording.list, *recording.color, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        colorRead.Copy(*recording.list, *recording.color, 0);
        Transition(*recording.list, *recording.color, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Require(frame.Present(owner, 1), "submit cache frame");
        if (enabled) Check(!cache.Prepared(), "cache committed after Signal");
        Require(frame.Drain(owner), "drain cache frame");
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t actual = depthRead.Pixel(i, 4, 4);
            Check(actual == std::bit_cast<uint32_t>(depths[i]), "cache depth"); observed.push_back(actual);
            const uint32_t sampled = colorRead.Pixel(0, i, 0);
            Check(sampled == (depths[i] >= .5f ? 0xff00ffff : 0xff00ff00), "cache production sampling");
            observed.push_back(sampled);
        }
    }
    Check(used[0] && used[1], "cache exercised both slots");
    return observed;
}

std::vector<uint32_t> RunClusters(HWND window, IDXGIFactory2& factory, const Frame::Owner& owner,
    int mode)
{
    auto& device = owner.device;
    Frame frame;
    Require(frame.Initialize(window, factory, owner), "cluster frame");
    Frame::Recording recording;
    Require(frame.Begin(window, owner, recording), "cluster frame begin");

    const auto vertex = Compile(R"(
float4 vs(uint id : SV_VertexID) : SV_Position
{
    const float2 p[3] = {float2(-1,-1), float2(-1,3), float2(3,-1)};
    return float4(p[id], 0, 1);
}
)", "vs", "vs_5_0");
    const std::string hlsl = "#define LEGACY_DIAGNOSTIC_LIGHT_SCALE 16\n" +
        std::string(kFrameShaderConstants) + kScenePixelShader + R"(
float4 clusterProbe(float4 pixel : SV_Position) : SV_Target
{
    float2 uv = pixel.xy / float2(ClusterTiles.xy);
    float z = uv.x < 0.5f ? 4.0f : uv.y < 0.5f ? 8.0f : 15.0f;
    float3 world = float3((2.0f * uv.x - 1.0f) * z, (1.0f - 2.0f * uv.y) * z, z);
    return float4(LocalLighting(float3(0,0,1), world, pixel.xy, false, false), 1);
}
)";
    const auto pixel = Compile(hlsl.c_str(), "clusterProbe", "ps_5_1");
    ComPtr<ID3D12ShaderReflection> reflection;
    Require(D3DReflect(pixel->GetBufferPointer(), pixel->GetBufferSize(), IID_ID3D12ShaderReflection,
        reinterpret_cast<void**>(reflection.GetAddressOf())), "reflect clustered shader");
    for (const auto [name, reg] : {std::pair{"LocalLights", 13u}, {"ClusterRanges", 16u}, {"ClusterLightIndices", 17u}})
    {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        Require(reflection->GetResourceBindingDescByName(name, &binding), name);
        Check(binding.Type == D3D_SIT_STRUCTURED && binding.BindPoint == reg && binding.BindCount == 1,
            "clustered SRV register");
    }
    D3D12_ROOT_PARAMETER params[5]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants = {1, 0, 4};
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    const uint32_t regs[] = {13, 16, 17};
    for (size_t i = 0; i < std::size(regs); ++i)
    {
        params[i + 2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i + 2].Descriptor.ShaderRegister = regs[i];
        params[i + 2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC signature{};
    signature.NumParameters = static_cast<UINT>(std::size(params));
    signature.pParameters = params;
    ComPtr<ID3DBlob> blob;
    Require(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr),
        "cluster root serialization");
    ComPtr<ID3D12RootSignature> root;
    Require(device.CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)),
        "cluster root signature");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root.Get();
    desc.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = recording.color->GetDesc().Format;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    ComPtr<ID3D12PipelineState> pipeline;
    Require(device.CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)), "cluster PSO");

    gamecluster12::Config config{recording.width, recording.height, 6, 7, 12, 0.5f, 32.f};
    config.viewProjection[0] = config.viewProjection[5] = config.viewProjection[11] = 1.f;
    config.viewProjection[10] = config.farDepth / (config.farDepth - config.nearDepth);
    config.viewProjection[14] = -config.nearDepth * config.viewProjection[10];
    const std::array<gamecluster12::Light, 5> volumes = {{
        {-2.f, 0.f, 5.f, 3.f}, {3.f, 0.f, 9.f, 4.f}, {0.f, 0.f, 0.f, 5.f},
        {0.f, -5.f, 16.f, 4.f}, {100.f, 100.f, 5.f, 1.f}
    }};
    std::array<LocalLightGpu, volumes.size()> lights{};
    for (uint32_t i = 0; i < lights.size(); ++i)
    {
        const auto& volume = volumes[i];
        auto& light = lights[i];
        light.positionInverseRangeSquared = {volume.x, volume.y, volume.z, 1.f / (volume.radius * volume.radius)};
        light.colorInnerCos = {i == 0 ? .75f : 0.f, i == 1 ? .75f : 0.f, i == 3 ? .75f : 0.f, .75f};
        light.directionOuterCos = {0, 0, -1, .1f};
        light.type[0] = i == 1;
    }
    gamecluster12::Result clusters;
    Check(gamecluster12::Build(config, volumes, clusters), "WARP cluster CPU lists");
    if (mode == 2)
        for (auto& entry : clusters.grid)
            entry.count = 0; // Мутация, обнаруживаемая сравнением изображений.
    if (mode == 3)
        for (auto& entry : clusters.grid)
            entry.offset = UINT32_MAX; // Некорректный диапазон включает полный цикл.
    SceneFrameConstants cb{};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            cb.viewProjection.m[row][col] = config.viewProjection[col * 4 + row];
    cb.lightingFlags[2] = static_cast<uint32_t>(lights.size());
    cb.clusterTiles[0] = config.width;
    cb.clusterTiles[1] = config.height;
    cb.clusterTiles[2] = config.tileWidth;
    cb.clusterTiles[3] = config.tileHeight;
    cb.clusterGrid[0] = clusters.tilesX;
    cb.clusterGrid[1] = clusters.tilesY;
    cb.clusterGrid[2] = mode == 0 ? 0 : config.slices;
    cb.clusterGrid[3] = static_cast<uint32_t>(clusters.indices.size());
    cb.clusterDepth[0] = config.nearDepth;
    cb.clusterDepth[1] = config.farDepth;
    cb.clusterDepth[2] = config.slices / std::log2(config.farDepth / config.nearDepth);
    game_shadow12::FrameStructuredUpload frameData, lightData, gridData, indexData;
    Require(frameData.Write(device, std::span<const SceneFrameConstants>(&cb, 1)), "upload cluster FrameCB");
    Require(lightData.Write(device, std::span<const LocalLightGpu>(lights)), "upload cluster lights");
    Require(gridData.Write(device, std::span<const gamecluster12::Entry>(clusters.grid)), "upload cluster ranges");
    Require(indexData.Write(device, std::span<const uint32_t>(clusters.indices)), "upload cluster indices");

    auto& list = *recording.list;
    list.SetGraphicsRootSignature(root.Get());
    list.SetPipelineState(pipeline.Get());
    list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list.OMSetRenderTargets(1, &recording.rtv, FALSE, nullptr);
    list.SetGraphicsRootConstantBufferView(0, frameData.Address());
    const uint32_t material[4]{};
    list.SetGraphicsRoot32BitConstants(1, 4, material, 0);
    list.SetGraphicsRootShaderResourceView(2, lightData.Address());
    list.SetGraphicsRootShaderResourceView(3, gridData.Address());
    list.SetGraphicsRootShaderResourceView(4, indexData.Address());
    list.DrawInstanced(3, 1, 0, 0);
    Readback pixels(device, recording.color->GetDesc(), 1);
    Transition(list, *recording.color, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    pixels.Copy(list, *recording.color, 0);
    Transition(list, *recording.color, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    Require(frame.Present(owner, 1), "clusters present");
    Require(frame.Drain(owner), "clusters drain");
    std::vector<uint32_t> result;
    for (uint32_t y = 0; y < recording.height; ++y)
        for (uint32_t x = 0; x < recording.width; ++x)
            result.push_back(pixels.Pixel(0, x, y));
    return result;
}

int main(int argc, char** argv)
{
    ComPtr<ID3D12InfoQueue> messages;
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
        Require(device.As(&messages), "D3D12 info queue");
        messages->ClearStorageFilter();
        messages->ClearRetrievalFilter();
        // Сообщения создания устройства уже прошли через исходный фильтр.
        const auto deniedAtDeviceCreation = messages->GetNumMessagesDeniedByStorageFilter();
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
        const bool pageTableOnly = argc > 1 && std::strcmp(argv[1], "--page-table") == 0;
        const bool bankedOnly = argc > 1 && std::strcmp(argv[1], "--banked-parity") == 0;
        const bool scalableOnly = argc > 1 && std::strcmp(argv[1], "--scalable") == 0;
        const bool mixedOnly = argc > 1 && std::strcmp(argv[1], "--adaptive-runtime") == 0;
        const bool cacheOnly = argc > 1 && std::strcmp(argv[1], "--shadow-cache") == 0;
        const bool clusterOnly = argc > 1 && std::strcmp(argv[1], "--clustered-lighting") == 0;
        if (cacheOnly)
        {
            DepthPacketPipeline depth(owner.device, true);
            ComPtr<IDXGIAdapter3> memoryAdapter;
            Require(adapter.As(&memoryAdapter), "cache DXGI budget interface");
            const auto reference = RunShadowCache(window, *factory.Get(), owner, *memoryAdapter.Get(), depth, false);
            Check(reference == RunShadowCache(window, *factory.Get(), owner, *memoryAdapter.Get(), depth, true),
                "cache-off/cache-on depth and sampling parity");
            Check(reference == RunShadowCache(window, *factory.Get(), owner, *memoryAdapter.Get(), depth, true, true),
                "static-copy/dynamic overlay depth and sampling parity");
        }
        else if (clusterOnly)
        {
            const auto reference = RunClusters(window, *factory.Get(), owner, 0);
            Check(reference == RunClusters(window, *factory.Get(), owner, 1), "clustered/full lighting parity");
            Check(reference != RunClusters(window, *factory.Get(), owner, 2), "missing light mutation");
            Check(reference == RunClusters(window, *factory.Get(), owner, 3), "invalid cluster metadata fallback");
            std::puts("PASS: clustered/full lighting parity and mutation checks");
        }
        else if (mixedOnly)
        {
            DepthPacketPipeline depth(owner.device);
            ComPtr<IDXGIAdapter3> memoryAdapter;
            Require(adapter.As(&memoryAdapter), "WARP DXGI memory budget interface");
            const auto serial = RunMixedRuntime(window, *factory.Get(), owner, *memoryAdapter.Get(), "multi", depth);
            Check(serial == RunMixedRuntime(window, *factory.Get(), owner, *memoryAdapter.Get(), "parallel", depth),
                "mixed serial/parallel parity");
            std::printf("PASS: mixed serial/parallel parity, %zu depth and sampling records per mode\n", serial.size());
        }
        else if (scalableOnly)
        {
            RunGatedDescriptorDraws(owner);
            DepthPacketPipeline pass(owner.device);
            const auto legacy = RunScalable(window, *factory.Get(), owner, pass, "legacy", 2);
            Check(legacy == RunScalable(window, *factory.Get(), owner, pass, "multi", 2), "legacy/multi depth parity");
            Check(legacy == RunScalable(window, *factory.Get(), owner, pass, "parallel", 2), "legacy/parallel depth parity");
            const auto serial = RunScalable(window, *factory.Get(), owner, pass, "multi", 7);
            Check(serial == RunScalable(window, *factory.Get(), owner, pass, "parallel", 7), "expanded serial/parallel depth parity");
        }
        else if (bankedOnly)
        {
            for (const uint32_t local : {0u, 8u})
            {
                const auto legacy = Run(window, *factory.Get(), owner, "legacy", 2, local);
                Check(legacy == Run(window, *factory.Get(), owner, "legacy", 2, local, 0, Mutation::None, true),
                    "banked SM5.0 / SM5.1 readback parity");
            }
            std::puts("PASS: banked PS 5.0 / 5.1, CSM, local maps and empty local maps");
        }
        else if (pageTableOnly)
            RunPages(window, *factory.Get(), owner);
        else
        {
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
        }
        DestroyWindow(window);
        CloseHandle(event);
        uint32_t problems = 0;
        uint32_t expectedClearWarnings = 0;
        Check(messages->GetNumMessagesDiscardedByMessageCountLimit() == 0, "InfoQueue discarded messages");
        Check(messages->GetNumMessagesDeniedByStorageFilter() == deniedAtDeviceCreation, "InfoQueue storage-filtered messages");
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
        std::puts(cacheOnly ? "PASS: WARP Debug Layer + GBV, whole-map cache and static copy parity" :
            clusterOnly ? "PASS: WARP Debug Layer + GBV, clustered lighting" : mixedOnly ? "PASS: WARP Debug Layer + GBV, adaptive native shadow runtime" :
            scalableOnly ? "PASS: WARP Debug Layer + GBV, scalable shadow recording" :
            bankedOnly ? "PASS: WARP Debug Layer + GBV, banked sampling parity" :
            pageTableOnly ? "PASS: WARP Debug Layer + GBV, physical page descriptor table" :
            "PASS: WARP Debug Layer + GBV, sun/local sampling, PCF 1-8, GPU mutations, ABI, readback parity, resize, cancel, GR fault drain");
        return 0;
    }
    catch (const std::exception& error)
    {
        if (messages)
            for (uint64_t i = 0; i < messages->GetNumStoredMessages(); ++i)
            {
                SIZE_T size = 0;
                if (FAILED(messages->GetMessage(i, nullptr, &size)) || size < sizeof(D3D12_MESSAGE))
                    continue;
                std::vector<unsigned char> storage(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                if (SUCCEEDED(messages->GetMessage(i, message, &size)) &&
                    message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING &&
                    message->ID != D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE)
                    std::fprintf(stderr, "D3D12 %u: %s\n", message->ID, message->pDescription);
            }
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
