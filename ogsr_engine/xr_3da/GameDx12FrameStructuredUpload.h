#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

namespace game_shadow12
{
// The caller owns the frame-slot fence. Write/Reset may only run after
// that slot is idle (and before recording); Quarantine retains unknown work.
// Root SRVs have no descriptor bounds: shader indices must use actual counts.
class FrameStructuredUpload
{
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    uint64_t capacity_ = 0;
public:
    FrameStructuredUpload() = default;
    FrameStructuredUpload(const FrameStructuredUpload&) = delete;
    FrameStructuredUpload& operator=(const FrameStructuredUpload&) = delete;

    template<class T>
    HRESULT Write(ID3D12Device& device, std::span<const T> records) noexcept
    {
        static_assert(std::is_trivially_copyable_v<T> && sizeof(T)%4==0);
        constexpr auto maxSize = (std::numeric_limits<size_t>::max)();
        if (records.size() > UINT32_MAX || records.size() > maxSize/sizeof(T))
            return E_INVALIDARG;
        const uint64_t bytes = records.empty() ? sizeof(T) : records.size_bytes();
        auto target = resource_;
        uint64_t capacity = capacity_;
        if (bytes > capacity)
        {
            // Grow geometrically where representable; actual API allocation
            // failure is returned to the owner, not silently truncated.
            capacity = bytes;
            if (capacity_ <= maxSize/2 && capacity_*2 > capacity)
                capacity = capacity_*2;
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            heap.CreationNodeMask = heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = capacity;
            desc.Height = 1;
            desc.DepthOrArraySize = desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            Microsoft::WRL::ComPtr<ID3D12Resource> replacement;
            const HRESULT hr = device.CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(replacement.GetAddressOf()));
            if (FAILED(hr)) return hr;
            target = std::move(replacement);
        }
        if (!target) return E_UNEXPECTED;
        void* mapped = nullptr;
        const D3D12_RANGE noRead{};
        const HRESULT hr = target->Map(0,&noRead,&mapped);
        if (FAILED(hr)) return hr;
        if (records.empty()) std::memset(mapped,0,static_cast<size_t>(bytes));
        else std::memcpy(mapped,records.data(),static_cast<size_t>(bytes));
        const D3D12_RANGE written{0,static_cast<SIZE_T>(bytes)};
        target->Unmap(0,&written);
        resource_ = std::move(target);
        capacity_ = capacity;
        return S_OK;
    }
    D3D12_GPU_VIRTUAL_ADDRESS Address() const noexcept
    {
        return resource_ ? resource_->GetGPUVirtualAddress() : 0;
    }
    uint64_t Capacity() const noexcept { return capacity_; }
    void Reset() noexcept { resource_.Reset(); capacity_=0; }
    void Quarantine() noexcept { resource_.Detach(); capacity_=0; }
};
}
