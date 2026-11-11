#pragma once

#include "dx12levelhierarchy.h"
#include "dx12xraylevelasset.h"
#include "dx12xraylevelgeometry.h"

#include "gr/jobs/JobSystem.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// F4.0-J2B-M1-R1: CPU-only, transactional four-visual batch.
//
// Failure contract:
//   - every public entry point is noexcept;
//   - failure status and error code are always available without allocation;
//   - error text is best-effort and fixed-capacity, never required for status;
//   - a failed call clears all published assets and the stable hash;
//   - a subsequent call on the same owner thread can succeed normally.
//
// The batch owns only CPU data. It never creates D3D12 resources and never
// publishes a partial result: all four requested visuals must read and convert
// successfully, or the batch is rejected.
namespace j2bbatch12
{
constexpr uint32_t kTargetVisualCount = 4;
constexpr size_t kErrorTextCapacity = 192;

enum class Error12 : uint32_t
{
    None = 0,
    InvalidIdentity,
    EmptyInput,
    InputLimit,
    DuplicateVisual,
    HierarchyParse,
    SectorCollect,
    MissingVisual,
    RootMismatch,
    ReadVisual,
    Convert,
    AssetLimit,
    TotalAssetLimit,
    WorkerAllocation,
    WorkerException,
    SubmitException,
    AllocationFailure,
    Exception,
    Unknown,
    LevelMismatch,
    OwnerThreadViolation,
    LevelNameTooLong,
    VfsOpen,
    VfsSize,
    VfsRead,
    VfsAllocation,
};

struct ErrorInfo12
{
    Error12 code = Error12::None;
    char text[kErrorTextCapacity] = {};
};

void SetErrorInfo(ErrorInfo12& info, Error12 code, const char* text) noexcept;
void SetErrorInfo2(ErrorInfo12& info, Error12 code, const char* first,
                   const char* second) noexcept;

struct Identity12
{
    uint64_t levelSession = 0;
    uint64_t levelEpoch = 0;

    bool operator==(const Identity12&) const = default;
};

struct Snapshot12
{
    // Owns immutable copies of the VFS bytes. No reader/engine pointer is
    // retained after construction.
    std::vector<uint8_t> levelBytes;
    std::vector<uint8_t> geomBytes;
    std::string levelName;
    Identity12 identity;
    uint64_t levelHash = 0;
    uint64_t geomHash = 0;
    double vfsOpenMs = 0.0;
    double vfsCopyMs = 0.0;
    double snapshotHashMs = 0.0;
};

struct Limits12
{
    uint64_t maxLevelBytes = 16ull * 1024ull * 1024ull;
    uint64_t maxGeomBytes = 128ull * 1024ull * 1024ull;
    uint64_t maxAssetBytes = 1ull * 1024ull * 1024ull;
    uint64_t maxTotalAssetBytes = 4ull * 1024ull * 1024ull;
    uint32_t maxNodeVisits = 200000;
    uint32_t maxEdgeVisits = 400000;
    uint32_t maxInstances = 64;
};

struct SourceKey12
{
    uint64_t levelSession = 0;
    uint32_t visualId = 0;
    uint32_t sourceVbId = 0;
    uint32_t sourceVBase = 0;
    uint32_t sourceVCount = 0;
    uint32_t sourceIbId = 0;
    uint32_t sourceIBase = 0;
    uint32_t sourceICount = 0;
    uint64_t sourceVertexByteOffset = 0;
    uint64_t sourceIndexByteOffset = 0;
    uint64_t compactVbHash = 0;
    uint64_t compactIbHash = 0;

    bool operator==(const SourceKey12&) const = default;
};

struct InstanceKey12
{
    uint32_t sectorId = 0;
    uint32_t rootVisualId = 0;
    uint32_t occurrenceOrdinal = 0;
    uint32_t visualId = 0;

    bool operator==(const InstanceKey12&) const = default;
};

struct Slot12
{
    bool valid = false;
    SourceKey12 source{};
    InstanceKey12 instance{};
    XrayAssetData12 asset{};
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint64_t compactVbHash = 0;
    uint64_t compactIbHash = 0;
    uint64_t assetVbHash = 0;
    uint64_t assetIbHash = 0;
    xraylevel12::ReadError readError = xraylevel12::ReadError::None;
    bool readOk = false;
    bool converted = false;
    bool completed = false;
    double readMs = 0.0;
    double convertMs = 0.0;
    ErrorInfo12 error{};
};

struct BatchResult12
{
    bool ok = false;
    Identity12 identity{};
    uint32_t sectorId = 0;
    uint32_t rootVisualId = 0;
    uint32_t instances = 0;
    std::array<Slot12, kTargetVisualCount> slots{};
    uint64_t stableHash = 0;
    double parseMs = 0.0;
    double collectMs = 0.0;
    double readVisualMs = 0.0;
    double convertMs = 0.0;
    double grSubmitMs = 0.0;
    double grWaitMs = 0.0;
    double stableHashMs = 0.0;
    double totalMs = 0.0;
    double vfsOpenMs = 0.0;
    double vfsCopyMs = 0.0;
    double snapshotHashMs = 0.0;
    ErrorInfo12 error{};
};

enum class AcceptStatus12 : uint32_t
{
    Accepted = 0,
    NotReady,
    InvalidResult,
    StaleSession,
    StaleEpoch,
};

// CPU-only acceptance token check. Call immediately before consuming a batch.
// It never publishes, allocates or mutates the result.
AcceptStatus12 AcceptBatch12(const BatchResult12& result,
                             const Identity12& live) noexcept;

uint64_t HashBytes(uint64_t seed, const void* data, size_t size) noexcept;
uint64_t HashSnapshot(const Snapshot12& snapshot) noexcept;

// The engine-side owner VFS adapter is declared in GameDx12J2ABatch.h as
// GameDx12J2ABatch_ReadSnapshot/GameDx12J2ABatch_PrepareFromVfs. The batch
// core itself only accepts an already-owned Snapshot12, so tests can build
// snapshots from immutable file bytes without linking engine code.

// Serial baseline: parse hierarchy, collect the requested sector, read and
// convert all four visuals, then publish the batch only if every slot passed.
bool PrepareBatch(const Snapshot12& snapshot, const Identity12& expected,
                  uint32_t sectorId, uint32_t rootVisualId,
                  const std::array<uint32_t, kTargetVisualCount>& visuals,
                  BatchResult12& result, const Limits12& limits) noexcept;

// M1-C: optional existing GR Job System. If jobs is null/uninitialized, the
// serial baseline is used. A partially submitted/failed group fails closed.
bool PrepareBatch(const Snapshot12& snapshot, const Identity12& expected,
                  uint32_t sectorId, uint32_t rootVisualId,
                  const std::array<uint32_t, kTargetVisualCount>& visuals,
                  gr::jobs::JobSystem* jobs, bool parallel,
                  BatchResult12& result, const Limits12& limits) noexcept;
} // namespace j2bbatch12
