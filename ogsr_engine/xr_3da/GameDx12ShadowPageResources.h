#pragma once

#include "GameDx12ShadowPagePlan.h"
#include "GameDx12ShadowAddress.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <memory>
#include <new>
#include <stdexcept>

namespace game_shadow12
{
struct ShadowPageShape { uint32_t resolution, slices; };

inline HRESULT DescribeShadowPage(ShadowPageShape shape, D3D12_RESOURCE_DESC& desc) noexcept
{
    desc = {};
    if (!shape.resolution || (shape.resolution & (shape.resolution-1)) ||
        shape.resolution > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        shape.slices < 6 || shape.slices > D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION)
        return E_INVALIDARG;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = shape.resolution;
    desc.Height = shape.resolution;
    desc.DepthOrArraySize = static_cast<UINT16>(shape.slices);
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    return S_OK;
}

inline HRESULT QueryShadowPageSpec(ID3D12Device& device, ShadowPageShape shape, ShadowPageSpec& spec) noexcept
{
    spec = {};
    if (device.GetNodeCount() != 1) return E_NOTIMPL;
    D3D12_RESOURCE_DESC desc{};
    const HRESULT hr = DescribeShadowPage(shape,desc);
    if (FAILED(hr)) return hr;
    const auto info = device.GetResourceAllocationInfo(0,1,&desc);
    if (info.SizeInBytes == UINT64_MAX || !info.SizeInBytes || !info.Alignment)
        return E_FAIL;
    spec = {shape.resolution,shape.slices,info.SizeInBytes};
    return S_OK;
}

// The budget is a process-wide observation, not a reservation. The owner
// must apportion it across slots/other allocations and refresh before staging.
inline HRESULT QueryShadowMemoryBudget(ID3D12Device& device, IDXGIAdapter3& adapter,
    uint64_t reserve, DXGI_QUERY_VIDEO_MEMORY_INFO& info, uint64_t& headroom) noexcept
{
    info = {}; headroom = 0;
    if (device.GetNodeCount() != 1) return E_NOTIMPL;
    DXGI_ADAPTER_DESC desc{};
    HRESULT hr = adapter.GetDesc(&desc);
    if (FAILED(hr)) return hr;
    const auto luid = device.GetAdapterLuid();
    if (desc.AdapterLuid.LowPart != luid.LowPart || desc.AdapterLuid.HighPart != luid.HighPart)
        return E_INVALIDARG;
    hr = adapter.QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info);
    if (FAILED(hr)) { info = {}; return hr; }
    const auto available = info.Budget > info.CurrentUsage ? info.Budget-info.CurrentUsage : 0;
    headroom = available > reserve ? available-reserve : 0;
    return S_OK;
}

// Per-slot physical owner, one device/queue-fence timeline, owner thread only.
// CPU DSV and SRV heaps are private; copy SRVs into the renderer's fenced
// shader-visible heap. All used subresources must end in PIXEL_SHADER_RESOURCE.
// No transitions or queue submission are hidden in this class.
class ShadowPageResources
{
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    struct Page
    {
        ShadowPage description{};
        Ptr<ID3D12Resource> depth;
        Ptr<ID3D12DescriptorHeap> dsv, srv;
    };
    struct Pending
    {
        ShadowPagePlan plan;
        std::vector<Page> pages;
    };
    Ptr<ID3D12Device> device_;
    Ptr<ID3D12Fence> fence_;
    uint32_t slot_ = 0;
    uint32_t dsvStride_ = 0;
    bool unknown_ = false;
    ShadowArena current_{};
    uint64_t bytes_ = 0;
    std::vector<Page> pages_;
    std::unique_ptr<Pending> pending_;

    HRESULT Completion(uint64_t& value) noexcept
    {
        if (!fence_ || unknown_) return E_UNEXPECTED;
        value = fence_->GetCompletedValue();
        if (value == UINT64_MAX)
        {
            Quarantine();
            return DXGI_ERROR_DEVICE_REMOVED;
        }
        return S_OK;
    }

    HRESULT CreatePage(const ShadowPage& page, Page& out)
    {
        D3D12_RESOURCE_DESC desc{};
        HRESULT hr = DescribeShadowPage({page.spec.resolution,page.spec.slices},desc);
        if (FAILED(hr)) return hr;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1.0f;
        Page created;
        created.description = page;
        hr = device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(created.depth.GetAddressOf()));
        if (FAILED(hr)) return hr;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hd.NumDescriptors = page.spec.slices;
        hr = device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(created.dsv.GetAddressOf()));
        if (FAILED(hr)) return hr;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 1;
        hr = device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(created.srv.GetAddressOf()));
        if (FAILED(hr)) return hr;
        auto dsv = created.dsv->GetCPUDescriptorHandleForHeapStart();
        D3D12_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.Format = DXGI_FORMAT_D32_FLOAT;
        dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dv.Texture2DArray.ArraySize = 1;
        for (uint32_t i=0;i<page.spec.slices;++i)
        {
            dv.Texture2DArray.FirstArraySlice = i;
            device_->CreateDepthStencilView(created.depth.Get(),&dv,dsv);
            dsv.ptr += dsvStride_;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = DXGI_FORMAT_R32_FLOAT;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2DArray.MipLevels = 1;
        sv.Texture2DArray.ArraySize = page.spec.slices;
        device_->CreateShaderResourceView(created.depth.Get(),&sv,created.srv->GetCPUDescriptorHandleForHeapStart());
        hr = device_->GetDeviceRemovedReason();
        if (FAILED(hr)) return hr;
        out = std::move(created);
        return S_OK;
    }

    static void Detach(std::vector<Page>& pages) noexcept
    {
        for (auto& p : pages)
        {
            p.depth.Detach(); p.dsv.Detach(); p.srv.Detach();
        }
        pages.clear();
    }

public:
    ShadowPageResources() = default;
    ShadowPageResources(const ShadowPageResources&) = delete;
    ShadowPageResources& operator=(const ShadowPageResources&) = delete;
    ~ShadowPageResources()
    {
        if (FAILED(Release())) Quarantine();
    }

    HRESULT Initialize(ID3D12Device& device, ID3D12Fence& fence, uint32_t slot) noexcept
    {
        if (device_ || unknown_) return E_UNEXPECTED;
        if (device.GetNodeCount() != 1) return E_NOTIMPL;
        Ptr<ID3D12Device> fenceDevice;
        HRESULT hr = fence.GetDevice(IID_PPV_ARGS(fenceDevice.GetAddressOf()));
        if (FAILED(hr)) return hr;
        if (fenceDevice.Get()!=&device) return E_INVALIDARG;
        if (fence.GetCompletedValue()==UINT64_MAX) return DXGI_ERROR_DEVICE_REMOVED;
        hr = device.GetDeviceRemovedReason();
        if (FAILED(hr)) return hr;
        device_ = &device; fence_ = &fence; slot_ = slot;
        dsvStride_ = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        return S_OK;
    }

    // Budget final pages independently from additional allocation headroom:
    // old pages remain alive during staging to permit transactional rollback.
    // A failed Prepare never publishes any new resources or placement.
    HRESULT Prepare(const AdaptivePlan& logical, std::span<const ShadowPageShape> shapes,
        uint64_t pageBudget, uint64_t additionalHeadroom, uint64_t submitFence)
    {
        if (!device_ || unknown_ || pending_) return E_UNEXPECTED;
        uint64_t completed = 0;
        HRESULT hr = Completion(completed);
        if (FAILED(hr)) return hr;
        if (current_.fence > completed) return DXGI_ERROR_WAS_STILL_DRAWING;
        try
        {
            std::vector<ShadowPageSpec> specs;
            specs.reserve(shapes.size());
            for (const auto& shape : shapes)
            {
                ShadowPageSpec spec{};
                hr = QueryShadowPageSpec(*device_.Get(),shape,spec);
                if (FAILED(hr)) return hr;
                specs.push_back(spec);
            }
            auto staged = std::make_unique<Pending>();
            if (!BuildShadowPagePlan(logical,specs,pageBudget,slot_,completed,submitFence,
                current_.fence ? &current_ : nullptr,staged->plan)) return E_INVALIDARG;
            uint64_t newBytes = 0;
            for (const auto& p : staged->plan.next.pages)
            {
                const auto it = std::find_if(pages_.begin(),pages_.end(),
                    [&](const auto& old){return old.description.id==p.id;});
                if (it != pages_.end())
                {
                    if (!SamePageSpec(it->description.spec,p.spec)) return E_UNEXPECTED;
                    continue;
                }
                if (p.spec.bytes > additionalHeadroom-newBytes ||
                    p.spec.bytes > UINT64_MAX-bytes_-newBytes) return E_OUTOFMEMORY;
                newBytes += p.spec.bytes;
            }
            staged->pages.reserve(staged->plan.next.pages.size());
            for (const auto& p : staged->plan.next.pages)
            {
                const auto it = std::find_if(pages_.begin(),pages_.end(),
                    [&](const auto& old){return old.description.id==p.id;});
                if (it!=pages_.end()) staged->pages.push_back(*it);
                else
                {
                    Page created;
                    hr = CreatePage(p,created);
                    if (FAILED(hr)) return hr;
                    staged->pages.push_back(std::move(created));
                }
            }
            pending_ = std::move(staged);
            return S_OK;
        }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (const std::length_error&) { return E_INVALIDARG; }
    }

    // Runtime entry: refresh the matching adapter's actual local-memory
    // budget, subtract caller reserve for the rest of the frame, then stage.
    // pageBudget is the owner's separate per-slot final page allowance.
    HRESULT PrepareBudgeted(const AdaptivePlan& logical, std::span<const ShadowPageShape> shapes,
        uint64_t pageBudget, IDXGIAdapter3& adapter, uint64_t reserve, uint64_t submitFence,
        DXGI_QUERY_VIDEO_MEMORY_INFO& info, uint64_t& headroom)
    {
        info = {}; headroom = 0;
        if (!device_ || unknown_ || pending_) return E_UNEXPECTED;
        uint64_t completed = 0;
        HRESULT hr = Completion(completed);
        if (FAILED(hr)) return hr;
        if (current_.fence > completed) return DXGI_ERROR_WAS_STILL_DRAWING;
        hr = QueryShadowMemoryBudget(*device_.Get(),adapter,reserve,info,headroom);
        if (FAILED(hr)) return hr;
        return Prepare(logical,shapes,pageBudget,headroom,submitFence);
    }

    const ShadowPagePlan* PreparedPlan() const noexcept { return pending_ ? &pending_->plan : nullptr; }
    uint64_t CurrentBytes() const noexcept { return bytes_; }

    // Build the descriptor ordinal / logical projection mapping from actual
    // staged resources, not the nominal adaptive assignment. Only usable
    // before Commit/Cancel on the owner thread.
    bool BuildPreparedAddresses(ShadowAddressPlan& out) const
    {
        if (!pending_ || unknown_) return false;
        const auto& arena=pending_->plan.next;
        return BuildShadowAddresses(std::span<const ShadowPage>(arena.pages),
            std::span<const ShadowPlacement>(arena.placed),out);
    }

    bool GetPreparedPageSrv(uint64_t page,D3D12_CPU_DESCRIPTOR_HANDLE& out) const noexcept
    {
        out={};
        if (!pending_ || unknown_) return false;
        const auto it=std::find_if(pending_->pages.begin(),pending_->pages.end(),
            [&](const auto& p){return p.description.id==page;});
        if (it==pending_->pages.end()) return false;
        out=it->srv->GetCPUDescriptorHandleForHeapStart();
        return true;
    }

    struct View
    {
        ID3D12Resource* resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{}, srv{};
        uint32_t slice = 0, resolution = 0;
    };
    // Borrowed handles; use only while this prepared recording is alive.
    bool GetView(uint64_t page, uint32_t slice, View& out) const noexcept
    {
        out = {};
        if (!pending_ || unknown_) return false;
        const auto it = std::find_if(pending_->pages.begin(),pending_->pages.end(),
            [&](const auto& p){return p.description.id==page;});
        if (it==pending_->pages.end() || slice>=it->description.spec.slices) return false;
        const bool placed = std::any_of(pending_->plan.next.placed.begin(),pending_->plan.next.placed.end(),
            [&](const auto& p){return p.page==page && slice>=p.first && slice-p.first<p.faces;});
        if (!placed) return false;
        out.resource = it->depth.Get();
        out.dsv = it->dsv->GetCPUDescriptorHandleForHeapStart();
        out.dsv.ptr += static_cast<SIZE_T>(slice)*dsvStride_;
        out.srv = it->srv->GetCPUDescriptorHandleForHeapStart();
        out.slice = slice; out.resolution = it->description.spec.resolution;
        return true;
    }

    // Owner must have submitted the lists AND successfully signaled this
    // exact reserved fence on the same timeline. Cannot infer Signal success
    // from GetCompletedValue. Any mismatch after submission is terminal.
    HRESULT CommitSubmitted(uint64_t signaledFence) noexcept
    {
        if (!pending_ || unknown_ || signaledFence!=pending_->plan.next.fence)
        {
            Quarantine(); return E_UNEXPECTED;
        }
        uint64_t completed = 0;
        const HRESULT hr = Completion(completed);
        if (FAILED(hr)) return hr;
        current_ = std::move(pending_->plan.next);
        bytes_ = pending_->plan.bytes;
        pages_ = std::move(pending_->pages);
        pending_.reset();
        return S_OK;
    }

    // Only after all recorded lists referencing this plan have been discarded
    // without execution. On Execute/Signal uncertainty call Quarantine instead.
    HRESULT CancelUnsubmitted() noexcept
    {
        if (unknown_ || !pending_) return E_UNEXPECTED;
        pending_.reset();
        return S_OK;
    }

    HRESULT Release() noexcept
    {
        if (unknown_ || pending_) return E_UNEXPECTED;
        if (fence_)
        {
            uint64_t completed = 0;
            const HRESULT hr = Completion(completed);
            if (FAILED(hr)) return hr;
            if (current_.fence>completed) return DXGI_ERROR_WAS_STILL_DRAWING;
        }
        pages_.clear(); current_={}; bytes_=0;
        fence_.Reset(); device_.Reset();
        return S_OK;
    }

    // Terminal process-lifetime retention, matching native-frame quarantine.
    // Owner must quarantine its external descriptor heap/lists as well.
    void Quarantine() noexcept
    {
        unknown_=true;
        if (pending_) { Detach(pending_->pages); pending_.reset(); }
        Detach(pages_);
        fence_.Detach(); device_.Detach();
    }
};
}
