#pragma once

#include "GameDx12NativeFrame.h"
#include "GameDx12ShadowSchedule.h"

namespace game_shadow12
{
struct ShadowRecordingPass
{
    uint32_t projection = 0;
    ShadowPageResources::View target{};
};

// Draw и Dispatch заимствуют неизменяемый снимок. Dispatch ждёт всех
// принятых работ и на ошибке. Владелец переводит кадр в цвет ровно один раз.
template<class Draw, class Dispatch>
HRESULT RecordShadowPasses(GameDx12NativeFrame12::Recording& recording,
    std::span<const ShadowRecordingPass> passes, Draw&& draw, Dispatch&& dispatch)
{
    if (passes.empty())
        return S_OK;
    if (!recording.frame || !recording.list || passes.size() >= UINT32_MAX - 2)
        return E_INVALIDARG;

    const auto record = [&]() -> HRESULT
    {
        const auto count = static_cast<uint32_t>(passes.size());
        std::vector<uint32_t> ids;
        ids.reserve(count);
        for (const auto& pass : passes)
        {
            if (!pass.target.resource || !pass.target.dsv.ptr || !pass.target.resolution ||
                (!ids.empty() && ids.back() >= pass.projection))
                return E_INVALIDARG;
            ids.push_back(pass.projection);
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
                const auto& pass = passes[i];
                const auto& target = pass.target;
                auto& list = *lists[i];
                recording.StampShadow(list, pass.projection, false);
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition = {target.resource, target.slice,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE};
                list.ResourceBarrier(1, &barrier);
                const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(target.resolution),
                    static_cast<float>(target.resolution), 0, 1};
                const D3D12_RECT scissor{0, 0, static_cast<LONG>(target.resolution),
                    static_cast<LONG>(target.resolution)};
                list.RSSetViewports(1, &viewport);
                list.RSSetScissorRects(1, &scissor);
                list.OMSetRenderTargets(0, nullptr, FALSE, &target.dsv);
                list.ClearDepthStencilView(target.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                const auto hr = draw(list, i);
                std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                list.ResourceBarrier(1, &barrier);
                recording.StampShadow(list, pass.projection, true);
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

template<class Draw, class Dispatch>
HRESULT RecordShadowSchedule(GameDx12NativeFrame12::Recording& recording, const ShadowPageResources& pages,
    std::span<const ScheduledShadowView> views, Draw&& draw, Dispatch&& dispatch)
{
    std::vector<ShadowRecordingPass> passes;
    passes.reserve(views.size());
    for (const auto& view : views)
    {
        ShadowPageResources::View target;
        if (!pages.GetView(view.page, view.slice, target) || target.resolution != view.resolution)
        {
            if (recording.frame)
                recording.frame->Cancel();
            return E_INVALIDARG;
        }
        passes.push_back({view.projection, target});
    }
    return RecordShadowPasses(recording, passes, [&](ID3D12GraphicsCommandList& list, uint32_t index)
    {
        return draw(list, views[index], passes[index].target);
    }, std::forward<Dispatch>(dispatch));
}
}
