#include <windows.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include "GameDx12NativeFrame.h"
#include "gr/jobs/JobGroup.h"
#include "gr/jobs/JobSystem.h"

#include <algorithm>
#include <array>
#include <bit>
#include <string>
#include <DirectXMath.h>
#include "GameDx12Lighting.h"
#include "production-shadow.inc"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

struct SamplingFrameConstants
{
    DirectX::XMFLOAT4X4 viewProjection;
    DirectX::XMFLOAT4 cameraPosition;
    std::array<DirectX::XMFLOAT4X4, 4> sunShadowViewProjection;
    DirectX::XMFLOAT4 ambient, sunDirection, sunColor;
    uint32_t lightingFlags[4];
    std::array<uint8_t, 16 * 64> localLights{};
    DirectX::XMFLOAT4 hemiColor;
    DirectX::XMFLOAT4X4 inverseViewProjection;
    DirectX::XMFLOAT4 waterWeather, waterSky;
    std::array<DirectX::XMFLOAT4X4, 8> localShadowViewProjection;
    uint32_t diagnosticLight[4];
    DirectX::XMFLOAT4 diagnosticOrigin;
    uint32_t legacyWaterSsr, sunFilter, sunReceiverBiasMask, sunShadowResolution;
    DirectX::XMFLOAT4 sunCameraForward;
    std::array<DirectX::XMFLOAT4, 4> sunCascadeData;
    std::array<float, 4> sunDepthBias;
    uint32_t sunCasterCounts[4];
    std::array<uint8_t, 96> productionPadding{};
};
constexpr uint32_t kProductionFrameConstantsSize = sizeof(SamplingFrameConstants);
static_assert(kProductionFrameConstantsSize == 2304);

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

void CheckSamplingMutations()
{
    constexpr std::array<uint32_t, 4> selected{0, 1, 2, 3};
    constexpr std::array<float, 4> depth{0.125f, 0.375f, 0.625f, 0.875f};
    const auto valid = [&](const std::array<uint32_t, 4>& indices, const std::array<float, 4>& values)
    { return indices == selected && values == depth; };
    auto swapped = selected; std::swap(swapped[1], swapped[2]);
    Check(!valid(swapped, depth), "mutation slice swap must fail");
    auto wrongOperator = depth; wrongOperator[0] = 0.0f;
    Check(!valid(selected, wrongOperator), "mutation comparison operator must fail");
    auto wrongNext = selected; wrongNext[1] = 0;
    Check(!valid(wrongNext, depth), "mutation next slice must fail");
    auto noBlend = depth; noBlend[1] = depth[0];
    Check(!valid(selected, noBlend), "mutation cascade blending must fail");
    auto wrongLocal = depth; wrongLocal[3] = 0.0625f;
    Check(!valid(selected, wrongLocal), "mutation local layer must fail");
    auto forcedSlice0 = selected; forcedSlice0.fill(0);
    Check(!valid(forcedSlice0, depth), "mutation forced sun slice zero must fail");
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

struct ShadowSamplingPass
{
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> srv;
    ComPtr<ID3D12Resource> constants;
    UINT stride = 0;

    ShadowSamplingPass(ID3D12Device& device, ID3D12Resource* sun, uint32_t sunSlices,
        ID3D12Resource* local, uint32_t localSlices)
    {
        constexpr char vertex[] = R"(
float4 vs(uint id : SV_VertexID) : SV_Position
{
    float2 p[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    return float4(p[id], 0, 1);
}
)";
        const std::string pixel = "#define MAX_LOCAL_LIGHTS " + std::to_string(gamelight12::MaxLocalLights) +
            "\n" + kFrameShaderConstants + kScenePixelShader + R"(
float4 probe(float4 pixel : SV_Position) : SV_Target
{
    uint x = (uint)pixel.x, y = (uint)pixel.y;
    uint count = LightingFlags.w;
    if (x >= count && x < count + LightingFlags.z)
    {
        uint layer = x - count;
        float stored = LocalShadow.Load(int4(1, 1, layer, 0));
        return float4(stored, SampleLocalVisibility(float3(.25, .25, .5), layer),
            SampleLocalVisibility(float3(.25, .25, .99), layer), 1);
    }
    // Каждая строка выбирает середину интервала, середину overlap или границу двух текселей.
    float depth = x + (y == 2 ? .875 : .5);
    float3 position = float3(depth, 0, .5);
    SunSelection selected = SelectSun(position, float3(0, 0, 1), 1);
    if (selected.slice >= 4)
        return float4(0, 0, SunSelectedVisibility(selected, float3(0, 0, 1)), 1);
    float raw = SunStoredDepth(int2(1, 1), selected.slice);
    float visibility = y == 3 ? SunFilterVisibility(float3(.5, .5, .5), float3(0, 0, 1), selected.slice) :
        SunSelectedVisibility(selected, float3(0, 0, 1));
    return float4(raw, (selected.slice + 1) / 255.0, visibility, 1);
}
)";
        const auto vs = Compile(vertex, "vs", "vs_5_0");
        const auto ps = Compile(pixel.c_str(), "probe", "ps_5_0");

        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 2;
        ranges[1] = ranges[0];
        ranges[1].BaseShaderRegister = 6;
        D3D12_ROOT_PARAMETER params[3]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = {1, &ranges[0]};
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable = {1, &ranges[1]};
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 3;
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
        cb.Width = kProductionFrameConstantsSize;
        cb.Height = cb.DepthOrArraySize = cb.MipLevels = cb.SampleDesc.Count = 1;
        cb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Require(device.CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &cb,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constants)), "frame CB allocation");
    }

    void Draw(ID3D12GraphicsCommandList& list, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        uint32_t sunSlices, uint32_t localSlices, uint32_t filter)
    {
        SamplingFrameConstants frame{};
        frame.lightingFlags[3] = sunSlices;
        frame.lightingFlags[2] = localSlices;
        frame.sunCameraForward = {1, 0, 0, 1};
        frame.sunShadowResolution = 8;
        frame.sunFilter = filter;
        for (uint32_t i = 0; i < sunSlices; ++i)
        {
            auto& matrix = frame.sunShadowViewProjection[i];
            // Транспонированная ортопроекция помещает приёмник в левую половину карты.
            matrix._11 = .01f;
            matrix._14 = -.5f;
            matrix._22 = matrix._33 = matrix._44 = 1;
            frame.sunCascadeData[i] = {float(i), float(i+1), float(i)+.75f, .125f};
        }
        void* memory;
        const D3D12_RANGE noRead{};
        Require(constants->Map(0, &noRead, &memory), "map frame constants");
        std::memcpy(memory, &frame, sizeof(frame));
        constants->Unmap(0, nullptr);
        ID3D12DescriptorHeap* heaps[] = {srv.Get()};
        list.SetDescriptorHeaps(1, heaps);
        list.SetPipelineState(pipeline.Get());
        list.SetGraphicsRootSignature(root.Get());
        list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto gpu = srv->GetGPUDescriptorHandleForHeapStart();
        list.SetGraphicsRootConstantBufferView(0, constants->GetGPUVirtualAddress());
        list.SetGraphicsRootDescriptorTable(1, gpu);
        gpu.ptr += stride;
        list.SetGraphicsRootDescriptorTable(2, gpu);
        const uint32_t width = sunSlices + localSlices + 1;
        D3D12_VIEWPORT viewport{0, 1, static_cast<float>(width), 3, 0, 1};
        D3D12_RECT rect{0, 1, static_cast<LONG>(width), 4};
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
    const char* mode, uint32_t sunCount, uint32_t localCount, uint32_t workers = 0)
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
    const auto createArray = [&](uint32_t slices, float clearDepth)
    {
        DepthArray maps{};
        maps.clear = clearDepth;
        maps.step = clearDepth < 0.1f ? 0.0625f : 0.25f;
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
    const auto sun = createArray(sunCount, 0.125f);
    const auto local = createArray(localCount, 0.0625f);
    ShadowSamplingPass sampling(device, sun.resource.Get(), sunCount, local.resource.Get(),
        (std::max)(localCount, 1u));
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
    for (uint32_t index = 0; index < 6; ++index)
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
        sampling.Draw(*recording.list, recording.rtv, sunCount, localCount, 4);
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
        const auto expectedChannel = [](float value)
        {
            const uint32_t channel = static_cast<uint32_t>(value * 255.0f + 0.5f);
            return channel > 255 ? 255u : channel;
        };
        for (uint32_t slice = 0; slice < sunCount; ++slice)
        {
            const uint32_t pixel = sampledColor.Pixel(0, slice, 1);
            const uint32_t expectedRaw = expectedChannel(0.125f + 0.25f * slice);
            const uint32_t expectedIndex = slice + 1;
            Check((pixel & 0xffu) == expectedRaw &&
                ((pixel >> 8) & 0xffu) == expectedIndex &&
                ((pixel >> 16) & 0xffu) == (slice < 2 ? 0u : 255u),
                "sun shadow SRV slice sample mismatch");
        }
        for (uint32_t slice = 0; slice < localCount; ++slice)
        {
            const uint32_t pixel = sampledColor.Pixel(0, sunCount + slice, 1);
            Check((pixel & 0xffu) == expectedChannel(0.0625f + 0.0625f * slice),
                "local shadow SRV slice sample mismatch");
        }
        result.insert(result.end(), {background, foreground});
        for (uint32_t pass = 0; pass < passCount; ++pass)
        {
            const auto view = passView(pass);
            const auto depth = (view.IsSun() ? sunDepth : localDepth).Pixel(view.Slice(), 0, 0);
            const float expectedDepth = view.IsSun() ? 0.125f + 0.25f * view.Slice() :
                0.0625f + 0.0625f * view.Slice();
            Check(depth == std::bit_cast<uint32_t>(expectedDepth), "shadow slice/order mismatch");
            result.push_back(depth);
        }
    }
    Require(frame.Drain(owner), "drain native slot work");
    if (parallel)
    {
        const auto stats = jobs.GetStats();
        Check(stats.tasksSubmitted == 7 && stats.tasksCompleted == 7 && stats.jobFailures == 1 &&
            stats.unobservedFailures == 0, "GR close/drain counters");
        std::printf("GR workers=%u submit=%llu complete=%llu observed_faults=%llu\n", workers,
            static_cast<unsigned long long>(stats.tasksSubmitted), static_cast<unsigned long long>(stats.tasksCompleted),
            static_cast<unsigned long long>(stats.jobFailures));
        jobs.Shutdown();
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
        CheckSamplingMutations();
        const bool sequentialOnly = argc > 1 && std::strcmp(argv[1], "--sequential") == 0;
        for (const uint32_t cascades : {1u, 2u, 3u, 4u})
        {
            const uint32_t locals = cascades == 2 ? 0 : game_shadow12::LocalLayers;
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
        DestroyWindow(window);
        CloseHandle(event);
        uint32_t problems = 0;
        uint32_t expectedClearWarnings = 0;
        for (uint64_t index = 0; index < messages->GetNumStoredMessages(); ++index)
        {
            SIZE_T size = 0;
            messages->GetMessage(index, nullptr, &size);
            std::vector<unsigned char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(messages->GetMessage(index, message, &size)) &&
                message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
            {
                if (message->ID == D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE)
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
        std::puts("PASS: WARP Debug Layer + GBV, sun/local slices, readback parity, resize, cancel, GR fault drain");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
