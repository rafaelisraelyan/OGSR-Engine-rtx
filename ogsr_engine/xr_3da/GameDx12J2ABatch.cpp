#include "stdafx.h"
#include "GameDx12J2ABatch.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "FS.h"
#include "LocatorAPI_defs.h"

#include "dx12scopeguard.h"

#include <cstring>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
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
} // namespace

bool GameDx12J2ABatch_ReadSnapshot(const char* levelName,
                                  const j2bbatch12::Identity12& identity,
                                  j2bbatch12::Snapshot12& snapshot,
                                  const j2bbatch12::Limits12& limits,
                                  std::string& reason) noexcept
{
    snapshot = {};
    try
    {
        if (levelName == nullptr || levelName[0] == '\0' ||
            identity.levelSession == 0 || identity.levelEpoch == 0)
        {
            reason = "invalid snapshot identity";
            return false;
        }

        // The game sets $level$ during level load. Setting it here keeps the
        // adapter reproducible for owner-thread test harnesses and is
        // idempotent with the current level name.
        char folder[260] = {};
        std::strncpy(folder, levelName, sizeof(folder) - 1);
        FS.get_path(fsgame::level)->_set(folder);

        j2bbatch12::Snapshot12 candidate;
        candidate.levelName = levelName;
        candidate.identity = identity;
        if (!ReadVfsFile(fsgame::level_files::level, limits.maxLevelBytes,
                         candidate.levelBytes, reason) ||
            !ReadVfsFile(fsgame::level_files::level_geom,
                         limits.maxGeomBytes, candidate.geomBytes, reason))
        {
            snapshot = {};
            return false;
        }
        candidate.levelHash = j2bbatch12::HashBytes(
            1469598103934665603ull, candidate.levelBytes.data(),
            candidate.levelBytes.size());
        candidate.geomHash = j2bbatch12::HashBytes(
            1469598103934665603ull, candidate.geomBytes.data(),
            candidate.geomBytes.size());
        snapshot = std::move(candidate);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        reason = "snapshot allocation failed";
    }
    catch (const std::exception&)
    {
        reason = "snapshot exception";
    }
    catch (...)
    {
        reason = "snapshot unknown exception";
    }
    snapshot = {};
    return false;
}

bool GameDx12J2ABatch_PrepareFromVfs(
    const char* levelName, const j2bbatch12::Identity12& identity,
    uint32_t sectorId, uint32_t rootVisualId,
    const std::array<uint32_t, j2bbatch12::kTargetVisualCount>& visuals,
    j2bbatch12::BatchResult12& result,
    const j2bbatch12::Limits12& limits) noexcept
{
    j2bbatch12::Snapshot12 snapshot;
    std::string reason;
    if (!GameDx12J2ABatch_ReadSnapshot(levelName, identity, snapshot, limits,
                                       reason))
    {
        result = {};
        result.identity = identity;
        result.reason = reason;
        return false;
    }
    return j2bbatch12::PrepareBatch(snapshot, identity, sectorId, rootVisualId,
                                    visuals, result, limits);
}

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
