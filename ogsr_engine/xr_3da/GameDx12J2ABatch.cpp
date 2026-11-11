#include "stdafx.h"
#include "GameDx12J2ABatch.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "FS.h"
#include "LocatorAPI_defs.h"

#include "dx12scopeguard.h"

#include <chrono>
#include <cstring>
#include <new>
#include <stdexcept>

#if defined(OGSR_J2B_CPU_FAULT_INJECTION)
#include "j2b_m1_fault.h"
#endif

namespace
{
using Clock = std::chrono::steady_clock;

constexpr size_t kMaxLevelName = 260;

double Ms(Clock::time_point start) noexcept
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start)
        .count();
}

void CloseReaderNoThrow(IReader*& reader) noexcept
{
    if (reader == nullptr)
        return;
    IReader* current = reader;
    reader = nullptr;
    try
    {
        FS.r_close(current);
    }
    catch (...)
    {
    }
}

bool CopyLevelName(char* dest, size_t capacity, const char* source) noexcept
{
    if (dest == nullptr || capacity == 0 || source == nullptr)
        return false;
    size_t i = 0;
    for (; i + 1 < capacity && source[i] != '\0'; ++i)
        dest[i] = source[i];
    dest[i] = '\0';
    return source[i] == '\0';
}

bool ReadVfsFile(const char* label, const char* name, uint64_t maxBytes,
                 std::vector<uint8_t>& bytes,
                 j2bbatch12::ErrorInfo12& error, double& openMs,
                 double& copyMs) noexcept
{
    const Clock::time_point openStart = Clock::now();
    IReader* reader = FS.r_open(fsgame::level, name);
    openMs += Ms(openStart);
    if (reader == nullptr)
    {
        j2bbatch12::SetErrorInfo2(error, j2bbatch12::Error12::VfsOpen,
                                  "VFS open failed: ", label);
        return false;
    }
    auto closeReader = dx12scope::MakeScopeGuard(
        [&reader]() noexcept { CloseReaderNoThrow(reader); });
    (void)closeReader;

    const size_t size = reader->length();
    if (size == 0 || static_cast<uint64_t>(size) > maxBytes)
    {
        j2bbatch12::SetErrorInfo2(error, j2bbatch12::Error12::VfsSize,
                                  "VFS size rejected: ", label);
        return false;
    }

    const Clock::time_point copyStart = Clock::now();
    try
    {
#if defined(OGSR_J2B_CPU_FAULT_INJECTION)
        j2b_m1_fault::MaybeEnablePersistentOom(
            j2b_m1_fault::Point::BeforeVfsCopy);
#endif
        bytes.resize(size);
    }
    catch (const std::bad_alloc&)
    {
        j2bbatch12::SetErrorInfo2(error,
                                  j2bbatch12::Error12::VfsAllocation,
                                  "VFS allocation failed: ", label);
        return false;
    }
    catch (const std::length_error&)
    {
        j2bbatch12::SetErrorInfo2(error,
                                  j2bbatch12::Error12::VfsAllocation,
                                  "VFS allocation failed: ", label);
        return false;
    }
    std::memcpy(bytes.data(), reader->begin(), size);
    copyMs += Ms(copyStart);
    return true;
}
} // namespace

extern bool OnMainThread();

bool GameDx12J2ABatch_ReadSnapshot(const char* levelName,
                                  const j2bbatch12::Identity12& identity,
                                  j2bbatch12::Snapshot12& snapshot,
                                  const j2bbatch12::Limits12& limits,
                                  j2bbatch12::ErrorInfo12& error) noexcept
{
    snapshot = {};
    j2bbatch12::SetErrorInfo(error, j2bbatch12::Error12::None, "");
    try
    {
        if (!OnMainThread())
        {
            j2bbatch12::SetErrorInfo(
                error, j2bbatch12::Error12::OwnerThreadViolation,
                "J2B batch must run on the owner thread");
            return false;
        }
        if (levelName == nullptr || levelName[0] == '\0' ||
            identity.levelSession == 0 || identity.levelEpoch == 0)
        {
            j2bbatch12::SetErrorInfo(error,
                                     j2bbatch12::Error12::InvalidIdentity,
                                     "invalid snapshot identity");
            return false;
        }
        if (std::strlen(levelName) >= kMaxLevelName)
        {
            j2bbatch12::SetErrorInfo(
                error, j2bbatch12::Error12::LevelNameTooLong,
                "level name exceeds fixed capacity");
            return false;
        }

        FS_Path* levelPath = FS.get_path(fsgame::level);
        if (levelPath == nullptr || levelPath->m_Add == nullptr)
        {
            j2bbatch12::SetErrorInfo(
                error, j2bbatch12::Error12::LevelMismatch,
                "live level path is not available");
            return false;
        }
        if (_stricmp(levelPath->m_Add, levelName) != 0)
        {
            j2bbatch12::SetErrorInfo2(
                error, j2bbatch12::Error12::LevelMismatch,
                "requested level is not live: ", levelPath->m_Add);
            return false;
        }

        char liveBefore[kMaxLevelName] = {};
        if (!CopyLevelName(liveBefore, sizeof(liveBefore), levelPath->m_Add))
        {
            j2bbatch12::SetErrorInfo(
                error, j2bbatch12::Error12::LevelNameTooLong,
                "live level name exceeds fixed capacity");
            return false;
        }

        j2bbatch12::Snapshot12 candidate;
        candidate.levelName = levelName;
        candidate.identity = identity;
        double openLevelMs = 0.0;
        double openGeomMs = 0.0;
        double copyLevelMs = 0.0;
        double copyGeomMs = 0.0;
        if (!ReadVfsFile("level", fsgame::level_files::level,
                         limits.maxLevelBytes, candidate.levelBytes, error,
                         openLevelMs, copyLevelMs) ||
            !ReadVfsFile("level.geom", fsgame::level_files::level_geom,
                         limits.maxGeomBytes, candidate.geomBytes, error,
                         openGeomMs, copyGeomMs))
        {
            snapshot = {};
            return false;
        }

        if (_stricmp(levelPath->m_Add, liveBefore) != 0)
        {
            j2bbatch12::SetErrorInfo(
                error, j2bbatch12::Error12::LevelMismatch,
                "level changed during snapshot read");
            snapshot = {};
            return false;
        }

        candidate.vfsOpenMs = openLevelMs + openGeomMs;
        candidate.vfsCopyMs = copyLevelMs + copyGeomMs;
        const Clock::time_point hashStart = Clock::now();
        candidate.levelHash = j2bbatch12::HashBytes(
            1469598103934665603ull, candidate.levelBytes.data(),
            candidate.levelBytes.size());
        candidate.geomHash = j2bbatch12::HashBytes(
            1469598103934665603ull, candidate.geomBytes.data(),
            candidate.geomBytes.size());
        candidate.snapshotHashMs = Ms(hashStart);
        snapshot = std::move(candidate);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        j2bbatch12::SetErrorInfo(error,
                                 j2bbatch12::Error12::AllocationFailure,
                                 "snapshot allocation failed");
    }
    catch (const std::exception&)
    {
        j2bbatch12::SetErrorInfo(error, j2bbatch12::Error12::Exception,
                                 "snapshot exception");
    }
    catch (...)
    {
        j2bbatch12::SetErrorInfo(error, j2bbatch12::Error12::Unknown,
                                 "snapshot unknown exception");
    }
    snapshot = {};
    return false;
}

bool GameDx12J2ABatch_PrepareFromVfs(
    const char* levelName, const j2bbatch12::Identity12& identity,
    uint32_t sectorId, uint32_t rootVisualId,
    const std::array<uint32_t, j2bbatch12::kTargetVisualCount>& visuals,
    gr::jobs::JobSystem* jobs, bool parallel,
    j2bbatch12::BatchResult12& result,
    const j2bbatch12::Limits12& limits) noexcept
{
    j2bbatch12::Snapshot12 snapshot;
    j2bbatch12::ErrorInfo12 error;
    if (!GameDx12J2ABatch_ReadSnapshot(levelName, identity, snapshot, limits,
                                       error))
    {
        result = {};
        result.identity = identity;
        result.error = error;
        return false;
    }
    return j2bbatch12::PrepareBatch(snapshot, identity, sectorId, rootVisualId,
                                    visuals, jobs, parallel, result, limits);
}

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
