#include "dx12j2bbatch.h"

#include "gr/jobs/JobGroup.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <new>
#include <stdexcept>
#include <type_traits>

#if defined(OGSR_J2B_CPU_FAULT_INJECTION)
#include "j2b_m1_fault.h"
#endif

namespace j2bbatch12
{
namespace
{
using Clock = std::chrono::steady_clock;

static_assert(std::is_nothrow_move_assignable_v<BatchResult12>,
              "BatchResult12 must be nothrow move-assignable");
static_assert(std::is_nothrow_move_assignable_v<Snapshot12>,
              "Snapshot12 must be nothrow move-assignable");

double Ms(Clock::time_point start) noexcept
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

void ClearAsset(XrayAssetData12& asset) noexcept
{
    asset.vertices.clear();
    asset.indices.clear();
    asset.textureName.clear();
    asset.shaderName.clear();
    asset.minimum = {};
    asset.maximum = {};
    asset.sphere = {};
}

void ResetSlot(Slot12& slot) noexcept
{
    slot.valid = false;
    slot.source = {};
    slot.instance = {};
    slot.vertexCount = 0;
    slot.indexCount = 0;
    slot.compactVbHash = 0;
    slot.compactIbHash = 0;
    slot.assetVbHash = 0;
    slot.assetIbHash = 0;
    slot.readError = xraylevel12::ReadError::None;
    slot.readOk = false;
    slot.converted = false;
    slot.completed = false;
    slot.readMs = 0.0;
    slot.convertMs = 0.0;
    slot.error.code = Error12::None;
    slot.error.text[0] = '\0';
    ClearAsset(slot.asset);
}

void ClearPublishedAssets(BatchResult12& result) noexcept
{
    for (Slot12& slot : result.slots)
        ResetSlot(slot);
}

void FailSlot(Slot12& slot, Error12 code, const char* text) noexcept
{
    slot.valid = false;
    slot.converted = false;
    slot.assetVbHash = 0;
    slot.assetIbHash = 0;
    ClearAsset(slot.asset);
    SetErrorInfo(slot.error, code, text);
}

void PublishFailure(BatchResult12& result, const BatchResult12& candidate,
                    Error12 code, const char* text, double totalMs) noexcept
{
    result.ok = false;
    result.identity = candidate.identity;
    result.sectorId = candidate.sectorId;
    result.rootVisualId = candidate.rootVisualId;
    result.instances = candidate.instances;
    result.stableHash = 0;
    result.parseMs = candidate.parseMs;
    result.collectMs = candidate.collectMs;
    result.readVisualMs = candidate.readVisualMs;
    result.convertMs = candidate.convertMs;
    result.grSubmitMs = candidate.grSubmitMs;
    result.grWaitMs = candidate.grWaitMs;
    result.stableHashMs = candidate.stableHashMs;
    result.totalMs = totalMs;
    result.vfsOpenMs = candidate.vfsOpenMs;
    result.vfsCopyMs = candidate.vfsCopyMs;
    result.snapshotHashMs = candidate.snapshotHashMs;
    SetErrorInfo(result.error, code, text);
    ClearPublishedAssets(result);
}

bool SameVisuals(const std::array<uint32_t, kTargetVisualCount>& visuals) noexcept
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
#if defined(OGSR_J2B_CPU_FAULT_INJECTION)
    j2b_m1_fault::MaybeEnablePersistentOom(
        j2b_m1_fault::Point::BeforeConvert);
#endif
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
        FailSlot(slot, Error12::ReadVisual,
                 readReason.empty() ? xraylevel12::ReadErrorMessage(
                                          slot.readError)
                                    : readReason.c_str());
        return false;
    }

    slot.readOk = true;
#if defined(OGSR_J2B_CPU_FAULT_INJECTION)
    j2b_m1_fault::MaybeThrow(j2b_m1_fault::Point::AfterRead);
    j2b_m1_fault::MaybeBlock(j2b_m1_fault::Point::AfterRead);
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
        FailSlot(slot, Error12::Convert,
                 convertReason != nullptr ? convertReason
                                          : "asset conversion failed");
        return false;
    }

    const uint64_t assetBytes =
        static_cast<uint64_t>(asset.vertices.size()) * sizeof(XrayVertex12) +
        static_cast<uint64_t>(asset.indices.size()) * sizeof(uint16_t);
    if (assetBytes > limits.maxAssetBytes)
    {
        FailSlot(slot, Error12::AssetLimit,
                 "asset exceeds per-visual byte limit");
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
    slot.error.code = Error12::None;
    slot.error.text[0] = '\0';
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

void SetErrorInfo(ErrorInfo12& info, Error12 code, const char* text) noexcept
{
    info.code = code;
    size_t i = 0;
    if (text != nullptr)
    {
        for (; i + 1 < sizeof(info.text) && text[i] != '\0'; ++i)
            info.text[i] = text[i];
    }
    info.text[i] = '\0';
}

void SetErrorInfo2(ErrorInfo12& info, Error12 code, const char* first,
                   const char* second) noexcept
{
    info.code = code;
    size_t i = 0;
    if (first != nullptr)
    {
        for (; i + 1 < sizeof(info.text) && first[i] != '\0'; ++i)
            info.text[i] = first[i];
    }
    if (second != nullptr)
    {
        for (size_t j = 0; i + 1 < sizeof(info.text) && second[j] != '\0';
             ++i, ++j)
            info.text[i] = second[j];
    }
    info.text[i] = '\0';
}

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

AcceptStatus12 AcceptBatch12(const BatchResult12& result,
                             const Identity12& live) noexcept
{
    if (!result.ok || result.error.code != Error12::None ||
        result.stableHash == 0)
        return AcceptStatus12::NotReady;
    if (live.levelSession == 0 || live.levelEpoch == 0)
        return AcceptStatus12::NotReady;
    if (result.identity.levelSession != live.levelSession)
        return AcceptStatus12::StaleSession;
    if (result.identity.levelEpoch != live.levelEpoch)
        return AcceptStatus12::StaleEpoch;
    for (const Slot12& slot : result.slots)
        if (!slot.valid)
            return AcceptStatus12::InvalidResult;
    return AcceptStatus12::Accepted;
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
    candidate.vfsOpenMs = snapshot.vfsOpenMs;
    candidate.vfsCopyMs = snapshot.vfsCopyMs;
    candidate.snapshotHashMs = snapshot.snapshotHashMs;

    const Clock::time_point totalStart = Clock::now();
    try
    {
        if (expected.levelSession == 0 || expected.levelEpoch == 0 ||
            snapshot.identity != expected)
        {
            PublishFailure(result, candidate, Error12::InvalidIdentity,
                           "snapshot session/epoch mismatch", Ms(totalStart));
            return false;
        }
        if (snapshot.levelBytes.empty() || snapshot.geomBytes.empty())
        {
            PublishFailure(result, candidate, Error12::EmptyInput,
                           "empty snapshot input", Ms(totalStart));
            return false;
        }
        if (snapshot.levelBytes.size() > limits.maxLevelBytes ||
            snapshot.geomBytes.size() > limits.maxGeomBytes)
        {
            PublishFailure(result, candidate, Error12::InputLimit,
                           "snapshot exceeds input byte limit", Ms(totalStart));
            return false;
        }
        if (!SameVisuals(visuals))
        {
            PublishFailure(result, candidate, Error12::DuplicateVisual,
                           "duplicate target visual", Ms(totalStart));
            return false;
        }

        levelhierarchy12::LevelHierarchy hierarchy;
        std::string parseReason;
        const Clock::time_point parseStart = Clock::now();
        if (!levelhierarchy12::Parse(snapshot.levelBytes, hierarchy,
                                     parseReason))
        {
            candidate.parseMs = Ms(parseStart);
            PublishFailure(result, candidate, Error12::HierarchyParse,
                           parseReason.empty() ? "hierarchy parse failed"
                                               : parseReason.c_str(),
                           Ms(totalStart));
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
            PublishFailure(result, candidate, Error12::SectorCollect,
                           collectReason.empty() ? "sector collection failed"
                                                 : collectReason.c_str(),
                           Ms(totalStart));
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
                SetErrorInfo(candidate.slots[i].error,
                             Error12::MissingVisual,
                             "target visual missing from sector");
                PublishFailure(result, candidate, Error12::MissingVisual,
                               "target visual missing from sector",
                               Ms(totalStart));
                return false;
            }
            if (rootVisualId != 0 && found->rootVisualId != rootVisualId)
            {
                candidate.slots[i].completed = true;
                SetErrorInfo(candidate.slots[i].error, Error12::RootMismatch,
                             "target root mismatch");
                PublishFailure(result, candidate, Error12::RootMismatch,
                               "target root mismatch", Ms(totalStart));
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
        const auto accumulateTimings = [&]() noexcept
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
            const Error12 code = candidate.slots[i].error.code == Error12::None
                                     ? Error12::Convert
                                     : candidate.slots[i].error.code;
            const char* text = candidate.slots[i].error.text[0] != '\0'
                                   ? candidate.slots[i].error.text
                                   : "visual conversion failed";
            PublishFailure(result, candidate, code, text, Ms(totalStart));
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
                const Clock::time_point submitStart = Clock::now();
                for (uint32_t i = 0; i < visuals.size(); ++i)
                {
#if defined(OGSR_J2B_CPU_FAULT_INJECTION)
                    j2b_m1_fault::BeforeSubmit(i);
                    if (j2b_m1_fault::RejectSubmit(i))
                        throw std::runtime_error(
                            "injected submit rejection");
                    if (j2b_m1_fault::SkipSubmit(i))
                        continue;
#endif
                    try
                    {
                        group.Submit([&, i]() { (void)convertOne(i); });
                    }
                    catch (const std::bad_alloc&)
                    {
                        group.DrainNoThrow();
                        PublishFailure(result, candidate,
                                       Error12::SubmitException,
                                       "submit allocation failed",
                                       Ms(totalStart));
                        return false;
                    }
                    catch (const std::exception&)
                    {
                        group.DrainNoThrow();
                        PublishFailure(result, candidate,
                                       Error12::SubmitException,
                                       "submit exception", Ms(totalStart));
                        return false;
                    }
                    catch (...)
                    {
                        group.DrainNoThrow();
                        PublishFailure(result, candidate,
                                       Error12::SubmitException,
                                       "submit unknown exception",
                                       Ms(totalStart));
                        return false;
                    }
                }
                candidate.grSubmitMs = Ms(submitStart);

                const Clock::time_point waitStart = Clock::now();
                try
                {
                    group.Wait();
                }
                catch (const std::bad_alloc&)
                {
                    candidate.grWaitMs = Ms(waitStart);
                    group.DrainNoThrow();
                    PublishFailure(result, candidate,
                                   Error12::WorkerAllocation,
                                   "worker allocation failed",
                                   Ms(totalStart));
                    return false;
                }
                catch (const std::exception&)
                {
                    candidate.grWaitMs = Ms(waitStart);
                    group.DrainNoThrow();
                    PublishFailure(result, candidate, Error12::WorkerException,
                                   "worker exception", Ms(totalStart));
                    return false;
                }
                catch (...)
                {
                    candidate.grWaitMs = Ms(waitStart);
                    group.DrainNoThrow();
                    PublishFailure(result, candidate, Error12::Unknown,
                                   "worker unknown exception", Ms(totalStart));
                    return false;
                }
                candidate.grWaitMs = Ms(waitStart);

                for (uint32_t i = 0; i < visuals.size(); ++i)
                {
                    if (!candidate.slots[i].completed ||
                        !candidate.slots[i].valid)
                    {
                        if (candidate.slots[i].error.code == Error12::None)
                        {
                            SetErrorInfo(candidate.slots[i].error,
                                         Error12::WorkerException,
                                         "accepted job did not publish a result");
                        }
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
            PublishFailure(result, candidate, Error12::TotalAssetLimit,
                           "batch exceeds total asset byte limit",
                           Ms(totalStart));
            return false;
        }

        const Clock::time_point hashStart = Clock::now();
        uint64_t hash = 1469598103934665603ull;
        hash = HashBytes(hash, &candidate.identity.levelSession,
                         sizeof(candidate.identity.levelSession));
        hash = HashBytes(hash, &candidate.identity.levelEpoch,
                         sizeof(candidate.identity.levelEpoch));
        hash = HashBytes(hash, &sectorId, sizeof(sectorId));
        hash = HashBytes(hash, &rootVisualId, sizeof(rootVisualId));
        for (const Slot12& slot : candidate.slots)
            hash = HashSlot(hash, slot);
        candidate.stableHashMs = Ms(hashStart);

        candidate.stableHash = hash;
        candidate.totalMs = Ms(totalStart);
        candidate.ok = true;
        candidate.error.code = Error12::None;
        candidate.error.text[0] = '\0';
        result = std::move(candidate);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        PublishFailure(result, candidate, Error12::AllocationFailure,
                       "batch allocation failed", Ms(totalStart));
        return false;
    }
    catch (const std::exception&)
    {
        PublishFailure(result, candidate, Error12::Exception, "batch exception",
                       Ms(totalStart));
        return false;
    }
    catch (...)
    {
        PublishFailure(result, candidate, Error12::Unknown,
                       "batch unknown exception", Ms(totalStart));
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
