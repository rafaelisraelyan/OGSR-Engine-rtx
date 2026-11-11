#include "dx12j2bbatch.h"
#include "j2b_m1_fault.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
int g_passed = 0;
int g_failed = 0;
int g_faultPoint = -1;
bool g_throwRuntime = false;
bool g_skipSubmit = false;
uint32_t g_skipIndex = 0;
bool g_rejectSubmit = false;
uint32_t g_rejectIndex = 0;

} // namespace

namespace j2b_m1_fault
{
void MaybeThrow(Point point)
{
    if (g_faultPoint != static_cast<int>(point))
        return;
    if (g_throwRuntime)
        throw std::runtime_error("injected worker exception");
    throw std::bad_alloc();
}

bool SkipSubmit(uint32_t index)
{
    return g_skipSubmit && index == g_skipIndex;
}

bool RejectSubmit(uint32_t index)
{
    return g_rejectSubmit && index == g_rejectIndex;
}
} // namespace j2b_m1_fault

namespace
{

bool ReadFileBytes(const char* path, std::vector<uint8_t>& bytes,
                   size_t maxBytes, std::string& reason)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        reason = "cannot open file";
        return false;
    }
    const std::streamoff length = file.tellg();
    if (length <= 0 || static_cast<uint64_t>(length) > maxBytes)
    {
        reason = "invalid file size";
        return false;
    }
    bytes.resize(static_cast<size_t>(length));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (file.gcount() != length || file.bad())
    {
        bytes.clear();
        reason = "incomplete file read";
        return false;
    }
    return true;
}

void Check(const char* name, bool condition)
{
    std::printf("BATCH_CASE %-34s status=%s\n", name,
                condition ? "PASS" : "FAIL");
    if (condition)
        ++g_passed;
    else
        ++g_failed;
}

bool AllSlotsEmpty(const j2bbatch12::BatchResult12& result)
{
    for (const auto& slot : result.slots)
        if (slot.valid || !slot.asset.vertices.empty() ||
            !slot.asset.indices.empty())
            return false;
    return true;
}

j2bbatch12::Snapshot12 MakeSnapshot(const std::vector<uint8_t>& level,
                                    const std::vector<uint8_t>& geom)
{
    j2bbatch12::Snapshot12 snapshot;
    snapshot.levelBytes = level;
    snapshot.geomBytes = geom;
    snapshot.levelName = "jupiter";
    snapshot.identity = {2, 3};
    snapshot.levelHash = j2bbatch12::HashBytes(
        1469598103934665603ull, level.data(), level.size());
    snapshot.geomHash = j2bbatch12::HashBytes(
        1469598103934665603ull, geom.data(), geom.size());
    return snapshot;
}

bool VerifyExpectedSlot(const j2bbatch12::Slot12& slot, uint32_t visualId,
                        uint32_t vbId, uint32_t vBase, uint32_t vCount,
                        uint32_t ibId, uint32_t iBase, uint32_t iCount)
{
    return slot.valid && slot.readOk && slot.converted && slot.completed &&
           slot.source.visualId == visualId && slot.source.sourceVbId == vbId &&
           slot.source.sourceVBase == vBase &&
           slot.source.sourceVCount == vCount &&
           slot.source.sourceIbId == ibId &&
           slot.source.sourceIBase == iBase &&
           slot.source.sourceICount == iCount &&
           slot.instance.sectorId == 129 &&
           slot.instance.rootVisualId == 17431 &&
           slot.instance.visualId == visualId && slot.vertexCount == vCount &&
           slot.indexCount == iCount;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: %s <level> <level.geom>\n", argv[0]);
        return 2;
    }
    std::vector<uint8_t> level;
    std::vector<uint8_t> geom;
    std::string reason;
    if (!ReadFileBytes(argv[1], level, 16ull * 1024ull * 1024ull, reason) ||
        !ReadFileBytes(argv[2], geom, 128ull * 1024ull * 1024ull, reason))
    {
        std::printf("INPUT_READ=FAIL reason='%s'\n", reason.c_str());
        return 1;
    }
    const j2bbatch12::Snapshot12 snapshot = MakeSnapshot(level, geom);
    const std::array<uint32_t, 4> visuals = {9, 105, 106, 107};

    j2bbatch12::BatchResult12 serial;
    const bool serialOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, serial,
        j2bbatch12::Limits12{});
    std::printf("M1_BATCH_SERIAL ok=%d hash=%016llX reason='%s'\n",
                serialOk ? 1 : 0,
                static_cast<unsigned long long>(serial.stableHash),
                serial.reason.c_str());
    Check("serial all-four ok", serialOk && serial.ok);
    Check("serial slot 9 source identity",
          VerifyExpectedSlot(serial.slots[0], 9, 9, 42158, 545, 4, 782316,
                             1314));
    Check("serial slot 105 source identity",
          VerifyExpectedSlot(serial.slots[1], 105, 24, 22188, 99, 7, 81021,
                             312));
    Check("serial slot 106 source identity",
          VerifyExpectedSlot(serial.slots[2], 106, 15, 28516, 120, 5, 221727,
                             192));
    Check("serial slot 107 source identity",
          VerifyExpectedSlot(serial.slots[3], 107, 15, 42683, 385, 5, 267702,
                             876));
    Check("serial stable hash nonzero", serial.stableHash != 0);

    gr::jobs::JobSystem jobs;
    gr::jobs::JobConfig config;
    config.backgroundWorkers = 2;
    config.automaticWorkers = false;
    config.maxAutoBackgroundWorkers = 4;
    config.defaultGrainSize = 1;
    config.enableStats = true;
    const bool jobsReady = jobs.Initialize(config);
    Check("GR JobSystem initialize", jobsReady);

    j2bbatch12::BatchResult12 parallel;
    const bool parallelOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, &jobs, true,
        parallel, j2bbatch12::Limits12{});
    std::printf("M1_BATCH_PARALLEL ok=%d hash=%016llX reason='%s'\n",
                parallelOk ? 1 : 0,
                static_cast<unsigned long long>(parallel.stableHash),
                parallel.reason.c_str());
    Check("parallel all-four ok", parallelOk && parallel.ok);
    Check("parallel hash equals serial",
          parallel.stableHash == serial.stableHash);
    bool slotHashesEqual = true;
    for (size_t i = 0; i < serial.slots.size(); ++i)
    {
        slotHashesEqual &=
            serial.slots[i].source == parallel.slots[i].source &&
            serial.slots[i].instance == parallel.slots[i].instance &&
            serial.slots[i].assetVbHash == parallel.slots[i].assetVbHash &&
            serial.slots[i].assetIbHash == parallel.slots[i].assetIbHash;
    }
    Check("parallel slot identities equal serial", slotHashesEqual);

    jobs.Shutdown();
    j2bbatch12::BatchResult12 uninitialized;
    const bool uninitializedOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, &jobs, true,
        uninitialized, j2bbatch12::Limits12{});
    Check("uninitialized GR falls back serial",
          uninitializedOk && uninitialized.ok &&
              uninitialized.stableHash == serial.stableHash);

    jobs.Initialize(config);
    g_skipSubmit = true;
    g_skipIndex = 2;
    j2bbatch12::BatchResult12 partial;
    const bool partialOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, &jobs, true, partial,
        j2bbatch12::Limits12{});
    g_skipSubmit = false;
    Check("partial submit rejects batch",
          !partialOk && !partial.ok && AllSlotsEmpty(partial));

    j2bbatch12::BatchResult12 afterPartial;
    const bool afterPartialOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, afterPartial,
        j2bbatch12::Limits12{});
    Check("drain/reuse after partial submit",
          afterPartialOk && afterPartial.ok &&
              afterPartial.stableHash == serial.stableHash);

    g_rejectSubmit = true;
    g_rejectIndex = 1;
    j2bbatch12::BatchResult12 submitRejected;
    const bool submitRejectedOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, &jobs, true,
        submitRejected, j2bbatch12::Limits12{});
    g_rejectSubmit = false;
    Check("submit rejection rejects batch",
          !submitRejectedOk && !submitRejected.ok &&
              AllSlotsEmpty(submitRejected));

    j2bbatch12::BatchResult12 afterSubmitReject;
    const bool afterSubmitRejectOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, afterSubmitReject,
        j2bbatch12::Limits12{});
    Check("drain/reuse after submit rejection",
          afterSubmitRejectOk && afterSubmitReject.ok &&
              afterSubmitReject.stableHash == serial.stableHash);

    g_faultPoint = static_cast<int>(j2b_m1_fault::Point::AfterRead);
    g_throwRuntime = true;
    j2bbatch12::BatchResult12 workerException;
    const bool workerExceptionOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, &jobs, true,
        workerException, j2bbatch12::Limits12{});
    g_faultPoint = -1;
    g_throwRuntime = false;
    Check("worker exception rejects batch",
          !workerExceptionOk && !workerException.ok &&
              AllSlotsEmpty(workerException));

    j2bbatch12::BatchResult12 afterException;
    const bool afterExceptionOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, afterException,
        j2bbatch12::Limits12{});
    Check("drain/reuse after worker exception",
          afterExceptionOk && afterException.ok &&
              afterException.stableHash == serial.stableHash);

    g_faultPoint = static_cast<int>(j2b_m1_fault::Point::BeforePublish);
    j2bbatch12::BatchResult12 lateOom;
    const bool lateOomOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, &jobs, true, lateOom,
        j2bbatch12::Limits12{});
    g_faultPoint = -1;
    Check("late allocation failure rejects batch",
          !lateOomOk && !lateOom.ok && AllSlotsEmpty(lateOom));

    j2bbatch12::BatchResult12 afterOom;
    const bool afterOomOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, afterOom,
        j2bbatch12::Limits12{});
    Check("drain/reuse after late allocation failure",
          afterOomOk && afterOom.ok &&
              afterOom.stableHash == serial.stableHash);
    jobs.Shutdown();

    j2bbatch12::BatchResult12 missing;
    const std::array<uint32_t, 4> missingVisuals = {9, 105, 106, 999};
    const bool missingOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, missingVisuals, missing,
        j2bbatch12::Limits12{});
    Check("missing visual rejects batch",
          !missingOk && !missing.ok && AllSlotsEmpty(missing));

    j2bbatch12::BatchResult12 wrongIdentity;
    const bool identityOk = j2bbatch12::PrepareBatch(
        snapshot, {3, 3}, 129, 17431, visuals, wrongIdentity,
        j2bbatch12::Limits12{});
    Check("session/epoch mismatch rejects batch",
          !identityOk && !wrongIdentity.ok && AllSlotsEmpty(wrongIdentity));

    j2bbatch12::Limits12 tiny;
    tiny.maxTotalAssetBytes = 1;
    j2bbatch12::BatchResult12 tooLarge;
    const bool sizeOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, tooLarge, tiny);
    Check("asset byte limit rejects batch",
          !sizeOk && !tooLarge.ok && AllSlotsEmpty(tooLarge));

    j2bbatch12::BatchResult12 duplicate;
    const std::array<uint32_t, 4> duplicateVisuals = {9, 9, 106, 107};
    const bool duplicateOk = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, duplicateVisuals, duplicate,
        j2bbatch12::Limits12{});
    Check("duplicate visual rejects batch",
          !duplicateOk && !duplicate.ok && AllSlotsEmpty(duplicate));

    std::printf("M1_BATCH_CPU_RESULT %s passed=%d failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
