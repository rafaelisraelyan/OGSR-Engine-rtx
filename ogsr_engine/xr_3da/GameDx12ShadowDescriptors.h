#pragma once

#include "GameDx12FrameStructuredUpload.h"
#include "GameDx12ShadowDescriptorRange.h"
#include "GameDx12ShadowPageResources.h"

#include <array>
#include <utility>

namespace game_shadow12
{
// Владелец сцены удерживает material heap. Таблица занимает только её хвост.
// Один владелец кадра вызывает Stage/Cancel/Commit, GR получает готовый GPU handle.
class ShadowDescriptors
{
public:
    static constexpr uint32_t kSlotCount = 2;

    static bool Supported(ID3D12Device& device) noexcept
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        D3D12_FEATURE_DATA_SHADER_MODEL shader{D3D_SHADER_MODEL_5_1};
        return SUCCEEDED(device.CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) &&
            options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2 &&
            SUCCEEDED(device.CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader, sizeof(shader))) &&
            shader.HighestShaderModel >= D3D_SHADER_MODEL_5_1;
    }

    HRESULT Initialize(ID3D12Device& device, ID3D12Fence& fence,
        ID3D12DescriptorHeap& heap, uint32_t prefix) noexcept
    {
        if (heap_ || unknown_)
            return E_UNEXPECTED;
        const auto desc = heap.GetDesc();
        if (desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
            desc.Flags != D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE || prefix >= desc.NumDescriptors)
            return E_INVALIDARG;
        if (!Supported(device))
            return DXGI_ERROR_UNSUPPORTED;
        stride_ = device.GetDescriptorHandleIncrementSize(desc.Type);
        if (!stride_)
            return E_FAIL;
        device_ = &device;
        fence_ = &fence;
        heap_ = &heap;
        prefix_ = prefix;
        capacity_ = desc.NumDescriptors;
        return S_OK;
    }

    ~ShadowDescriptors()
    {
        if (heap_)
        {
            const auto done = fence_->GetCompletedValue();
            if (pending_ || done == UINT64_MAX || slots_[0].fence > done || slots_[1].fence > done)
                Quarantine();
        }
    }

    ShadowDescriptors() = default;
    ShadowDescriptors(const ShadowDescriptors&) = delete;
    ShadowDescriptors& operator=(const ShadowDescriptors&) = delete;

    struct Binding
    {
        D3D12_GPU_DESCRIPTOR_HANDLE table{};
        uint32_t pageCount = 0;
        D3D12_GPU_VIRTUAL_ADDRESS shapes = 0;
    };

    HRESULT Stage(ID3D12Fence& fence, uint32_t slot, ShadowPageResources& pages,
        ShadowAddressPlan& addresses, Binding& binding) try
    {
        if (!heap_ || unknown_ || pending_ || slot >= slots_.size() || fence_.Get() != &fence ||
            !pages.MatchesOwner(fence, slot))
            return E_INVALIDARG;

        const auto done = fence_->GetCompletedValue();
        if (done == UINT64_MAX)
            return DXGI_ERROR_DEVICE_REMOVED;
        if (slots_[slot].fence > done)
            return DXGI_ERROR_WAS_STILL_DRAWING;

        ShadowAddressPlan staged;
        if (!pages.BuildPreparedAddresses(staged) || staged.pageIds.empty() || staged.pageIds.size() > UINT32_MAX)
            return E_INVALIDARG;
        const auto plan = pages.PreparedPlan();
        if (plan->next.fence <= done)
            return E_INVALIDARG;

        const auto& other = slots_[1 - slot];
        ShadowDescriptorRange range;
        if (!PlaceShadowDescriptors(prefix_, capacity_, slot, static_cast<uint32_t>(staged.pageIds.size()),
                other.fence > done ? other.range.count : 0, range))
            return E_OUTOFMEMORY;

        struct Shape { uint32_t width, height, slices, padding; };
        std::vector<Shape> shapes;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> sources;
        shapes.reserve(staged.pageIds.size());
        sources.reserve(staged.pageIds.size());
        for (const auto id : staged.pageIds)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE source{};
            D3D12_RESOURCE_DESC desc{};
            if (!pages.GetPreparedPageSrv(id, source, &desc) ||
                desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
                !desc.Width || desc.Width > UINT32_MAX || !desc.Height || !desc.DepthOrArraySize)
                return E_UNEXPECTED;
            shapes.push_back({static_cast<uint32_t>(desc.Width), desc.Height, desc.DepthOrArraySize, 0});
            sources.push_back(source);
        }
        auto& shapesUpload = shapes_[slot];
        const auto hr = shapesUpload.Write(*device_.Get(), std::span<const Shape>(shapes));
        if (FAILED(hr))
            return hr;

        auto destination = heap_->GetCPUDescriptorHandleForHeapStart();
        destination.ptr += static_cast<SIZE_T>(range.first) * stride_;
        for (const auto source : sources)
        {
            device_->CopyDescriptorsSimple(1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            destination.ptr += stride_;
        }

        auto table = heap_->GetGPUDescriptorHandleForHeapStart();
        table.ptr += static_cast<UINT64>(range.first) * stride_;
        pending_ = &pages;
        pendingSlot_ = slot;
        staged_ = {range, plan->next.fence};
        addresses = std::move(staged);
        binding = {table, range.count, shapesUpload.Address()};
        return S_OK;
    }

    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (const std::length_error&) { return E_INVALIDARG; }

    bool Expects(const ID3D12Fence& fence, uint64_t value) const noexcept
    {
        return pending_ && fence_.Get() == &fence && staged_.fence == value;
    }

    HRESULT CommitSubmitted(ID3D12Fence& fence, uint64_t value) noexcept
    {
        if (!Expects(fence, value))
        {
            Quarantine();
            return E_UNEXPECTED;
        }
        const auto hr = pending_->CommitSubmitted(value);
        if (FAILED(hr))
        {
            Quarantine();
            return hr;
        }
        slots_[pendingSlot_] = staged_;
        pending_ = nullptr;
        return S_OK;
    }

    HRESULT CancelUnsubmitted() noexcept
    {
        if (!pending_)
            return S_FALSE;
        const auto hr = pending_->CancelUnsubmitted();
        pending_ = nullptr;
        return hr;
    }

    void Quarantine() noexcept
    {
        unknown_ = true;
        if (pending_)
        {
            pending_->Quarantine();
            pending_ = nullptr;
        }
        for (auto& shapes : shapes_)
            shapes.Quarantine();
        heap_.Detach();
    }

private:
    struct Slot
    {
        ShadowDescriptorRange range{};
        uint64_t fence = 0;
    };

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    std::array<Slot, kSlotCount> slots_{};
    std::array<FrameStructuredUpload, kSlotCount> shapes_{};
    Slot staged_{};
    ShadowPageResources* pending_ = nullptr;
    uint32_t pendingSlot_ = 0, prefix_ = 0, capacity_ = 0, stride_ = 0;
    bool unknown_ = false;
};
}
