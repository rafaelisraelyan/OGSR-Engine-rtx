#include "stdafx.h"
#include "GameDx12J2ABatch.h"

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "FS.h"
#include "LocatorAPI_defs.h"

#include "dx12scopeguard.h"

#include <algorithm>
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

size_t LevelPathLength(const char* path) noexcept
{
    if (path == nullptr)
        return 0;
    size_t length = std::strlen(path);
    while (length > 0 &&
           (path[length - 1] == '\\' || path[length - 1] == '/'))
        --length;
    return length;
}

bool SameLevelPath(const char* left, const char* right) noexcept
{
    if (left == nullptr || right == nullptr)
        return false;
    const size_t leftLength = LevelPathLength(left);
    const size_t rightLength = LevelPathLength(right);
    return leftLength == rightLength &&
           _strnicmp(left, right, leftLength) == 0;
}

bool ReadVfsFile(const char* label, const char* name, uint64_t maxBytes,
                 std::vector<uint8_t>& bytes,
                 j2bbatch12::ErrorInfo12& error, double& openMs,
                 double& copyMs, const char* alias = fsgame::level) noexcept
{
    try
    {
        const Clock::time_point openStart = Clock::now();
        IReader* reader = FS.r_open(alias, name);
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
    catch (const std::bad_alloc&)
    {
        j2bbatch12::SetErrorInfo2(error,
                                  j2bbatch12::Error12::VfsAllocation,
                                  "VFS open allocation failed: ", label);
    }
    catch (const std::exception&)
    {
        j2bbatch12::SetErrorInfo2(error, j2bbatch12::Error12::VfsOpen,
                                  "VFS open exception: ", label);
    }
    catch (...)
    {
        j2bbatch12::SetErrorInfo2(error, j2bbatch12::Error12::VfsOpen,
                                  "VFS open unknown exception: ", label);
    }
    return false;
}

bool BinaryAlphaFromThm(const std::vector<uint8_t>& bytes,
                        const XrayTextureData12& dds) noexcept
{
    const auto word = [&](size_t offset)
    {
        uint32_t value;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };
    for (size_t offset = 0; bytes.size() - offset >= 8;)
    {
        const uint32_t chunk = word(offset);
        const uint32_t size = word(offset + 4);
        offset += 8;
        if (size > bytes.size() - offset)
            return false;
        if (chunk == 0x812 && size >= 32)
            return word(offset + 24) == dds.width && word(offset + 28) == dds.height &&
                   (word(offset) == 1 || (word(offset + 4) & 2) != 0);
        offset += size;
    }
    return false;
}
} // namespace

extern bool OnMainThread();

void GameDx12J2ABatch_ReadTextures(
    const j2bbatch12::SceneBatch12& scene,
    std::vector<GameDx12SceneTexture12>& textures) noexcept
{
    textures.clear();
    constexpr size_t maxTextureBytes = 4u * 1024u * 1024u;
    constexpr size_t maxSceneBytes = 16u * 1024u * 1024u;
    size_t totalBytes = 0;
    try
    {
        if (!OnMainThread())
            return;
        for (const auto& source : scene.sources)
        {
            const auto& name = source.asset.textureName;
            if (name.empty() || name.size() > 180 || name.front() == '\\' ||
                name.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.\\") != std::string::npos ||
                name.find("..") != std::string::npos)
                continue;
            if (std::any_of(textures.begin(), textures.end(), [&](const auto& texture)
                { return _stricmp(texture.name.c_str(), name.c_str()) == 0; }))
                continue;

            auto& entry = textures.emplace_back();
            entry.name = name;
            const std::string ddsName = name + ".dds";
            double openMs = 0.0;
            double copyMs = 0.0;
            j2bbatch12::ErrorInfo12 error;
            std::vector<uint8_t> bytes;
            const char* reason = "texture budget exhausted";
            const size_t limit = (std::min)(maxTextureBytes, maxSceneBytes - totalBytes);
            if (limit == 0 ||
                (!ReadVfsFile("DDS", ddsName.c_str(), limit, bytes, error, openMs, copyMs) &&
                 !ReadVfsFile("DDS", ddsName.c_str(), limit, bytes, error, openMs, copyMs,
                              fsgame::game_textures)) ||
                !DecodeXrayDds12(bytes, entry.dds, &reason))
            {
                entry.dds = {};
                Msg("ph_dx12_b2: texture fallback name='%s' reason='%s'",
                    name.c_str(), error.code != j2bbatch12::Error12::None
                        ? error.text : reason);
                continue;
            }
            totalBytes += entry.dds.data.size();
            const std::string thmName = name + ".thm";
            std::vector<uint8_t> thm;
            if (ReadVfsFile("THM", thmName.c_str(), 16384, thm, error, openMs, copyMs,
                            fsgame::game_textures))
                entry.alphaTest = BinaryAlphaFromThm(thm, entry.dds);
            Msg("ph_dx12_b2: texture name='%s' bc1_srgb=%ux%u mips=%u alpha_thm=%d",
                name.c_str(), entry.dds.width, entry.dds.height, entry.dds.mipCount,
                entry.alphaTest ? 1 : 0);
        }
    }
    catch (...)
    {
        textures.clear();
    }
}

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
        if (!SameLevelPath(levelPath->m_Add, levelName))
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

        if (!SameLevelPath(levelPath->m_Add, liveBefore))
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
