#include <windows.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include "GameDx12StaticScene.h"
#include "GameDx12PsoDiagnostics.h"
#include "GameDx12ShadowFilter.h"
#include "GameDx12ShadowFilterHlsl.h"

void Msg(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::puts("");
}

#include "production-pso.inc"

GameDx12StaticScene12::~GameDx12StaticScene12() = default;

namespace
{
void Require(HRESULT hr)
{
    if (FAILED(hr))
        throw hr;
}

ComPtr<ID3D12Resource> Resource(ID3D12Device& device, D3D12_HEAP_TYPE heapType,
    const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clear = nullptr)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heapType;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    ComPtr<ID3D12Resource> resource;
    Require(device.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        state, clear, IID_PPV_ARGS(&resource)));
    return resource;
}

D3D12_RESOURCE_DESC Buffer(UINT64 size)
{
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.SampleDesc.Count = 1;
    return desc;
}

ComPtr<ID3D12DescriptorHeap> Heap(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count,
    D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE)
{
    const D3D12_DESCRIPTOR_HEAP_DESC desc{type, count, flags, 0};
    ComPtr<ID3D12DescriptorHeap> heap;
    Require(device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)));
    return heap;
}

bool DrawReadback(ID3D12Device& device, ID3D12RootSignature& root, ID3D12PipelineState& pso)
{
    constexpr UINT size = 4;
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = texture.Height = size;
    texture.DepthOrArraySize = texture.MipLevels = 1;
    texture.SampleDesc.Count = 1;
    texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE colorClear{};
    colorClear.Format = texture.Format;
    const auto color = Resource(device, D3D12_HEAP_TYPE_DEFAULT, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    device.GetCopyableFootprints(&texture, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    const auto readback = Resource(device, D3D12_HEAP_TYPE_READBACK, Buffer(bytes), D3D12_RESOURCE_STATE_COPY_DEST);
    texture.Format = DXGI_FORMAT_D32_FLOAT;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = texture.Format;
    depthClear.DepthStencil.Depth = 1;
    const auto depth = Resource(device, D3D12_HEAP_TYPE_DEFAULT, texture, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear);
    const auto rtvHeap = Heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    const auto dsvHeap = Heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
    const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    const auto dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    device.CreateRenderTargetView(color.Get(), nullptr, rtv);
    device.CreateDepthStencilView(depth.Get(), nullptr, dsv);
    const auto srv = Heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 12,
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    const auto increment = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (UINT i = 0; i < 12; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        const bool array = i == 1 || i == 5;
        view.Format = array || i == 4 ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.ViewDimension = array ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
        if (array)
        {
            view.Texture2DArray.MipLevels = 1;
            view.Texture2DArray.ArraySize = 1;
        }
        else
            view.Texture2D.MipLevels = 1;
        auto handle = srv->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += i * increment;
        device.CreateShaderResourceView(nullptr, &view, handle);
    }

    const auto upload = Resource(device, D3D12_HEAP_TYPE_UPLOAD, Buffer(4096), D3D12_RESOURCE_STATE_GENERIC_READ);
    const D3D12_RANGE noReads{0, 0};
    void* mapped = nullptr;
    Require(upload->Map(0, &noReads, &mapped));
    std::memset(mapped, 0, 4096);
    SceneFrameConstants constants{};
    DirectX::XMStoreFloat4x4(&constants.viewProjection, DirectX::XMMatrixIdentity());
    constants.localFilterRadius = SceneLocalFilterRadius();
    std::memcpy(mapped, &constants, sizeof(constants));
    // Ровно те же поля и байтовые смещения, что в kStaticLayout.
    struct Vertex { float position[3]; uint32_t nh, tangent, binormal, color; int16_t uv[2]; };
    static_assert(sizeof(Vertex) == 32 && offsetof(Vertex, uv) == kStaticLayout[5].AlignedByteOffset);
    const Vertex vertices[] = {{{-1,-1,.5f}}, {{-1,3,.5f}}, {{3,-1,.5f}}};
    std::memcpy(static_cast<uint8_t*>(mapped) + 2048, vertices, sizeof(vertices));
    upload->Unmap(0, nullptr);

    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    Require(device.CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
    Require(device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    Require(device.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), &pso, IID_PPV_ARGS(&list)));
    Require(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    const float clear[4]{};
    list->ClearRenderTargetView(rtv, clear, 0, nullptr);
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    const D3D12_VIEWPORT viewport{0, 0, size, size, 0, 1};
    const D3D12_RECT scissor{0, 0, size, size};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap* heaps[] = {srv.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(&root);
    const auto table = [&](UINT parameter, UINT offset)
    {
        auto handle = srv->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += offset * increment;
        list->SetGraphicsRootDescriptorTable(parameter, handle);
    };
    const auto address = upload->GetGPUVirtualAddress();
    const uint32_t material[4]{4, 0, 0, 0}, geometry[4]{};
    list->SetGraphicsRootConstantBufferView(0, address);
    table(1, 0);
    list->SetGraphicsRoot32BitConstants(2, 4, material, 0);
    list->SetGraphicsRoot32BitConstants(3, 16, &constants.viewProjection, 0);
    list->SetGraphicsRootShaderResourceView(4, address + 1024);
    list->SetGraphicsRoot32BitConstants(5, 4, geometry, 0);
    table(6, 1);
    table(7, 2);
    list->SetGraphicsRoot32BitConstant(kShadowLayerRoot, 0, 0);
    table(kLocalShadowSrvRoot, 5);
    table(kHemiSrvRoot, 6);
    table(kTerrainSrvRoot, 7);
    list->SetGraphicsRoot32BitConstant(kTerrainScaleRoot, 0, 0);
    for (UINT parameter = kLocalLightBufferRoot; parameter <= kClusterIndicesRoot; ++parameter)
        list->SetGraphicsRootShaderResourceView(parameter, address + 1024);
    const D3D12_VERTEX_BUFFER_VIEW vb{address + 2048, sizeof(vertices), sizeof(Vertex)};
    list->IASetVertexBuffers(0, 1, &vb);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {color.Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = color.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    Require(list->Close());
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    Require(queue->Signal(fence.Get(), 1));
    Require(fence->SetEventOnCompletion(1, nullptr));
    Require(device.GetDeviceRemovedReason());
    const SIZE_T pixelOffset = footprint.Offset + footprint.Footprint.RowPitch + 4;
    const D3D12_RANGE range{pixelOffset, pixelOffset + 4};
    Require(readback->Map(0, &range, &mapped));
    const auto* pixel = static_cast<const uint8_t*>(mapped) + pixelOffset;
    const bool matches = std::abs(int(pixel[0]) - 221) <= 2 && std::abs(int(pixel[1]) - 221) <= 2 &&
        std::abs(int(pixel[2]) - 226) <= 2 && pixel[3] == 255;
    Msg("%s draw/readback rgba=%u,%u,%u,%u", matches ? "PASS" : "FAIL", pixel[0], pixel[1], pixel[2], pixel[3]);
    readback->Unmap(0, &noReads);
    return matches;
}
}

struct GameDx12ProductionPsoProbe
{
    static bool Run(ID3D12Device& device)
    {
        GameDx12StaticScene12 scene;
        std::string reason;
        if (std::getenv("OGSR_DX12_PSO_REFLECT_PROBE"))
        {
            ComPtr<ID3D10Blob> pixel;
            if (!CompileShader(kScenePixelShader, "main", "ps_5_1", pixel.GetAddressOf(), reason, true))
            {
                Msg("FAIL %s", reason.c_str());
                return false;
            }
            gamepso12::DescribeShader({pixel->GetBufferPointer(), pixel->GetBufferSize()}, "production_ps_5_1", Msg);
        }
        Msg("Creating production color and depth PSOs");
        if (!scene.CreatePipeline(&device, reason) || !scene.CreateSunShadowPipeline(&device, reason))
        {
            Msg("FAIL %s", reason.c_str());
            return false;
        }
        if (!scene.m_shadowPageSampling || !scene.m_pipelinePuddle || !scene.m_sunShadowAuxPipeline)
            return false;
        Msg("All production PSOs created; starting draw/readback");
        return DrawReadback(device, *scene.m_rootSignature.Get(), *scene.m_pipelineState.Get());
    }
};

int wmain(int argc, wchar_t** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    _putenv_s("OGSR_DX12_PSO_TRACE", "1");
    if (argc != 4)
        return 2;
    _wputenv_s(L"OGSR_DX12_GAME_SUN_FILTER", argv[2]);
    _wputenv_s(L"OGSR_DX12_GAME_LOCAL_FILTER", argv[3]);
    for (const wchar_t* name : {L"LIGHTING", L"SUN", L"SHADOWS", L"SUN_SHADOWS",
        L"LOCAL_LIGHTS", L"LOCAL_SHADOWS", L"DYNAMIC_SUN_SHADOWS", L"PUDDLES"})
        _wputenv_s((std::wstring(L"OGSR_DX12_GAME_") + name).c_str(), L"1");
    _wputenv_s(L"OGSR_DX12_GAME_DETAIL_SUN_SHADOWS", L"0");

    ComPtr<ID3D12Debug> debug;
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))) ||
        FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
        return 3;
    debug->EnableDebugLayer();
    ComPtr<IDXGIAdapter1> adapter;
    if (std::wcscmp(argv[1], L"warp") == 0)
    {
        if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))))
            return 3;
    }
    else
    {
        for (UINT i = 0; ; ++i)
        {
            if (FAILED(factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf())))
                return 3;
            DXGI_ADAPTER_DESC1 description{};
            if (SUCCEEDED(adapter->GetDesc1(&description)) && std::wcsstr(description.Description, argv[1]))
                break;
        }
    }
    DXGI_ADAPTER_DESC1 description{};
    adapter->GetDesc1(&description);
    std::printf("adapter=%ls vendor=0x%04x device=0x%04x sun=%ls local=%ls\n",
        description.Description, description.VendorId, description.DeviceId, argv[2], argv[3]);
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12InfoQueue> queue;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        FAILED(device.As(&queue)))
        return 3;
    bool created = false;
    try { created = GameDx12ProductionPsoProbe::Run(*device.Get()); }
    catch (HRESULT hr) { Msg("FAIL draw/readback HRESULT=0x%08X", static_cast<unsigned>(hr)); }
    bool errors = false;
    for (UINT64 i = 0; i < queue->GetNumStoredMessages(); ++i)
    {
        SIZE_T bytes = 0;
        if (FAILED(queue->GetMessage(i, nullptr, &bytes)))
            return 3;
        std::vector<uint8_t> storage(bytes);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (FAILED(queue->GetMessage(i, message, &bytes)))
            return 3;
        Msg("InfoQueue severity=%u id=%u %s", message->Severity, message->ID, message->pDescription);
        errors |= message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING;
    }
    Msg("%s production PSOs, SM5.1 page table, Debug Layer", created && !errors ? "PASS" : "FAIL");
    return created && !errors ? 0 : 1;
}
