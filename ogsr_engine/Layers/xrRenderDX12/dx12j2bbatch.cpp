#include "dx12j2bbatch.h"

#include "gr/jobs/JobGroup.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <new>
#include <stdexcept>

#if defined(OGSR_J2B_CPU_FAULT_INJECTION)
#include "j2b_m1_fault.h"
#endif

namespace j2bbatch12
{
namespace
{
using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start)
        .count();
}

uint64_t HashFloat(uint64_t hash, float value) noexcept
{
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    return HashBytes(hash, &raw, sizeof(raw));
}

void ClearPublishedAssets(BatchResult12& result) noexcept
{
    for (Slot12& slot : result.slots)
    {
        slot.valid = false;
        slot.asset = {};
    }
}

void Fail(BatchResult12& result, const char* text)
{
    result.ok = false;
    result.stableHash = 0;
    ClearPublishedAssets(result);
    result.reason = text != nullptr ? text : "batch preparation failed";
}

bool SameVisuals(const std::array<uint32_t, kTargetVisualCount>& visuals)
{
    for (uint32_t i = 0; i < visuals.size(); ++i)
        for (uint32_t j = i + 1; j < visuals.size(); ++j)
            if (visuals[i] == visuals[j])
                return false;
    return true;
}

bool ConvertSlot(const Snapshot12& snapshot, uint32_t visualId,
                 const InstanceKey12& instance, const Limits12& limits,
                 Slot12& slot)
{
    const Clock::time_point readStart = Clock::now();
    xraylevel12::Geometry geometry;
    std::string readReason;
    slot.readError = xraylevel12::ReadVisual(
        snapshot.levelBytes, snapshot.geomBytes, visualId, geometry,
        readReason, xraylevel12::Limits{limits.maxLevelBytes,
                                        limits.maxGeomBytes,
                                        limits.maxAssetBytes});
    slot.readMs = Ms(readStart);
    slot.completed = true;
    if (slot.readError != xraylevel12::ReadError::None)
    {
        slot.readOk = false;
        slot.reason = readReason.empty()
                          ? xraylevel12::ReadErrorMessage(slot.readError)
                          : readReason;
        return false;
    }

    slot.readOk = true;
#if defined(OGSR_J2B_CPU_FAULT_INJECTION)
    j2b_m1_fault::MaybeThrow(j2b_m1_fault::Point::AfterRead);
#endif
    slot.source.levelSession = snapshot.identity.levelSession;
    slot.source.visualId = geometry.visual_id;
    slot.source.sourceVbId = geometry.source_vb_id;
    slot.source.sourceVBase = geometry.source_vbase;
    slot.source.sourceVCount = geometry.source_vcount;
    slot.source.sourceIbId = geometry.source_ib_id;
    slot.source.sourceIBase = geometry.source_ibase;
    slot.source.sourceICount = geometry.source_icount;
    slot.source.sourceVertexByteOffset = geometry.source_vertex_byte_offset;
    slot.source.sourceIndexByteOffset = geometry.source_index_byte_offset;
    slot.source.compactVbHash = HashBytes(
        1469598103934665603ull, geometry.vertex_bytes.data(),
        geometry.vertex_bytes.size());
    slot.source.compactIbHash = HashBytes(
        1469598103934665603ull, geometry.indices.data(),
        geometry.indices.size() * sizeof(uint16_t));
    slot.compactVbHash = slot.source.compactVbHash;
    slot.compactIbHash = slot.source.compactIbHash;
    slot.instance = instance;
    slot.vertexCount = geometry.vertex_count;
    slot.indexCount = geometry.index_count;

    XrayAssetData12 asset;
    const char* convertReason = nullptr;
    const Clock::time_point convertStart = Clock::now();
    const bool converted = BuildXrayLevelAsset12(geometry, asset,
                                                  &convertReason);
    slot.convertMs = Ms(convertStart);
    if (!converted)
    {
        slot.converted = false;
        slot.reason = convertReason != nullptr ? convertReason
                                               : "asset conversion failed";
        return false;
    }

    const uint64_t assetBytes =
        static_cast<uint64_t>(asset.vertices.size()) * sizeof(XrayVertex12) +
        static_cast<uint64_t>(asset.indices.size()) * sizeof(uint16_t);
    if (assetBytes > limits.maxAssetBytes)
    {
        slot.converted = false;
        slot.reason = "asset exceeds per-visual byte limit";
        return false;
    }
    slot.assetVbHash = HashBytes(1469598103934665603ull,
                                 asset.vertices.data(),
                                 asset.vertices.size() * sizeof(XrayVertex12));
    slot.assetIbHash = HashBytes(1469598103934665603ull, asset.indices.data(),
                                 asset.indices.size() * sizeof(uint16_t));
#if defined(OGSR_J2B_CPU_FAULT_INJECTION)
    j2b_m1_fault::MaybeThrow(j2b_m1_fault::Point::BeforePublish);
#endif
    slot.asset = std::move(asset);
    slot.converted = true;
    slot.valid = true;
    slot.reason.clear();
    return true;
}

uint64_t HashSource(uint64_t hash, const SourceKey12& source) noexcept
{
    hash = HashBytes(hash, &source.levelSession, sizeof(source.levelSession));
    hash = HashBytes(hash, &source.visualId, sizeof(source.visualId));
    hash = HashBytes(hash, &source.sourceVbId, sizeof(source.sourceVbId));
    hash = HashBytes(hash, &source.sourceVBase, sizeof(source.sourceVBase));
    hash = HashBytes(hash, &source.sourceVCount, sizeof(source.sourceVCount));
    hash = HashBytes(hash, &source.sourceIbId, sizeof(source.sourceIbId));
    hash = HashBytes(hash, &source.sourceIBase, sizeof(source.sourceIBase));
    hash = HashBytes(hash, &source.sourceICount, sizeof(source.sourceICount));
    hash = HashBytes(hash, &source.sourceVertexByteOffset,
                     sizeof(source.sourceVertexByteOffset));
    hash = HashBytes(hash, &source.sourceIndexByteOffset,
                     sizeof(source.sourceIndexByteOffset));
    hash = HashBytes(hash, &source.compactVbHash,
                     sizeof(source.compactVbHash));
    hash = HashBytes(hash, &source.compactIbHash,
                     sizeof(source.compactIbHash));
    return hash;
}

uint64_t HashSlot(uint64_t hash, const Slot12& slot) noexcept
{
    hash = HashSource(hash, slot.source);
    hash = HashBytes(hash, &slot.instance.sectorId,
                     sizeof(slot.instance.sectorId));
    hash = HashBytes(hash, &slot.instance.rootVisualId,
                     sizeof(slot.instance.rootVisualId));
    hash = HashBytes(hash, &slot.instance.occurrenceOrdinal,
                     sizeof(slot.instance.occurrenceOrdinal));
    hash = HashBytes(hash, &slot.instance.visualId,
                     sizeof(slot.instance.visualId));
    hash = HashBytes(hash, &slot.vertexCount, sizeof(slot.vertexCount));
    hash = HashBytes(hash, &slot.indexCount, sizeof(slot.indexCount));
    hash = HashBytes(hash, &slot.assetVbHash, sizeof(slot.assetVbHash));
    hash = HashBytes(hash, &slot.assetIbHash, sizeof(slot.assetIbHash));
    hash = HashFloat(hash, slot.asset.minimum.x);
    hash = HashFloat(hash, slot.asset.minimum.y);
    hash = HashFloat(hash, slot.asset.minimum.z);
    hash = HashFloat(hash, slot.asset.maximum.x);
    hash = HashFloat(hash, slot.asset.maximum.y);
    hash = HashFloat(hash, slot.asset.maximum.z);
    hash = HashFloat(hash, slot.asset.sphere.center.x);
    hash = HashFloat(hash, slot.asset.sphere.center.y);
    hash = HashFloat(hash, slot.asset.sphere.center.z);
    hash = HashFloat(hash, slot.asset.sphere.radius);
    return hash;
}
} // namespace

uint64_t HashBytes(uint64_t seed, const void* data, size_t size) noexcept
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        seed ^= bytes[i];
        seed *= 1099511628211ull;
    }
    return seed;
}

uint64_t HashSnapshot(const Snapshot12& snapshot) noexcept
{
    uint64_t hash = 1469598103934665603ull;
    hash = HashBytes(hash, snapshot.levelBytes.data(),
                     snapshot.levelBytes.size());
    hash = HashBytes(hash, snapshot.geomBytes.data(), snapshot.geomBytes.size());
    hash = HashBytes(hash, &snapshot.identity.levelSession,
                     sizeof(snapshot.identity.levelSession));
    hash = HashBytes(hash, &snapshot.identity.levelEpoch,
                     sizeof(snapshot.identity.levelEpoch));
    return hash;
}

bool PrepareBatchImpl(const Snapshot12& snapshot, const Identity12& expected,
                      uint32_t sectorId, uint32_t rootVisualId,
                      const std::array<uint32_t, kTargetVisualCount>& visuals,
                      gr::jobs::JobSystem* jobs, bool parallel,
                      BatchResult12& result, const Limits12& limits) noexcept
{
    BatchResult12 candidate;
    candidate.identity = expected;
    candidate.sectorId = sectorId;
    candidate.rootVisualId = rootVisualId;

    const Clock::time_point totalStart = Clock::now();
    try
    {
        if (expected.levelSession == 0 || expected.levelEpoch == 0 ||
            snapshot.identity != expected)
        {
            Fail(candidate, "snapshot session/epoch mismatch");
            result = std::move(candidate);
            return false;
        }
        if (snapshot.levelBytes.empty() || snapshot.geomBytes.empty())
        {
            Fail(candidate, "empty snapshot input");
            result = std::move(candidate);
            return false;
        }
        if (snapshot.levelBytes.size() > limits.maxLevelBytes ||
            snapshot.geomBytes.size() > limits.maxGeomBytes)
        {
            Fail(candidate, "snapshot exceeds input byte limit");
            result = std::move(candidate);
            return false;
        }
        if (!SameVisuals(visuals))
        {
            Fail(candidate, "duplicate target visual");
            result = std::move(candidate);
            return false;
        }

        levelhierarchy12::LevelHierarchy hierarchy;
        std::string parseReason;
        const Clock::time_point parseStart = Clock::now();
        if (!levelhierarchy12::Parse(snapshot.levelBytes, hierarchy,
                                     parseReason))
        {
            candidate.reason = parseReason.empty() ? "hierarchy parse failed"
                                                   : parseReason;
            candidate.parseMs = Ms(parseStart);
            Fail(candidate, candidate.reason.c_str());
            result = std::move(candidate);
            return false;
        }
        candidate.parseMs = Ms(parseStart);

        levelhierarchy12::CollectBudget budget;
        budget.maxInstances = limits.maxInstances;
        budget.maxNodeVisits = limits.maxNodeVisits;
        budget.maxEdgeVisits = limits.maxEdgeVisits;
        levelhierarchy12::CollectStats stats;
        std::vector<levelhierarchy12::LeafInstance> selected;
        std::string collectReason;
        const Clock::time_point collectStart = Clock::now();
        const levelhierarchy12::CollectStatus collect =
            levelhierarchy12::CollectSectorLeaves(
                hierarchy, sectorId, budget, selected, stats, collectReason);
        candidate.collectMs = Ms(collectStart);
        candidate.instances = static_cast<uint32_t>(selected.size());
        if (collect != levelhierarchy12::CollectStatus::Complete)
        {
            candidate.reason = collectReason.empty()
                                   ? "sector collection failed"
                                   : collectReason;
            Fail(candidate, candidate.reason.c_str());
            result = std::move(candidate);
            return false;
        }

        std::array<InstanceKey12, kTargetVisualCount> instanceKeys{};
        for (uint32_t i = 0; i < visuals.size(); ++i)
        {
            const uint32_t visualId = visuals[i];
            const levelhierarchy12::LeafInstance* found = nullptr;
            for (const auto& instance : selected)
            {
                if (instance.visualId == visualId)
                {
                    found = &instance;
                    break;
                }
            }
            if (found == nullptr)
            {
                candidate.slots[i].completed = true;
                candidate.slots[i].reason = "target visual missing from sector";
                candidate.reason = candidate.slots[i].reason;
                Fail(candidate, candidate.reason.c_str());
                result = std::move(candidate);
                return false;
            }
            if (rootVisualId != 0 && found->rootVisualId != rootVisualId)
            {
                candidate.slots[i].completed = true;
                candidate.slots[i].reason = "target root mismatch";
                candidate.reason = candidate.slots[i].reason;
                Fail(candidate, candidate.reason.c_str());
                result = std::move(candidate);
                return false;
            }
            instanceKeys[i].sectorId = sectorId;
            instanceKeys[i].rootVisualId = found->rootVisualId;
            instanceKeys[i].occurrenceOrdinal = found->referenceOrdinal;
            instanceKeys[i].visualId = visualId;
        }

        const auto convertOne = [&](uint32_t i)
        {
            return ConvertSlot(snapshot, visuals[i], instanceKeys[i], limits,
                               candidate.slots[i]);
        };
        const auto accumulateTimings = [&]()
        {
            candidate.readVisualMs = 0.0;
            candidate.convertMs = 0.0;
            for (const Slot12& slot : candidate.slots)
            {
                candidate.readVisualMs += slot.readMs;
                candidate.convertMs += slot.convertMs;
            }
        };
        const auto failSlot = [&](uint32_t i) -> bool
        {
            candidate.reason = candidate.slots[i].reason.empty()
                                   ? "visual conversion failed"
                                   : candidate.slots[i].reason;
            Fail(candidate, candidate.reason.c_str());
            result = std::move(candidate);
            return false;
        };

        const bool useParallel =
            parallel && jobs != nullptr && jobs->IsInitialized();
        if (!useParallel)
        {
            for (uint32_t i = 0; i < visuals.size(); ++i)
            {
                if (!convertOne(i))
                    return failSlot(i);
            }
            accumulateTimings();
        }
        else
        {
            gr::jobs::JobGroup group = jobs->CreateGroup();
            if (!group.IsValid())
            {
                // A system that became unavailable falls back to the serial
                // baseline rather than publishing a partial batch.
                for (uint32_t i = 0; i < visuals.size(); ++i)
                {
                    if (!convertOne(i))
                        return failSlot(i);
                }
                accumulateTimings();
            }
            else
            {
                for (uint32_t i = 0; i < visuals.size(); ++i)
                {
#if defined(OGSR_J2B_CPU_FAULT_INJECTION)
                    if (j2b_m1_fault::RejectSubmit(i))
                        throw std::runtime_error(
                            "injected submit rejection");
                    if (j2b_m1_fault::SkipSubmit(i))
                        continue;
#endif
                    group.Submit([&, i]() { (void)convertOne(i); });
                }
                try
                {
                    group.Wait();
                }
                catch (const std::bad_alloc&)
                {
                    Fail(candidate, "worker allocation failed");
                    result = std::move(candidate);
                    return false;
                }
                catch (const std::exception&)
                {
                    Fail(candidate, "worker exception");
                    result = std::move(candidate);
                    return false;
                }
                catch (...)
                {
                    Fail(candidate, "worker unknown exception");
                    result = std::move(candidate);
                    return false;
                }
                for (uint32_t i = 0; i < visuals.size(); ++i)
                {
                    if (!candidate.slots[i].completed ||
                        !candidate.slots[i].valid)
                    {
                        if (candidate.slots[i].reason.empty())
                            candidate.slots[i].reason =
                                "accepted job did not publish a result";
                        return failSlot(i);
                    }
                }
                accumulateTimings();
            }
        }

        uint64_t totalAssetBytes = 0;
        for (const Slot12& slot : candidate.slots)
        {
            totalAssetBytes +=
                static_cast<uint64_t>(slot.asset.vertices.size()) *
                    sizeof(XrayVertex12) +
                static_cast<uint64_t>(slot.asset.indices.size()) *
                    sizeof(uint16_t);
        }
        if (totalAssetBytes > limits.maxTotalAssetBytes)
        {
            Fail(candidate, "batch exceeds total asset byte limit");
            result = std::move(candidate);
            return false;
        }

        uint64_t hash = 1469598103934665603ull;
        hash = HashBytes(hash, &candidate.identity.levelSession,
                         sizeof(candidate.identity.levelSession));
        hash = HashBytes(hash, &candidate.identity.levelEpoch,
                         sizeof(candidate.identity.levelEpoch));
        hash = HashBytes(hash, &sectorId, sizeof(sectorId));
        hash = HashBytes(hash, &rootVisualId, sizeof(rootVisualId));
        for (const Slot12& slot : candidate.slots)
            hash = HashSlot(hash, slot);

        candidate.stableHash = hash;
        candidate.totalMs = Ms(totalStart);
        candidate.ok = true;
        candidate.reason.clear();
        result = std::move(candidate);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        Fail(candidate, "batch allocation failed");
        result = std::move(candidate);
        return false;
    }
    catch (const std::exception&)
    {
        Fail(candidate, "batch exception");
        result = std::move(candidate);
        return false;
    }
    catch (...)
    {
        Fail(candidate, "batch unknown exception");
        result = std::move(candidate);
        return false;
    }
}

bool PrepareBatch(const Snapshot12& snapshot, const Identity12& expected,
                  uint32_t sectorId, uint32_t rootVisualId,
                  const std::array<uint32_t, kTargetVisualCount>& visuals,
                  BatchResult12& result, const Limits12& limits) noexcept
{
    return PrepareBatchImpl(snapshot, expected, sectorId, rootVisualId, visuals,
                            nullptr, false, result, limits);
}

bool PrepareBatch(const Snapshot12& snapshot, const Identity12& expected,
                  uint32_t sectorId, uint32_t rootVisualId,
                  const std::array<uint32_t, kTargetVisualCount>& visuals,
                  gr::jobs::JobSystem* jobs, bool parallel,
                  BatchResult12& result, const Limits12& limits) noexcept
{
    return PrepareBatchImpl(snapshot, expected, sectorId, rootVisualId, visuals,
                            jobs, parallel, result, limits);
}
} // namespace j2bbatch12
