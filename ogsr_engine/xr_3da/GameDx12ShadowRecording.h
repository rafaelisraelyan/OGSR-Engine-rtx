#pragma once

#include "GameDx12NativeFrame.h"
#include "GameDx12ShadowSchedule.h"

namespace game_shadow12
{
// Владелец передаёт валидированное расписание staged-арены и неизменяемый
// снимок. Dispatch обязан дождаться всех принятых работ даже при ошибке.
template<class Draw, class Dispatch>
HRESULT RecordShadowSchedule(GameDx12NativeFrame12::Recording& recording, const ShadowPageResources& pages,
    std::span<const ScheduledShadowView> views, Draw&& draw, Dispatch&& dispatch)
{
    if (views.empty())
        return S_OK;
    if (!recording.frame || !recording.list || !pages.PreparedPlan() || views.size() >= UINT32_MAX - 2)
        return E_INVALIDARG;

    const auto record = [&]() -> HRESULT
    {
        const auto count = static_cast<uint32_t>(views.size());
        std::vector<ShadowPageResources::View> physical(count);
        std::vector<uint32_t> ids;
        ids.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& view = views[i];
            auto& target = physical[i];
            if (!pages.GetView(view.page, view.slice, target) || target.resolution != view.resolution)
                return E_INVALIDARG;
            ids.push_back(view.projection);
        }

        const auto timing = recording.SetShadowTimingViews(ids);
        if (FAILED(timing))
            return timing;
        const bool separate = recording.UsesShadowLists();
        std::vector<ID3D12GraphicsCommandList*> lists(count, recording.list);
        std::vector<HRESULT> results(count, E_PENDING);
        if (separate)
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto hr = recording.OpenShadowList(i, lists[i]);
                if (FAILED(hr))
                    return hr;
            }

        const auto work = [&](uint32_t first, uint32_t last, uint32_t)
        {
            for (uint32_t i = first; i < last; ++i)
            {
                const auto& view = views[i];
                const auto& target = physical[i];
                auto& list = *lists[i];
                recording.StampShadow(list, view.projection, false);
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition = {target.resource, target.slice,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE};
                list.ResourceBarrier(1, &barrier);
                const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(view.resolution),
                    static_cast<float>(view.resolution), 0, 1};
                const D3D12_RECT scissor{0, 0, static_cast<LONG>(view.resolution), static_cast<LONG>(view.resolution)};
                list.RSSetViewports(1, &viewport);
                list.RSSetScissorRects(1, &scissor);
                list.OMSetRenderTargets(0, nullptr, FALSE, &target.dsv);
                list.ClearDepthStencilView(target.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                const auto hr = draw(list, view, target);
                std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                list.ResourceBarrier(1, &barrier);
                recording.StampShadow(list, view.projection, true);
                const auto close = separate ? recording.CloseShadowList(i) : S_OK;
                results[i] = FAILED(hr) ? hr : close;
            }
        };

        if (separate && recording.UsesParallelShadowLists() && count > 1)
        {
            const auto hr = dispatch(count, work);
            if (FAILED(hr))
                return hr;
        }
        else
            work(0, count, 0);

        for (const auto hr : results)
            if (FAILED(hr))
                return hr;
        return separate ? recording.BeginColorList() : S_OK;
    };

    HRESULT hr;
    try { hr = record(); }
    catch (const std::bad_alloc&) { hr = E_OUTOFMEMORY; }
    catch (...) { hr = E_FAIL; }
    if (FAILED(hr))
        recording.frame->Cancel();
    return hr;
}
}
