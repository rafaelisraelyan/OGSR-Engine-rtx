#pragma once

#include "dx12levelhierarchy.h"
#include "dx12xraylevelasset.h"
#include "dx12xraylevelgeometry.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// F4.0-J2B-M1: CPU-only, transactional four-visual batch.
//
// The batch owns only CPU data. It never creates D3D12 resources and never
// publishes a partial result: all four requested visuals must read and convert
// successfully, or the batch is rejected.
namespace j2bbatch12
{
constexpr uint32_t kTargetVisualCount = 4;

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
    std::string reason;
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
    double totalMs = 0.0;
    std::string reason;
};

uint64_t HashBytes(uint64_t seed, const void* data, size_t size) noexcept;
uint64_t HashSnapshot(const Snapshot12& snapshot) noexcept;

// Owner VFS read into an owned snapshot. The adapter is implemented in
// GameDx12J2ABatch.cpp (engine side); this declaration is kept here so tests
// can construct snapshots from immutable file bytes as well.
bool ReadSnapshotFromVfs(const char* levelName, const Identity12& identity,
                         Snapshot12& snapshot, const Limits12& limits,
                         std::string& reason) noexcept;

// Serial baseline: parse hierarchy, collect the requested sector, read and
// convert all four visuals, then publish the batch only if every slot passed.
bool PrepareBatch(const Snapshot12& snapshot, const Identity12& expected,
                  uint32_t sectorId, uint32_t rootVisualId,
                  const std::array<uint32_t, kTargetVisualCount>& visuals,
                  BatchResult12& result, const Limits12& limits) noexcept;
} // namespace j2bbatch12
