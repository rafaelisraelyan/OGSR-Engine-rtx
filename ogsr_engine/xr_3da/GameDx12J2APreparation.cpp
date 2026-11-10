#include "stdafx.h"
#include "GameDx12J2APreparation.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "FS.h"
#include "LocatorAPI_defs.h"

#include "dx12levelhierarchy.h"
#include "dx12packetplan.h"
#include "dx12renderprep.h"
#include "dx12scopeguard.h"
#include "dx12xraylevelgeometry.h"

#if defined(OGSR_J2A_CPU_FAULT_INJECTION)
#include "j2a_r6_fault.h"
#endif

#include "gr/jobs/JobSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
gr::jobs::JobSystem g_j2aJobs;
bool g_j2aJobsReady = false;

using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start)
        .count();
}

const char* LookupStatusName(levelhierarchy12::SectorLookupStatus status)
{
    switch (status)
    {
    case levelhierarchy12::SectorLookupStatus::Found:
        return "Found";
    case levelhierarchy12::SectorLookupStatus::NotFound:
        return "NotFound";
    case levelhierarchy12::SectorLookupStatus::LimitExceeded:
        return "LimitExceeded";
    case levelhierarchy12::SectorLookupStatus::InvalidHierarchy:
        return "InvalidHierarchy";
    }
    return "Unknown";
}

const char* CollectStatusName(levelhierarchy12::CollectStatus status)
{
    switch (status)
    {
    case levelhierarchy12::CollectStatus::Complete:
        return "Complete";
    case levelhierarchy12::CollectStatus::LimitExceeded:
        return "LimitExceeded";
    case levelhierarchy12::CollectStatus::InvalidHierarchy:
        return "InvalidHierarchy";
    }
    return "Unknown";
}

uint32_t EnvU32(const char* name, uint32_t fallback, uint32_t minimum,
                uint32_t maximum)
{
    const char* text = std::getenv(name);
    if (text == nullptr || text[0] == '\0')
        return fallback;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0')
        return fallback;
    if (parsed < minimum)
        return minimum;
    if (parsed > maximum)
        return maximum;
    return static_cast<uint32_t>(parsed);
}

bool ReadVfsFile(const char* name, uint64_t maxBytes,
                 std::vector<uint8_t>& bytes, std::string& reason)
{
    IReader* reader = FS.r_open(fsgame::level, name);
    if (reader == nullptr)
    {
        reason = std::string("VFS open failed: ") + name;
        return false;
    }
    auto closeReader = dx12scope::MakeScopeGuard(
        [&reader]()
        {
            if (reader != nullptr)
                FS.r_close(reader);
        });
    (void)closeReader;

    const size_t size = reader->length();
    if (size == 0 || static_cast<uint64_t>(size) > maxBytes)
    {
        reason = std::string("VFS size rejected: ") + name;
        return false;
    }
    try
    {
        bytes.resize(size);
    }
    catch (const std::bad_alloc&)
    {
        reason = "VFS read allocation failed";
        return false;
    }
    catch (const std::length_error&)
    {
        reason = "VFS read allocation failed";
        return false;
    }
    std::memcpy(bytes.data(), reader->begin(), size);
    return true;
}

bool EnsureJobs(std::string& reason)
{
    if (g_j2aJobsReady)
        return true;
    gr::jobs::JobConfig config;
    config.backgroundWorkers = 0;
    config.automaticWorkers = true;
    config.maxAutoBackgroundWorkers = 4;
    config.defaultGrainSize = 16;
    config.enableStats = true;
    if (!g_j2aJobs.Initialize(config))
    {
        reason = "GR JobSystem initialization failed";
        return false;
    }
    g_j2aJobsReady = true;
    return true;
}

uint64_t Fnv1a(uint64_t hash, const void* data, size_t size)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

struct VisualInfo
{
    uint32_t indexCount = 0;
    levelhierarchy12::Bounds bounds{};
    xraylevel12::Sphere sphere{};
};

void HashFloat(uint64_t& hash, float value)
{
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    hash = Fnv1a(hash, &raw, sizeof(raw));
}

// Success-only counters and hash are never visible after a failed run. Stage
// timings, sector/root provenance and budget visit counts stay available for
// diagnostics because they describe how far preparation got before the error.
void ClearSuccessMetadata(GameDx12J2AResult12& result) noexcept
{
    result.ok = false;
    result.instances = 0;
    result.uniqueVisuals = 0;
    result.packets = 0;
    result.visiblePackets = 0;
    result.culledPackets = 0;
    result.batches = 0;
    result.lists = 0;
    result.stableHash = 0;
    result.serialMs = 0.0;
    result.parallelMs = 0.0;
    result.verifyMs = 0.0;
}

// Text is diagnostic-only. Its allocation failure must never change the
// returned status or the fixed error code.
void SetReasonBestEffort(GameDx12J2AResult12& result,
                         const char* text) noexcept
{
    if (text == nullptr)
        return;
    try
    {
        result.reason.assign(text);
    }
    catch (...)
    {
        result.reason.clear();
    }
}

// Formats into a fixed stack buffer and isolates the engine logging sink.
// A logging allocation failure is not a preparation failure.
void LogBestEffort(const char* format, ...) noexcept
{
    if (format == nullptr)
        return;
    char buffer[2048];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (written <= 0)
        return;
    try
    {
        Msg("%s", buffer);
    }
    catch (...)
    {
    }
}

bool Fail(GameDx12J2AResult12& result, GameDx12J2AError12 error,
          const char* text) noexcept
{
    ClearSuccessMetadata(result);
    result.error = error;
    SetReasonBestEffort(result, text);
    return false;
}

bool FailCurrent(GameDx12J2AResult12& result,
                 GameDx12J2AError12 error) noexcept
{
    ClearSuccessMetadata(result);
    result.error = error;
    return false;
}

// Keeps the established stage-prefixed reason text without heap concatenation.
bool FailPrefixed(GameDx12J2AResult12& result, GameDx12J2AError12 error,
                  const char* prefix) noexcept
{
    char buffer[512];
    const int written =
        std::snprintf(buffer, sizeof(buffer), "%s%s", prefix,
                      result.reason.empty() ? "unknown" : result.reason.c_str());
    if (written <= 0)
        return FailCurrent(result, error);
    ClearSuccessMetadata(result);
    result.error = error;
    SetReasonBestEffort(result, buffer);
    return false;
}
} // namespace

bool GameDx12J2A_IsEnabled()
{
    const char* text = std::getenv("OGSR_DX12_J2A_GEOMETRY");
    return text != nullptr && text[0] != '\0' && std::strcmp(text, "0") != 0;
}

namespace
{
bool RunJ2A(const char* levelName, uint64_t levelSession,
            uint64_t levelEpoch, const DirectX::XMFLOAT4X4& view,
            const DirectX::XMFLOAT4X4& projection,
            GameDx12J2AResult12& result)
{
    result = {};
    try
    {
        if (levelName == nullptr || levelName[0] == '\0')
            return Fail(result, GameDx12J2AError12::InvalidArguments,
                        "missing level name");
        if (levelSession == 0 || levelEpoch == 0)
            return Fail(result, GameDx12J2AError12::InvalidArguments,
                        "invalid session/epoch");
        if (!EnsureJobs(result.reason))
            return FailCurrent(result, GameDx12J2AError12::JobSystem);

        LogBestEffort("ph_dx12_j2a: begin level='%s' session=%llu epoch=%llu",
            levelName,
            static_cast<unsigned long long>(levelSession),
            static_cast<unsigned long long>(levelEpoch));

        xraylevel12::Limits limits{};
        std::vector<uint8_t> levelBytes;
        std::vector<uint8_t> geomBytes;
        const Clock::time_point vfsStart = Clock::now();
        const bool vfsOk =
            ReadVfsFile(fsgame::level_files::level, limits.maxLevelBytes,
                        levelBytes, result.reason) &&
            ReadVfsFile(fsgame::level_files::level_geom, limits.maxGeomBytes,
                        geomBytes, result.reason);
        result.vfsMs = ElapsedMs(vfsStart);
        if (!vfsOk)
            return FailCurrent(result, GameDx12J2AError12::Vfs);

        levelhierarchy12::LevelHierarchy hierarchy;
        const Clock::time_point parseStart = Clock::now();
        const bool parseOk =
            levelhierarchy12::Parse(levelBytes, hierarchy, result.reason);
        result.parseMs = ElapsedMs(parseStart);
        if (!parseOk)
            return FailCurrent(result, GameDx12J2AError12::Parse);

        const uint32_t maxInstances =
            EnvU32("OGSR_DX12_J2A_MAX_INSTANCES", 24, 1, 32);
        constexpr uint32_t kLookupNodeLimit = 200000;
        constexpr uint32_t kLookupEdgeLimit = 400000;
        constexpr uint32_t kCollectNodeLimit = 200000;
        constexpr uint32_t kCollectEdgeLimit = 400000;

        levelhierarchy12::SectorLookupStats lookupStats;
        uint32_t chosenSector = 0;
        const Clock::time_point lookupStart = Clock::now();
        const levelhierarchy12::SectorLookupStatus lookupStatus =
            levelhierarchy12::FindSectorForVisual(
                hierarchy, 9, kLookupNodeLimit, kLookupEdgeLimit,
                chosenSector, lookupStats, result.reason);
        result.lookupMs = ElapsedMs(lookupStart);
        result.lookupNodeVisits = lookupStats.nodeVisits;
        result.lookupEdgeVisits = lookupStats.edgeVisits;
        LogBestEffort("ph_dx12_j2a: lookup_stats index_nodes=%llu index_edges=%llu "
            "traversal_nodes=%llu traversal_edges=%llu total_nodes=%llu "
            "total_edges=%llu",
            static_cast<unsigned long long>(lookupStats.indexNodeVisits),
            static_cast<unsigned long long>(lookupStats.indexEdgeVisits),
            static_cast<unsigned long long>(lookupStats.traversalNodeVisits),
            static_cast<unsigned long long>(lookupStats.traversalEdgeVisits),
            static_cast<unsigned long long>(lookupStats.nodeVisits),
            static_cast<unsigned long long>(lookupStats.edgeVisits));
        if (lookupStatus != levelhierarchy12::SectorLookupStatus::Found)
        {
            LogBestEffort("ph_dx12_j2a: lookup visual=9 status=%s sector=%u "
                          "nodes=%llu edges=%llu reason='%s'",
                          LookupStatusName(lookupStatus), chosenSector,
                          static_cast<unsigned long long>(lookupStats.nodeVisits),
                          static_cast<unsigned long long>(lookupStats.edgeVisits),
                          result.reason.c_str());
            return FailPrefixed(result, GameDx12J2AError12::Lookup,
                                "visual 9 lookup failed: ");
        }

        bool chosenRootFound = false;
        for (const levelhierarchy12::Sector& sector : hierarchy.sectors)
        {
            if (sector.index == chosenSector)
            {
                result.sectorRoot = sector.rootVisualId;
                chosenRootFound = true;
                break;
            }
        }
        if (!chosenRootFound)
            return Fail(result, GameDx12J2AError12::Lookup,
                        "chosen sector root is missing");
        result.sectorIndex = chosenSector;

        levelhierarchy12::CollectStats collectStats;
        std::vector<levelhierarchy12::LeafInstance> selected;
        const levelhierarchy12::CollectBudget collectBudget{
            maxInstances, kCollectNodeLimit, kCollectEdgeLimit};
        const Clock::time_point collectStart = Clock::now();
        const levelhierarchy12::CollectStatus collectStatus =
            levelhierarchy12::CollectSectorLeaves(
                hierarchy, chosenSector, collectBudget, selected, collectStats,
                result.reason);
        result.collectMs = ElapsedMs(collectStart);
        result.collectNodeVisits = collectStats.nodeVisits;
        result.collectEdgeVisits = collectStats.edgeVisits;
        result.collectMaxNodeVisits = collectStats.appliedMaxNodeVisits;
        result.collectMaxEdgeVisits = collectStats.appliedMaxEdgeVisits;
        result.collectMaxInstances = collectStats.appliedMaxInstances;
        LogBestEffort("ph_dx12_j2a: collect_stats index_nodes=%llu index_edges=%llu "
            "traversal_nodes=%llu traversal_edges=%llu total_nodes=%llu "
            "total_edges=%llu instances=%u",
            static_cast<unsigned long long>(collectStats.indexNodeVisits),
            static_cast<unsigned long long>(collectStats.indexEdgeVisits),
            static_cast<unsigned long long>(collectStats.traversalNodeVisits),
            static_cast<unsigned long long>(collectStats.traversalEdgeVisits),
            static_cast<unsigned long long>(collectStats.nodeVisits),
            static_cast<unsigned long long>(collectStats.edgeVisits),
            collectStats.instances);
        if (collectStatus != levelhierarchy12::CollectStatus::Complete)
        {
            LogBestEffort("ph_dx12_j2a: collect sector=%u status=%s nodes=%llu "
                "edges=%llu instances=%u reason='%s'",
                chosenSector, CollectStatusName(collectStatus),
                static_cast<unsigned long long>(collectStats.nodeVisits),
                static_cast<unsigned long long>(collectStats.edgeVisits),
                collectStats.instances, result.reason.c_str());
            return FailPrefixed(result, GameDx12J2AError12::Collect,
                                "sector collect failed: ");
        }

        // The selected sector must actually emit the requested drawable
        // visual. This is a second, independent check after the targeted
        // reverse lookup; a mismatch must never publish a snapshot.
        bool targetPresent = false;
        for (const levelhierarchy12::LeafInstance& instance : selected)
        {
            if (instance.sectorIndex != chosenSector)
            {
                LogBestEffort("ph_dx12_j2a: provenance mismatch sector=%u "
                              "collected=%u",
                              chosenSector, instance.sectorIndex);
                return Fail(result, GameDx12J2AError12::Target,
                            "collected instance sector mismatch");
            }
            if (instance.visualId == 9)
                targetPresent = true;
        }
        if (!targetPresent)
        {
            LogBestEffort("ph_dx12_j2a: target visual=9 missing sector=%u "
                          "instances=%u",
                          chosenSector,
                          static_cast<uint32_t>(selected.size()));
            return Fail(result, GameDx12J2AError12::Target,
                        "collected sector does not contain visual 9");
        }

        std::vector<uint32_t> uniqueVisuals;
        if (!levelhierarchy12::BuildUniqueOrder(selected, uniqueVisuals,
                                                result.reason))
            return FailCurrent(result, GameDx12J2AError12::Geometry);

        std::unordered_map<uint32_t, VisualInfo> visualInfo;
        const Clock::time_point readVisualStart = Clock::now();
        for (uint32_t visualId : uniqueVisuals)
        {
            xraylevel12::Geometry geometry;
            std::string readReason;
            const xraylevel12::ReadError error = xraylevel12::ReadVisual(
                levelBytes, geomBytes, visualId, geometry, readReason, limits);
            if (error != xraylevel12::ReadError::None)
            {
                LogBestEffort("ph_dx12_j2a: skip visual=%u reason='%s'",
                    visualId,
                    readReason.empty() ? xraylevel12::ReadErrorMessage(error)
                                       : readReason.c_str());
                continue;
            }
            if (geometry.visual_id != visualId || geometry.index_count == 0 ||
                geometry.vertex_count == 0)
            {
                LogBestEffort("ph_dx12_j2a: skip visual=%u invalid geometry identity",
                    visualId);
                continue;
            }
            VisualInfo info;
            info.indexCount = geometry.index_count;
            std::memcpy(info.bounds.minimum, geometry.bounds.minimum,
                        sizeof(info.bounds.minimum));
            std::memcpy(info.bounds.maximum, geometry.bounds.maximum,
                        sizeof(info.bounds.maximum));
            info.sphere = geometry.sphere;
            visualInfo.emplace(visualId, info);
            // Do not retain vertex/index payloads in the frame snapshot.
            geometry.clear();
        }
        result.readVisualMs = ElapsedMs(readVisualStart);
        // The requested drawable must survive validation itself, not only the
        // hierarchy collection. ReadVisual may reject visual 9 while other
        // visuals remain valid; publishing those would turn a missing target
        // into a false PASS.
        if (visualInfo.find(9) == visualInfo.end())
        {
            LogBestEffort("ph_dx12_j2a: target visual=9 was not validated");
            return Fail(result, GameDx12J2AError12::Target,
                        "target visual 9 was not validated");
        }
        if (visualInfo.size() < 2)
            return Fail(result, GameDx12J2AError12::Geometry,
                        "fewer than two validated visuals");

        std::vector<renderprep12::Object12> objects;
        objects.reserve(selected.size());
        for (const levelhierarchy12::LeafInstance& instance : selected)
        {
            const auto found = visualInfo.find(instance.visualId);
            if (found == visualInfo.end())
                continue;
            const VisualInfo& info = found->second;
            renderprep12::Object12 object;
            object.id = (static_cast<uint64_t>(instance.visualId) << 32) |
                        (static_cast<uint64_t>(instance.referenceOrdinal) + 1);
            object.firstIndex = 0;
            object.indexCount = info.indexCount;
            object.baseVertex = 0;
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column)
                    object.world.m[row][column] = row == column ? 1.0f : 0.0f;
            object.minimum = {info.bounds.minimum[0], info.bounds.minimum[1],
                              info.bounds.minimum[2]};
            object.maximum = {info.bounds.maximum[0], info.bounds.maximum[1],
                              info.bounds.maximum[2]};
            object.sphere.center = {info.sphere.center[0],
                                    info.sphere.center[1],
                                    info.sphere.center[2]};
            object.sphere.radius = info.sphere.radius;
            object.visibility.sectorId = instance.sectorIndex;
            object.visibility.mask = ~0u;
            object.visual = {instance.visualId, 1};
            object.material = {1, 1};
            object.pso = {1, 1};
            objects.push_back(object);
        }
        if (objects.size() < 2)
            return Fail(result, GameDx12J2AError12::Geometry,
                        "fewer than two immutable draw instances");

        const uint32_t instanceCount =
            static_cast<uint32_t>(objects.size());
        const uint32_t uniqueVisualCount =
            static_cast<uint32_t>(visualInfo.size());

        renderprep12::RenderSceneSnapshot12 snapshot(
            view, projection, objects, levelEpoch);
        std::vector<renderprep12::Resource12> extraResources;
        extraResources.reserve(uniqueVisuals.size());
        for (uint32_t visualId : uniqueVisuals)
            extraResources.push_back({visualId, 1});
        const Clock::time_point prepareStart = Clock::now();
        renderprep12::OrderingTimings12 serialTiming;
        renderprep12::OrderingTimings12 parallelTiming;
        renderprep12::OrderingTimings12 verifyTiming;
        const renderprep12::OrderedFrame12 serial =
            renderprep12::PrepareOrdered(snapshot, levelEpoch,
                                         renderprep12::Mode::Serial, nullptr,
                                         serialTiming, extraResources);
        const renderprep12::OrderedFrame12 parallel =
            renderprep12::PrepareOrdered(snapshot, levelEpoch,
                                         renderprep12::Mode::Parallel,
                                         &g_j2aJobs, parallelTiming,
                                         extraResources);
        const renderprep12::OrderedFrame12 verify =
            renderprep12::PrepareOrdered(snapshot, levelEpoch,
                                         renderprep12::Mode::Verify,
                                         &g_j2aJobs, verifyTiming,
                                         extraResources);
        result.prepareMs = ElapsedMs(prepareStart);

        const bool serialParallelEqual =
            renderprep12::Equivalent(serial, parallel);
        const bool verifyEqual = renderprep12::Equivalent(serial, verify);
        if (!serialParallelEqual || !verifyEqual)
            return Fail(result, GameDx12J2AError12::Prepare,
                        "Serial/Parallel/Verify mismatch");

        const renderprep12::FrameBatchPlan12 plan =
            renderprep12::BuildPacketBatchPlan(parallel, 4, 8);
        const renderprep12::BatchPlanValidation12 validation =
            renderprep12::ValidatePacketBatchPlan(parallel, plan);
        if (!validation.ok)
        {
            LogBestEffort("ph_dx12_j2a: packet plan invalid: %s",
                          validation.reason != nullptr ? validation.reason
                                                       : "unknown");
            return Fail(result, GameDx12J2AError12::Prepare,
                        "packet plan invalid");
        }

        uint64_t hash = 1469598103934665603ull;
        uint32_t visible = 0;
        for (const renderprep12::DrawPacket12& packet : parallel.frame.packets)
        {
            if (packet.visible)
                ++visible;
            hash = Fnv1a(hash, &packet.object.visual.id,
                         sizeof(packet.object.visual.id));
            hash = Fnv1a(hash, &packet.object.indexCount,
                         sizeof(packet.object.indexCount));
            hash = Fnv1a(hash, &packet.object.visibility.sectorId,
                         sizeof(packet.object.visibility.sectorId));
            HashFloat(hash, packet.worldMinimum.x);
            HashFloat(hash, packet.worldMinimum.y);
            HashFloat(hash, packet.worldMinimum.z);
            HashFloat(hash, packet.worldMaximum.x);
            HashFloat(hash, packet.worldMaximum.y);
            HashFloat(hash, packet.worldMaximum.z);
            hash = Fnv1a(hash, &packet.visible, sizeof(packet.visible));
        }

        const uint32_t packetCount =
            static_cast<uint32_t>(parallel.frame.packets.size());
        const uint32_t culledCount = instanceCount - packetCount;
        const uint32_t batchCount =
            static_cast<uint32_t>(plan.batches.size());
        const uint32_t listCount = static_cast<uint32_t>(plan.lists.size());
        const double serialMs = serialTiming.serial.total;
        const double parallelMs = parallelTiming.parallel.total;
        const double verifyMs = verifyTiming.serial.total +
                                verifyTiming.parallel.total;

        // Every potentially throwing diagnostic is completed before the
        // success transaction commits. packetIds is diagnostic-only, but its
        // reserve/insert allocations belong to the pre-commit phase.
#if defined(OGSR_J2A_CPU_FAULT_INJECTION)
        j2a_r6_fault::MaybeThrow(
            j2a_r6_fault::Point::BeforePacketIdsReserve);
#endif
        std::unordered_set<uint64_t> packetIds;
        packetIds.reserve(parallel.frame.packets.size());
        for (const renderprep12::DrawPacket12& packet :
             parallel.frame.packets)
        {
#if defined(OGSR_J2A_CPU_FAULT_INJECTION)
            j2a_r6_fault::MaybeThrow(
                j2a_r6_fault::Point::AtPacketIdsInsert);
#endif
            packetIds.insert(packet.object.id);
        }
        for (const renderprep12::Object12& object : objects)
        {
            if (packetIds.find(object.id) == packetIds.end())
            {
                LogBestEffort("ph_dx12_j2a: culled id=%llu visual=%u sector=%u "
                    "indices=%u",
                    static_cast<unsigned long long>(object.id),
                    object.visual.id, object.visibility.sectorId,
                    object.indexCount);
            }
        }

        const uint32_t logCount = std::min<uint32_t>(packetCount, 32u);
        for (uint32_t i = 0; i < logCount; ++i)
        {
            const renderprep12::DrawPacket12& packet =
                parallel.frame.packets[i];
            LogBestEffort("ph_dx12_j2a: packet[%u] id=%llu visual=%u sector=%u "
                "indices=%u visible=%d min=(%.3f,%.3f,%.3f) "
                "max=(%.3f,%.3f,%.3f)",
                i, static_cast<unsigned long long>(packet.object.id),
                packet.object.visual.id, packet.object.visibility.sectorId,
                packet.object.indexCount, packet.visible ? 1 : 0,
                packet.worldMinimum.x, packet.worldMinimum.y,
                packet.worldMinimum.z, packet.worldMaximum.x,
                packet.worldMaximum.y, packet.worldMaximum.z);
        }

#if defined(OGSR_J2A_CPU_FAULT_INJECTION)
        j2a_r6_fault::MaybeThrow(j2a_r6_fault::Point::BeforeCommit);
#endif
        LogBestEffort("ph_dx12_j2a: level='%s' session=%llu epoch=%llu sector=%u "
            "root=%u instances=%u unique=%u packets=%u visible=%u "
            "culled=%u batches=%u lists=%u serial_ms=%.3f parallel_ms=%.3f "
            "verify_ms=%.3f hash=%016llX prepared=1",
            levelName,
            static_cast<unsigned long long>(levelSession),
            static_cast<unsigned long long>(levelEpoch),
            result.sectorIndex, result.sectorRoot, instanceCount,
            uniqueVisualCount, packetCount, visible, culledCount, batchCount,
            listCount, serialMs, parallelMs, verifyMs,
            static_cast<unsigned long long>(hash));

        // Commit: only non-throwing scalar assignments follow the prepared
        // diagnostic. The single final result=PASS/FAIL line is emitted by the
        // wrapper after this function returns.
        result.instances = instanceCount;
        result.uniqueVisuals = uniqueVisualCount;
        result.packets = packetCount;
        result.visiblePackets = visible;
        result.culledPackets = culledCount;
        result.batches = batchCount;
        result.lists = listCount;
        result.stableHash = hash;
        result.serialMs = serialMs;
        result.parallelMs = parallelMs;
        result.verifyMs = verifyMs;
        result.ok = true;
        return true;
    }
    catch (const std::bad_alloc& error)
    {
        ClearSuccessMetadata(result);
        result.error = GameDx12J2AError12::OutOfMemory;
        SetReasonBestEffort(result, error.what());
        LogBestEffort("ph_dx12_j2a: exception '%s'", result.reason.c_str());
        return false;
    }
    catch (const std::exception& error)
    {
        ClearSuccessMetadata(result);
        result.error = GameDx12J2AError12::Exception;
        SetReasonBestEffort(result, error.what());
        LogBestEffort("ph_dx12_j2a: exception '%s'", result.reason.c_str());
        return false;
    }
    catch (...)
    {
        ClearSuccessMetadata(result);
        result.error = GameDx12J2AError12::Unknown;
        SetReasonBestEffort(result, "unknown exception");
        LogBestEffort("ph_dx12_j2a: unknown exception");
        return false;
    }
}
} // namespace

bool GameDx12J2A_Run(const char* levelName, uint64_t levelSession,
                     uint64_t levelEpoch,
                     const DirectX::XMFLOAT4X4& view,
                     const DirectX::XMFLOAT4X4& projection,
                     GameDx12J2AResult12& result) noexcept
{
    const Clock::time_point start = Clock::now();
    bool ok = false;
    try
    {
        ok = RunJ2A(levelName, levelSession, levelEpoch, view, projection,
                    result);
    }
    catch (const std::bad_alloc& error)
    {
        ClearSuccessMetadata(result);
        result.error = GameDx12J2AError12::OutOfMemory;
        SetReasonBestEffort(result, error.what());
        LogBestEffort("ph_dx12_j2a: exception '%s'", result.reason.c_str());
        ok = false;
    }
    catch (const std::exception& error)
    {
        ClearSuccessMetadata(result);
        result.error = GameDx12J2AError12::Exception;
        SetReasonBestEffort(result, error.what());
        LogBestEffort("ph_dx12_j2a: exception '%s'", result.reason.c_str());
        ok = false;
    }
    catch (...)
    {
        ClearSuccessMetadata(result);
        result.error = GameDx12J2AError12::Unknown;
        SetReasonBestEffort(result, "unknown exception");
        LogBestEffort("ph_dx12_j2a: unknown exception");
        ok = false;
    }

    if (!ok || !result.ok)
    {
        ClearSuccessMetadata(result);
        if (result.error == GameDx12J2AError12::None)
            result.error = GameDx12J2AError12::Exception;
        ok = false;
    }

    result.totalMs = ElapsedMs(start);
    LogBestEffort("ph_dx12_j2a: timing vfs_ms=%.3f parse_ms=%.3f lookup_ms=%.3f "
        "collect_ms=%.3f readvisual_ms=%.3f prepare_ms=%.3f total_ms=%.3f "
        "sector=%u root=%u instances=%u unique=%u packets=%u visible=%u "
        "culled=%u batches=%u lists=%u hash=%016llX "
        "lookup_nodes=%llu lookup_edges=%llu collect_nodes=%llu "
        "collect_edges=%llu node_limit=%u edge_limit=%u instance_limit=%u "
        "result=%s",
        result.vfsMs, result.parseMs, result.lookupMs, result.collectMs,
        result.readVisualMs, result.prepareMs, result.totalMs,
        result.sectorIndex, result.sectorRoot, result.instances,
        result.uniqueVisuals, result.packets, result.visiblePackets,
        result.culledPackets, result.batches, result.lists,
        static_cast<unsigned long long>(result.stableHash),
        static_cast<unsigned long long>(result.lookupNodeVisits),
        static_cast<unsigned long long>(result.lookupEdgeVisits),
        static_cast<unsigned long long>(result.collectNodeVisits),
        static_cast<unsigned long long>(result.collectEdgeVisits),
        result.collectMaxNodeVisits, result.collectMaxEdgeVisits,
        result.collectMaxInstances, ok ? "PASS" : "FAIL");
    return ok;
}

bool GameDx12J2A_RunForBridge(const char* levelName, uint64_t levelSession,
                              uint64_t levelEpoch,
                              const DirectX::XMFLOAT4X4& view,
                              const DirectX::XMFLOAT4X4& projection,
                              GameDx12J2AResult12& result) noexcept
{
    bool ok = false;
    try
    {
        ok = GameDx12J2A_Run(levelName, levelSession, levelEpoch, view,
                             projection, result);
    }
    catch (const std::bad_alloc& error)
    {
        ClearSuccessMetadata(result);
        result.error = GameDx12J2AError12::OutOfMemory;
        SetReasonBestEffort(result, error.what());
        ok = false;
    }
    catch (...)
    {
        ClearSuccessMetadata(result);
        result.error = GameDx12J2AError12::Unknown;
        SetReasonBestEffort(result, "unknown exception");
        ok = false;
    }

    if (!ok || !result.ok)
    {
        ClearSuccessMetadata(result);
        if (result.error == GameDx12J2AError12::None)
            result.error = GameDx12J2AError12::Exception;
        LogBestEffort(
            "ph_dx12_j2a: failure_detail sector=%u code=%u reason='%s'",
            result.sectorIndex, static_cast<unsigned>(result.error),
            result.reason.empty() ? "unknown failure"
                                  : result.reason.c_str());
        return false;
    }
    return true;
}

void GameDx12J2A_Shutdown()
{
    if (g_j2aJobsReady)
    {
        g_j2aJobs.Shutdown();
        g_j2aJobsReady = false;
    }
}

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
