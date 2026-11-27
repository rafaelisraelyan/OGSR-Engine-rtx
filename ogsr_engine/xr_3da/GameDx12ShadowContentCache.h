#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>

namespace game_shadow12
{
// Все поля должны описывать фактические входы записи глубины. Векторы содержат
// точные значения/битовые представления, а не хеши; порядок совпадает с draw.
struct ShadowContentCaster
{
    uint64_t identity = 0, generation = 0;
    std::vector<uint64_t> geometry, material, transform, raster;
    bool known = false;
    bool operator==(const ShadowContentCaster&) const = default;
};

struct ShadowContentView
{
    // resource — идентичность физического ID3D12Resource, generation исключает ABA.
    uint64_t resource = 0, resourceGeneration = 0, session = 0, epoch = 0;
    uint32_t slice = 0, resolution = 0;
    uint64_t light = 0, lightGeneration = 0;
    uint32_t face = 0;
    std::array<uint32_t, 16> matrixBits{};
    std::vector<uint64_t> lightState, raster;
    // Индексы immutable draw sources в пределах поколения сцены, в порядке записи.
    std::vector<uint32_t> staticCasters;
    uint64_t sceneGeneration = 0;
    std::vector<ShadowContentCaster> casters;
    // false при отсутствии хотя бы одного входа, включая membership или pose.
    bool known = false;
};

enum ShadowContentReason : uint32_t
{
    ShadowContentHit = 0, ShadowContentUnknown = 1u << 0, ShadowContentNew = 1u << 1,
    ShadowContentResource = 1u << 2, ShadowContentLight = 1u << 3,
    ShadowContentMatrix = 1u << 4, ShadowContentRaster = 1u << 5,
    ShadowContentCasters = 1u << 6
};
struct ShadowContentStats
{
    uint32_t hits = 0, dirty = 0;
    std::array<uint32_t, 7> reasons{};
};

// Один объект на slot, поток-владелец. Prepare вызывается после завершения
// предыдущего использования slot; fence и завершение GPU проверяет владелец.
class ShadowContentCache
{
    std::vector<ShadowContentView> current_, pending_;
    std::vector<uint32_t> reasons_;
    ShadowContentStats stats_{};
    uint64_t fence_ = 0;
    bool prepared_ = false, quarantined_ = false;

    static bool Known(const ShadowContentView& v) noexcept
    {
        if (!v.known || !v.resource || !v.resourceGeneration || !v.session || !v.epoch ||
            !v.resolution || !v.light || !v.lightGeneration) return false;
        for (const auto& c : v.casters)
            if (!c.known || !c.identity || !c.generation) return false;
        return true;
    }
    static uint32_t Compare(const ShadowContentView& a, const ShadowContentView& b) noexcept
    {
        uint32_t reason = 0;
        if (!Known(a) || !Known(b)) reason |= ShadowContentUnknown;
        if (a.resourceGeneration != b.resourceGeneration || a.session != b.session || a.epoch != b.epoch ||
            a.resolution != b.resolution) reason |= ShadowContentResource;
        if (a.light != b.light || a.lightGeneration != b.lightGeneration || a.face != b.face ||
            a.lightState != b.lightState) reason |= ShadowContentLight;
        if (a.matrixBits != b.matrixBits) reason |= ShadowContentMatrix;
        if (a.raster != b.raster) reason |= ShadowContentRaster;
        if (a.sceneGeneration != b.sceneGeneration || a.staticCasters != b.staticCasters ||
            a.casters != b.casters) reason |= ShadowContentCasters;
        return reason;
    }
public:
    ShadowContentCache() = default;
    ShadowContentCache(const ShadowContentCache&) = delete;
    ShadowContentCache& operator=(const ShadowContentCache&) = delete;

    // Передаётся полный снимок активных слоёв; дубликат physical resource/slice
    // отвергается. При исключении выделения памяти прежний снимок не меняется.
    bool Prepare(std::vector<ShadowContentView> views, uint64_t submitFence)
    {
        if (prepared_ || quarantined_ || !submitFence) return false;
        for (std::size_t i = 0; i < views.size(); ++i)
            for (std::size_t j = 0; j < i; ++j)
                if (views[i].resource && views[i].resource == views[j].resource && views[i].slice == views[j].slice)
                    return false;
        std::vector<uint32_t> reasons;
        reasons.reserve(views.size());
        ShadowContentStats stats{};
        for (const auto& v : views)
        {
            const ShadowContentView* old = nullptr;
            for (const auto& entry : current_)
                if (entry.resource == v.resource && entry.slice == v.slice) { old = &entry; break; }
            uint32_t reason = old ? Compare(*old, v) : ShadowContentNew;
            if (!Known(v)) reason |= ShadowContentUnknown;
            reasons.push_back(reason);
            if (reason) ++stats.dirty; else ++stats.hits;
            for (unsigned bit = 0; bit < stats.reasons.size(); ++bit)
                if (reason & (1u << bit)) ++stats.reasons[bit];
        }
        pending_ = std::move(views); reasons_ = std::move(reasons); stats_ = stats;
        fence_ = submitFence; prepared_ = true;
        return true;
    }
    const std::vector<uint32_t>& Reasons() const noexcept { return reasons_; }
    const ShadowContentStats& Stats() const noexcept { return stats_; }
    bool Prepared() const noexcept { return prepared_; }
    bool Expects(uint64_t fence) const noexcept { return prepared_ && fence_ == fence; }
    void DiscardContents() noexcept { current_.clear(); }

    // Вызвать только после Execute и успешного Signal именно этого fence.
    // После отправки каждая dirty-карта должна быть действительно перезаписана.
    bool CommitSubmitted(uint64_t signaledFence) noexcept
    {
        if (!prepared_ || quarantined_ || signaledFence != fence_)
        { Quarantine(); return false; }
        current_ = std::move(pending_);
        prepared_ = false; fence_ = 0;
        return true;
    }
    bool CancelUnsubmitted() noexcept
    {
        if (!prepared_ || quarantined_) return false;
        pending_.clear(); reasons_.clear(); stats_ = {}; fence_ = 0; prepared_ = false;
        return true;
    }
    // При неопределённой отправке состояние навсегда запрещено использовать.
    void Quarantine() noexcept
    {
        current_.clear(); pending_.clear(); reasons_.clear(); stats_ = {};
        fence_ = 0; prepared_ = false; quarantined_ = true;
    }
};
}
