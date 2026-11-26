#pragma once

#include "GameDx12AdaptiveShadowBudget.h"
#include "GameDx12Lighting.h"
#include "GameDx12NativeFrame.h"

#include <cstdlib>
#include <cstring>
#include <optional>

namespace game_shadow12
{
// Живёт вместе со сценой. Арены публикуются только владельцем NativeFrame.
class AdaptiveShadowRuntime
{
public:
    static bool Enabled() noexcept
    {
        const char* value = std::getenv("OGSR_DX12_GAME_ADAPTIVE_SHADOWS");
        return value && std::strcmp(value, "1") == 0;
    }

    static AdaptiveCandidate Candidate(const gamelight12::LocalLight& light,
        const float camera[3], double pixelsPerRadian) noexcept
    {
        const double distance = std::hypot(double(light.position.x) - camera[0],
            double(light.position.y) - camera[1], double(light.position.z) - camera[2]);
        const double radius = light.range;
        const double coverage = radius > 0.0 ? radius / (std::max)(radius, distance) : 0.0;
        const double score = (std::max)(0.0, light.selectionScore) * (0.5 + 0.5 * coverage);
        return {light.id, score, (std::max)(1.0, coverage * pixelsPerRadian),
            light.type == gamelight12::LocalType::Spot ? 1u : 6u};
    }

    HRESULT Prepare(const GameDx12NativeFrame12::Owner& owner, IDXGIAdapter3& adapter,
        GameDx12NativeFrame12::Recording& recording, ShadowDescriptors& descriptors,
        std::span<const AdaptiveCandidate> candidates, uint64_t session, uint64_t epoch, uint64_t frame,
        uint32_t sunViews, ShadowAddressPlan& addresses, ShadowDescriptors::Binding& binding,
        uint64_t slotAllowance = UINT64_MAX)
    {
        if (!initialized_)
        {
            for (size_t i = 0; i < shapes_.size(); ++i)
            {
                const auto hr = QueryShadowPageSpec(owner.device, shapes_[i], specs_[i]);
                if (FAILED(hr))
                    return hr;
                tiers_[i] = {specs_[i].resolution, (specs_[i].bytes - 1) / specs_[i].slices + 1};
            }
            for (uint32_t i = 0; i < arenas_.size(); ++i)
            {
                const auto hr = arenas_[i].Initialize(owner.device, owner.fence, i);
                if (FAILED(hr))
                {
                    for (auto& arena : arenas_)
                        arena.Release();
                    return hr;
                }
            }
            initialized_ = true;
        }

        auto& arena = arenas_[recording.slot];
        const auto& other = arenas_[1 - recording.slot];
        if (other.PreparedPlan() || owner.fenceValue >= UINT64_MAX - 1)
            return E_UNEXPECTED;
        const auto submit = owner.fenceValue + 1;
        const auto available = descriptors.Available(recording.slot, owner.fence.GetCompletedValue());
        const auto& current = arena.CurrentArena();
        const auto& last = other.CurrentArena().frame > current.frame ? other.CurrentArena() : current;
        AdaptivePlan prior{last.session, last.epoch, last.frame, last.policyRevision, 0, 0, {}};
        for (const auto& group : last.placed)
            prior.lights.push_back({group.light, group.faces, group.resolution, group.resolution, 0});
        const auto observe = [&]() -> std::optional<AdaptiveSlotBudget>
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            uint64_t headroom = 0;
            if (FAILED(QueryShadowMemoryBudget(owner.device, adapter, 0, info, headroom)))
                return {};
            auto budget = PlanAdaptiveSlotBudget(info.Budget, info.CurrentUsage, arena.CurrentBytes(), other.CurrentBytes(), 0);
            budget.finalBytes = (std::min)(budget.finalBytes, slotAllowance);
            budget.newBytes = (std::min)(budget.newBytes, budget.finalBytes);
            return budget;
        };
        auto budget = observe();
        if (!budget)
        {
            const auto status = owner.device.GetDeviceRemovedReason();
            return FAILED(status) ? status : S_FALSE;
        }
        AdaptivePlan logical;
        const auto plan = [&]()
        {
            return BuildAdaptivePlan(tiers_, candidates, budget->finalBytes, session, epoch, frame, 1, &prior, logical);
        };
        if (!plan())
            return E_INVALIDARG;

        ShadowPagePlan preview;
        if (!BuildShadowPagePlan(logical, specs_, budget->finalBytes, recording.slot,
                owner.fence.GetCompletedValue(), submit, current.fence ? &current : nullptr, preview, available))
            return E_INVALIDARG;
        uint64_t views = sunViews;
        for (const auto& group : preview.next.placed)
            views += group.faces;
        if (views > (UINT32_MAX - 4) / 2)
            return E_OUTOFMEMORY;
        const auto storage = recording.ReserveShadowStorage(static_cast<uint32_t>(views));
        if (FAILED(storage))
            return storage;
        // Запрос после роста реальных списков и timestamp storage. Драйвер
        // может отразить их стоимость в CurrentUsage с задержкой.
        budget = observe();
        if (!budget)
        {
            const auto status = owner.device.GetDeviceRemovedReason();
            return FAILED(status) ? status : S_FALSE;
        }
        if (!plan())
            return E_INVALIDARG;
        auto hr = arena.Prepare(logical, shapes_, budget->finalBytes, budget->newBytes, submit, available);
        if (hr == E_OUTOFMEMORY)
        {
            // Последовательно уменьшаем качество по существующим ступеням.
            // Последняя попытка сохраняет только совместимые размещения:
            // временный пик нельзя оплачивать кредитом за будущий retire.
            for (size_t tier = tiers_.size() - 1; tier > 0 && hr == E_OUTOFMEMORY; --tier)
            {
                const auto ceiling = tiers_[tier - 1];
                for (auto& assignment : logical.lights)
                    if (assignment.resolution > ceiling.resolution)
                    {
                        assignment.resolution = ceiling.resolution;
                        assignment.bytes = ceiling.bytesPerFace * assignment.faces;
                    }
                hr = arena.Prepare(logical, shapes_, budget->finalBytes, budget->newBytes, submit, available);
            }
            if (hr == E_OUTOFMEMORY)
            {
                std::unordered_map<uint64_t, const ShadowPlacement*> retained;
                if (current.session == session && current.epoch == epoch)
                    for (const auto& group : current.placed)
                        retained.emplace(group.light, &group);
                for (auto& assignment : logical.lights)
                {
                    const auto placement = retained.find(assignment.id);
                    if (assignment.resolution && placement != retained.end() && placement->second->faces == assignment.faces)
                    {
                        const auto physical = std::find_if(tiers_.begin(), tiers_.end(), [&](const auto& tier)
                        {
                            return tier.resolution == placement->second->resolution;
                        });
                        if (physical != tiers_.end())
                        {
                            assignment.resolution = physical->resolution;
                            assignment.bytes = physical->bytesPerFace * assignment.faces;
                            continue;
                        }
                    }
                    assignment.resolution = 0;
                    assignment.bytes = 0;
                }
                hr = arena.Prepare(logical, shapes_, budget->finalBytes, 0, submit, available);
            }
        }
        if (FAILED(hr))
            return hr;
        hr = recording.StageShadowPages(descriptors, arena, addresses, binding);
        if (FAILED(hr))
            arena.CancelUnsubmitted();
        return hr;
    }

    const ShadowPageResources& Pages(uint32_t slot) const noexcept { return arenas_[slot]; }
    void Quarantine() noexcept
    {
        for (auto& arena : arenas_)
            arena.Quarantine();
    }

private:
    std::array<ShadowPageResources, GameDx12NativeFrame12::kFrameCount> arenas_;
    const std::array<ShadowPageShape, 4> shapes_{{{128, 6}, {256, 6}, {512, 6}, {1024, 6}}};
    std::array<ShadowPageSpec, 4> specs_{};
    std::array<AdaptiveTier, 4> tiers_{};
    bool initialized_ = false;
};
}
