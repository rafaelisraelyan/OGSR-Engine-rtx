#include "GameDx12ShadowPageResources.h"
#include <dxgi1_6.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d12sdklayers.h>

using namespace game_shadow12;
using Microsoft::WRL::ComPtr;
void Check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::abort(); }
}
void Require(HRESULT hr, const char* what)
{
    if (FAILED(hr)) { std::fprintf(stderr,"0x%08X: %s\n",static_cast<unsigned>(hr),what); std::abort(); }
}
void Transition(ID3D12GraphicsCommandList& list, ID3D12Resource& resource, uint32_t slice,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={&resource,slice,before,after};
    list.ResourceBarrier(1,&b);
}
int main()
{
    ComPtr<ID3D12Debug> debug;
    Require(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf())),"debug layer");
    debug->EnableDebugLayer();
    ComPtr<ID3D12Debug1> debug1;
    Require(debug.As(&debug1),"GBV interface");
    debug1->SetEnableGPUBasedValidation(TRUE);
    ComPtr<IDXGIFactory4> factory;
    Require(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())),"factory");
    ComPtr<IDXGIAdapter3> adapter;
    Require(factory->EnumWarpAdapter(IID_PPV_ARGS(adapter.GetAddressOf())),"WARP adapter");
    ComPtr<ID3D12Device> device;
    Require(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(device.GetAddressOf())),"WARP device");
    ComPtr<ID3D12InfoQueue> messages;
    Require(device.As(&messages),"InfoQueue");
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    Require(device->CreateCommandQueue(&qd,IID_PPV_ARGS(queue.GetAddressOf())),"queue");
    ComPtr<ID3D12Fence> fence,gate;
    Require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(fence.GetAddressOf())),"fence");
    Require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(gate.GetAddressOf())),"gate");
    const HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    Check(event!=nullptr,"event");
    const auto wait=[&](uint64_t value) {
        Require(fence->SetEventOnCompletion(value,event),"fence event");
        Check(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"fence timeout");
        Check(fence->GetCompletedValue()!=UINT64_MAX && fence->GetCompletedValue()>=value,"completion");
        Require(device->GetDeviceRemovedReason(),"device status");
    };
    const std::array<ShadowPageShape,2> shapes{{{128,8},{256,8}}};
    ShadowPageSpec small{},large{},invalid{};
    Require(QueryShadowPageSpec(*device.Get(),shapes[0],small),"small allocation info");
    Require(QueryShadowPageSpec(*device.Get(),shapes[1],large),"large allocation info");
    Check(FAILED(QueryShadowPageSpec(*device.Get(),{127,8},invalid)) && !invalid.bytes,"invalid shape rejected");
    Check(small.bytes>0 && large.bytes>0,"allocation sizes");
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    uint64_t headroom=0;
    const auto budgetResult=QueryShadowMemoryBudget(*device.Get(),*adapter.Get(),0,memory,headroom);
    std::printf("WARP allocation bytes=%llu/%llu DXGI budget hr=0x%08X budget=%llu usage=%llu headroom=%llu\n",
        static_cast<unsigned long long>(small.bytes),static_cast<unsigned long long>(large.bytes),
        static_cast<unsigned>(budgetResult),static_cast<unsigned long long>(memory.Budget),
        static_cast<unsigned long long>(memory.CurrentUsage),static_cast<unsigned long long>(headroom));
    if (SUCCEEDED(budgetResult))
        Check(headroom==(memory.Budget>memory.CurrentUsage?memory.Budget-memory.CurrentUsage:0),"headroom");
    // Explicit fixture budget, not a claim of real WARP local-memory headroom.
    AdaptivePlan logical{1,1,1,1,UINT64_MAX,0,{{1,6,128,128,1},{2,1,128,128,1}}};
    ShadowPageResources owner;
    Require(owner.Initialize(*device.Get(),*fence.Get(),0),"initialize");
    Check(owner.Prepare(logical,shapes,small.bytes,0,1)==E_OUTOFMEMORY && !owner.PreparedPlan(),
        "transient budget rejects new allocation without publication");
    Require(owner.Prepare(logical,shapes,small.bytes,small.bytes,1),"prepare physical page");
    const auto* plan=owner.PreparedPlan();
    Check(plan && plan->next.pages.size()==1 && plan->next.placed.size()==2 && plan->bytes==small.bytes,
        "physical plan and allocation accounting");
    const auto point=plan->next.placed[0];
    Check(point.faces==6,"point group atomic");
    ShadowAddressPlan addresses;
    Check(owner.BuildPreparedAddresses(addresses) && addresses.pageIds.size()==1 &&
        addresses.groups.size()==2 && addresses.views.size()==7,"physical page addresses");
    Check(addresses.groups[0].light==1 && addresses.groups[0].faces==6,"stable point group identity");
    for (uint32_t face=0;face<6;++face)
    {
        const auto& v=addresses.views[addresses.groups[0].firstProjection+face];
        Check(addresses.pageIds[v.page]==point.page && v.slice==point.first+face &&
            v.resolution==128 && v.group==(6u|(face<<8)),"physical point face mapping");
    }
    D3D12_CPU_DESCRIPTOR_HANDLE pageSrv{};
    Check(owner.GetPreparedPageSrv(point.page,pageSrv) && pageSrv.ptr!=0,"page descriptor lookup");
    Check(!owner.GetPreparedPageSrv(0,pageSrv) && pageSrv.ptr==0,"invalid page descriptor rejected");
    ShadowPageResources::View a,b,bad;
    Require(owner.GetView(point.page,point.first,a)?S_OK:E_FAIL,"point face 0 view");
    Require(owner.GetView(point.page,point.first+5,b)?S_OK:E_FAIL,"point face 5 view");
    Check(a.resource==b.resource && a.dsv.ptr!=b.dsv.ptr && a.srv.ptr==b.srv.ptr,"per-slice DSV and page SRV");
    Check(!owner.GetView(point.page,8,bad) && !owner.GetView(point.page,7,bad),"unallocated view rejected");
    const auto resourceDesc=a.resource->GetDesc();
    Check(resourceDesc.Width==128 && resourceDesc.DepthOrArraySize==8 && resourceDesc.Format==DXGI_FORMAT_R32_TYPELESS,
        "resource descriptor");
    // Copy the CPU SRV into a real GPU-visible heap. This fixture checks
    // descriptor creation/copy; it does not execute a sampling shader.
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors=1;
    hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> shaderHeap;
    Require(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(shaderHeap.GetAddressOf())),"shader heap");
    device->CopyDescriptorsSimple(1,shaderHeap->GetCPUDescriptorHandleForHeapStart(),a.srv,hd.Type);
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    Require(device->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(allocator.GetAddressOf())),"allocator");
    Require(device->CreateCommandList(0,qd.Type,allocator.Get(),nullptr,IID_PPV_ARGS(list.GetAddressOf())),"list");
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,8> footprints{};
    uint64_t readbackSize=0;
    device->GetCopyableFootprints(&resourceDesc,0,8,0,footprints.data(),nullptr,nullptr,&readbackSize);
    Check(readbackSize>0 && readbackSize!=UINT64_MAX,"copy footprints");
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width=readbackSize; rd.Height=1; rd.DepthOrArraySize=1; rd.MipLevels=1;
    rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type=D3D12_HEAP_TYPE_READBACK; hp.CreationNodeMask=hp.VisibleNodeMask=1;
    ComPtr<ID3D12Resource> readback;
    Require(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,IID_PPV_ARGS(readback.GetAddressOf())),"readback");
    for (const auto& v : {a,b})
    {
        Transition(*list.Get(),*v.resource,v.slice,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        list->ClearDepthStencilView(v.dsv,D3D12_CLEAR_FLAG_DEPTH,v.slice==a.slice?.25f:.75f,0,0,nullptr);
        Transition(*list.Get(),*v.resource,v.slice,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};
        src.pResource=v.resource; src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex=v.slice;
        dst.pResource=readback.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint=footprints[v.slice];
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        Transition(*list.Get(),*v.resource,v.slice,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    Require(list->Close(),"close");
    Require(queue->Wait(gate.Get(),1),"hold GPU work");
    ID3D12CommandList* lists[]={list.Get()};
    queue->ExecuteCommandLists(1,lists);
    Require(queue->Signal(fence.Get(),1),"signal submission");
    Require(owner.CommitSubmitted(1),"publish after signal");
    logical.frame=2;
    Check(owner.CurrentBytes()==small.bytes &&
        owner.Prepare(logical,shapes,small.bytes,0,2)==DXGI_ERROR_WAS_STILL_DRAWING &&
        owner.Release()==DXGI_ERROR_WAS_STILL_DRAWING,"in-flight slot protected");
    Require(gate->Signal(1),"release GPU work");
    wait(1);
    const D3D12_RANGE readRange{0,static_cast<SIZE_T>(readbackSize)};
    void* data=nullptr;
    Require(readback->Map(0,&readRange,&data),"map");
    for (const auto& v : {a,b})
    {
        const auto& fp=footprints[v.slice];
        for (uint32_t y=0;y<v.resolution;++y)
            for (uint32_t x=0;x<v.resolution;++x)
            {
                float depth=0;
                std::memcpy(&depth,static_cast<const uint8_t*>(data)+fp.Offset+y*fp.Footprint.RowPitch+x*4,4);
                Check(depth==(v.slice==a.slice?.25f:.75f),"independent layer readback");
            }
    }
    const D3D12_RANGE noWrite{};
    readback->Unmap(0,&noWrite);
    Require(owner.Prepare(logical,shapes,small.bytes,0,2),"reuse with no new bytes");
    ShadowPageResources::View reused;
    Check(owner.GetView(point.page,point.first,reused) && reused.resource==a.resource &&
        owner.PreparedPlan()->create.empty(),"physical resource reuse");
    Require(owner.CancelUnsubmitted(),"discard unsubmitted plan");
    if (SUCCEEDED(budgetResult))
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO latest{};
        uint64_t available=0;
        Require(owner.PrepareBudgeted(logical,shapes,small.bytes,*adapter.Get(),UINT64_MAX,2,latest,available),
            "live DXGI budget with zero additional headroom and retained resources");
        Check(available==0 && owner.PreparedPlan()->create.empty(),"real budget reuse");
        Require(owner.CancelUnsubmitted(),"cancel live budget plan");
    }
    logical.epoch=2;
    logical.lights[0].resolution=256; logical.lights[1].resolution=256;
    Check(owner.Prepare(logical,shapes,large.bytes,0,2)==E_OUTOFMEMORY && owner.CurrentBytes()==small.bytes,
        "epoch replacement peak denied transactionally");
    Require(owner.Prepare(logical,shapes,large.bytes,large.bytes,2),"stage epoch replacement");
    Check(owner.PreparedPlan()->retire.size()==1 && owner.PreparedPlan()->create.size()==1,"replacement delta");
    Require(owner.CancelUnsubmitted(),"rollback replacement");
    Require(owner.Release(),"release completed slot");
    // A differing optimized-clear value is intentional in the layer test.
    const auto count=messages->GetNumStoredMessages();
    Check(messages->GetNumMessagesDeniedByStorageFilter()==0 &&
        messages->GetNumMessagesDiscardedByMessageCountLimit()==0 &&
        messages->GetNumStoredMessagesAllowedByRetrievalFilter()==count,"complete debug messages");
    for (uint64_t i=0;i<count;++i)
    {
        SIZE_T size=0;
        Require(messages->GetMessage(i,nullptr,&size),"message size");
        std::vector<uint8_t> storage(size);
        auto* msg=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Require(messages->GetMessage(i,msg,&size),"debug message");
        if (msg->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION || msg->Severity==D3D12_MESSAGE_SEVERITY_ERROR ||
            (msg->Severity==D3D12_MESSAGE_SEVERITY_WARNING &&
                msg->ID!=D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE))
        { std::fprintf(stderr,"%s\n",msg->pDescription); Check(false,"unexpected debug message"); }
    }
    CloseHandle(event);
    std::puts("PASS: WARP physical shadow page allocation, DSV readback, SRV copy, fenced reuse, transactional replacement");
}
