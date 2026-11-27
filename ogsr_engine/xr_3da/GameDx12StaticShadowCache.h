#pragma once

#include "GameDx12ShadowPageResources.h"

namespace game_shadow12
{
struct StaticShadowSlice
{
    uint64_t page = 0;
    ShadowPageResources::View view;
};

class StaticShadowScratch
{
public:
    explicit StaticShadowScratch(const ShadowPageResources& pages)
    {
        const auto* plan = pages.PreparedPlan();
        if (!plan)
            return;
        for (const auto& page : plan->next.pages)
            for (uint32_t slice = 0; slice < page.spec.slices; ++slice)
            {
                ShadowPageResources::View view;
                if (pages.GetSpareView(page.id, slice, view))
                    spare_.push_back({page.id, view});
            }
    }

    StaticShadowSlice Take(uint32_t resolution) noexcept
    {
        const auto entry = std::find_if(spare_.begin(), spare_.end(), [resolution](const auto& spare)
        {
            return spare.page != 0 && spare.view.resolution == resolution;
        });
        return entry == spare_.end() ? StaticShadowSlice{} : std::exchange(*entry, StaticShadowSlice{});
    }

private:
    std::vector<StaticShadowSlice> spare_;
};

// Вызывается после перехода слоя назначения в DEPTH_WRITE. Полный срез
// R32_TYPELESS копируется в совместимый срез, без регионального копирования.
template<class StaticDraw>
void CopyStaticShadow(ID3D12GraphicsCommandList& list, const ShadowPageResources::View& source,
    const ShadowPageResources::View& destination, bool refresh, StaticDraw&& draw)
{
    constexpr auto read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    constexpr auto write = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    constexpr auto copySource = D3D12_RESOURCE_STATE_COPY_SOURCE;
    constexpr auto copyDestination = D3D12_RESOURCE_STATE_COPY_DEST;
    const auto transition = [](const auto& view, auto before, auto after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {view.resource, view.slice, before, after};
        return barrier;
    };
    if (refresh)
    {
        const auto toStatic = transition(source, read, write);
        list.ResourceBarrier(1, &toStatic);
        list.OMSetRenderTargets(0, nullptr, FALSE, &source.dsv);
        list.ClearDepthStencilView(source.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        draw();
    }
    list.OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    const D3D12_RESOURCE_BARRIER toCopy[] = {
        transition(source, refresh ? write : read, copySource),
        transition(destination, write, copyDestination)
    };
    list.ResourceBarrier(2, toCopy);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = source.resource;
    dst.pResource = destination.resource;
    src.Type = dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = source.slice;
    dst.SubresourceIndex = destination.slice;
    list.CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    const D3D12_RESOURCE_BARRIER fromCopy[] = {
        transition(source, copySource, read), transition(destination, copyDestination, write)
    };
    list.ResourceBarrier(2, fromCopy);
    list.OMSetRenderTargets(0, nullptr, FALSE, &destination.dsv);
}
}
