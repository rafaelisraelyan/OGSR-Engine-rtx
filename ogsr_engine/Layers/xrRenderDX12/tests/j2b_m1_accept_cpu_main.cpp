#include "dx12j2bbatch.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
int g_failed = 0;

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
    std::printf("ACCEPT_CASE %-40s status=%s\n", name,
                condition ? "PASS" : "FAIL");
    if (!condition)
        ++g_failed;
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
    j2bbatch12::BatchResult12 batch;
    const bool ok = j2bbatch12::PrepareBatch(
        snapshot, snapshot.identity, 129, 17431, visuals, batch,
        j2bbatch12::Limits12{});
    Check("prepare batch", ok && batch.ok);

    const uint64_t originalHash = batch.stableHash;
    Check("accept live session/epoch",
          j2bbatch12::AcceptBatch12(batch, snapshot.identity) ==
              j2bbatch12::AcceptStatus12::Accepted);
    Check("reject stale session",
          j2bbatch12::AcceptBatch12(batch, {3, 3}) ==
              j2bbatch12::AcceptStatus12::StaleSession);
    Check("reject stale epoch",
          j2bbatch12::AcceptBatch12(batch, {2, 4}) ==
              j2bbatch12::AcceptStatus12::StaleEpoch);
    Check("reject zero live token",
          j2bbatch12::AcceptBatch12(batch, {0, 0}) ==
              j2bbatch12::AcceptStatus12::NotReady);

    j2bbatch12::BatchResult12 failed = batch;
    failed.ok = false;
    failed.stableHash = 0;
    Check("reject failed result",
          j2bbatch12::AcceptBatch12(failed, snapshot.identity) ==
              j2bbatch12::AcceptStatus12::NotReady);

    j2bbatch12::BatchResult12 invalid = batch;
    invalid.slots[0].valid = false;
    Check("reject invalid slot set",
          j2bbatch12::AcceptBatch12(invalid, snapshot.identity) ==
              j2bbatch12::AcceptStatus12::InvalidResult);

    Check("acceptance does not mutate result",
          batch.stableHash == originalHash && batch.slots[0].valid &&
              batch.slots[1].valid && batch.slots[2].valid &&
              batch.slots[3].valid);

    std::printf("M1_ACCEPT_RESULT %s failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_failed);
    return g_failed == 0 ? 0 : 1;
}
