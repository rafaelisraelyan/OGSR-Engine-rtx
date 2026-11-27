#pragma once

#include "GameDx12AdaptiveShadowBudget.h"
#include "GameDx12Lighting.h"
#include "GameDx12NativeFrame.h"

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace game_shadow12
{
struct AdaptiveShadowTelemetry
{
    const char* fallback = "none";
    uint64_t dxgiBudget = 0, dxgiUsage = 0, retained = 0, newBytes = 0, peak = 0;
    uint32_t descriptorCapacity = 0, descriptors = 0;
    std::array<uint32_t, 4> pagesByTier{};
    double lightPlanMs = 0, pagePlanMs = 0, pagePrepareMs = 0, descriptorStageMs = 0;
    bool observed = false;
};

// Живёт вместе со сценой. Арены публикуются только владельцем NativeFrame.
class AdaptiveShadowRuntime
{
public:
    static bool Enabled() noexcept
    {
        const char* value = std::getenv("OGSR_DX12_GAME_ADAPTIVE_SHADOWS");
        return value && std::strcmp(value, "1") == 0;
    }

    static uint64_t BudgetBytes() noexcept
    {
        static const auto value = ReadBytes("OGSR_DX12_GAME_ADAPTIVE_SHADOW_BUDGET_BYTES", UINT64_MAX);
        return value;
    }

    static uint64_t ReserveBytes() noexcept
    {
        static const auto value = ReadBytes("OGSR_DX12_GAME_ADAPTIVE_SHADOW_RESERVE_BYTES", 0);
        return value;
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
        uint64_t slotAllowance = UINT64_MAX, AdaptiveShadowTelemetry* telemetry = nullptr,
        uint32_t shadowListLimit = UINT32_MAX)
    {
        using Clock = std::chrono::steady_clock;
        const auto now = [&] { return telemetry ? Clock::now() : Clock::time_point{}; };
        const auto elapsed = [&](auto start) { return std::chrono::duration<double, std::milli>(now() - start).count(); };
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
        if (telemetry) telemetry->descriptorCapacity = available;
        const auto& current = arena.CurrentArena();
        const auto& last = other.CurrentArena().frame > current.frame ? other.CurrentArena() : current;
        AdaptivePlan prior{last.session, last.epoch, last.frame, last.policyRevision, 0, 0, {}};
        for (const auto& group : last.placed)
            prior.lights.push_back({group.light, group.faces, group.resolution, group.resolution, 0});
        const auto observe = [&]() -> std::optional<AdaptiveSlotBudget>
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            uint64_t headroom = 0;
            const auto status = QueryShadowMemoryBudget(owner.device, adapter, 0, info, headroom);
            if (FAILED(status))
            {
                if (telemetry) telemetry->fallback = "dxgi_budget_unavailable";
                return {};
            }
            if (telemetry)
            {
                telemetry->observed = true;
                telemetry->dxgiBudget = info.Budget;
                telemetry->dxgiUsage = info.CurrentUsage;
            }
            auto budget = PlanAdaptiveSlotBudget(info.Budget, info.CurrentUsage, arena.CurrentBytes(), other.CurrentBytes(),
                ReserveBytes(), BudgetBytes());
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
            const auto start = now();
            const auto result = BuildAdaptivePlan(tiers_, candidates, budget->finalBytes, session, epoch, frame, 1, &prior, logical);
            if (telemetry) telemetry->lightPlanMs += elapsed(start);
            return result;
        };
        if (!plan())
            return E_INVALIDARG;

        const auto pagePlanStart = now();
        ShadowPagePlan preview;
        if (!BuildShadowPagePlan(logical, specs_, budget->finalBytes, recording.slot,
                owner.fence.GetCompletedValue(), submit, current.fence ? &current : nullptr, preview, available))
            return E_INVALIDARG;
        if (telemetry) telemetry->pagePlanMs = elapsed(pagePlanStart);
        uint64_t views = sunViews;
        for (const auto& group : preview.next.placed)
            views += group.faces;
        if (views > (UINT32_MAX - 4) / 2)
            return E_OUTOFMEMORY;
        const auto storage = recording.ReserveShadowStorage(static_cast<uint32_t>(views), shadowListLimit);
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
        const auto pagePrepareStart = now();
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
        if (telemetry)
        {
            telemetry->pagePrepareMs = elapsed(pagePrepareStart);
            const auto& staged = *arena.PreparedPlan();
            for (const auto& page : staged.next.pages)
            {
                const auto tier = std::find_if(shapes_.begin(), shapes_.end(),
                    [&](const auto& shape) { return shape.resolution == page.spec.resolution; });
                if (tier != shapes_.end())
                    ++telemetry->pagesByTier[static_cast<size_t>(tier - shapes_.begin())];
                if (std::binary_search(staged.create.begin(), staged.create.end(), page.id))
                    telemetry->newBytes += page.spec.bytes;
            }
            telemetry->retained = staged.bytes - telemetry->newBytes;
            const auto sum = [](uint64_t a, uint64_t b) { return a > UINT64_MAX - b ? UINT64_MAX : a + b; };
            telemetry->peak = sum(sum(arena.CurrentBytes(), other.CurrentBytes()), telemetry->newBytes);
        }
        const auto descriptorStart = now();
        hr = recording.StageShadowPages(descriptors, arena, addresses, binding);
        if (telemetry) telemetry->descriptorStageMs = elapsed(descriptorStart);
        if (telemetry && SUCCEEDED(hr)) telemetry->descriptors = binding.pageCount;
        if (FAILED(hr))
            arena.CancelUnsubmitted();
        return hr;
    }

    const ShadowPageResources& Pages(uint32_t slot) const noexcept { return arenas_[slot]; }
    ShadowContentCache& Content(uint32_t slot) noexcept { return content_[slot]; }
    ShadowContentCache& StaticContent(uint32_t slot) noexcept { return staticContent_[slot]; }
    void Quarantine() noexcept
    {
        for (auto& cache : content_)
            cache.Quarantine();
        for (auto& cache : staticContent_)
            cache.Quarantine();
        for (auto& arena : arenas_)
            arena.Quarantine();
    }

private:
    static uint64_t ReadBytes(const char* key, uint64_t fallback) noexcept
    {
        const auto* text = std::getenv(key);
        if (!text || !*text)
            return fallback;
        uint64_t value = 0;
        const auto* end = text + std::strlen(text);
        const auto parsed = std::from_chars(text, end, value);
        return parsed.ec == std::errc{} && parsed.ptr == end ? value : 0;
    }

    std::array<ShadowContentCache, GameDx12NativeFrame12::kFrameCount> content_, staticContent_;
    std::array<ShadowPageResources, GameDx12NativeFrame12::kFrameCount> arenas_;
    const std::array<ShadowPageShape, 4> shapes_{{{128, 6}, {256, 6}, {512, 6}, {1024, 6}}};
    std::array<ShadowPageSpec, 4> specs_{};
    std::array<AdaptiveTier, 4> tiers_{};
    bool initialized_ = false;
};
}
