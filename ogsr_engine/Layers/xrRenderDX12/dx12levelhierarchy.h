#pragma once

// F4.0-J2A R3: CPU-only level visual hierarchy reader.
//
// The level file stores all OGF visuals in chunk 3. Hierarchical visuals use
// the OGF_CHILDREN_L subchunk (u32 count + child visual ids). Sector chunk 8
// stores one subchunk per sector; subchunk 2 (fsP_Root) is the root visual id.
// Sector chunk headers carry the real sector id (R4 reads S->open_chunk(i)).
//
// This module has no engine, VFS, GPU or job-system dependency. It validates
// the container and publishes immutable metadata for the owner-side adapter.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace levelhierarchy12
{
constexpr uint32_t kLevelVisualsChunk = 3;
constexpr uint32_t kLevelSectorsChunk = 8;
constexpr uint32_t kSectorRootChunk = 2;

constexpr uint32_t kOgfHeader = 1;
constexpr uint32_t kOgfChildrenLink = 10;

constexpr uint32_t kMtNormal = 0;
constexpr uint32_t kMtHierarchy = 1;

constexpr uint32_t kOgfHeaderSize = 44;
constexpr uint32_t kCompressMark = 0x80000000u;
constexpr uint32_t kMaxVisuals = 65536;
constexpr uint32_t kMaxSectors = 4096;
constexpr uint32_t kMaxChildren = 4096;
constexpr uint32_t kMaxDepth = 64;

struct Bounds
{
    float minimum[3] = {0.0f, 0.0f, 0.0f};
    float maximum[3] = {0.0f, 0.0f, 0.0f};
};

struct VisualNode
{
    uint32_t id = 0;
    uint8_t type = 0;
    uint16_t shaderId = 0;
    Bounds bounds{};
    bool hasHeader = false;
    std::vector<uint32_t> children;
};

struct Sector
{
    // Real sector chunk id (not physical position). Parse rejects duplicate,
    // out-of-range and non-contiguous ids and sorts sectors by id.
    uint32_t index = 0;
    uint32_t rootVisualId = 0;
};

struct LevelHierarchy
{
    std::vector<Sector> sectors;
    std::vector<VisualNode> visuals;
};

struct LeafInstance
{
    uint32_t visualId = 0;
    uint32_t sectorIndex = 0;
    uint32_t rootVisualId = 0;
    uint32_t depth = 0;
    uint32_t referenceOrdinal = 0;
};

// Parses and validates the visual and sector containers. On failure the
// output is cleared and reason is best-effort text. All ids are checked for
// existence, cycles, duplicate visual chunks, depth and sector id continuity.
bool Parse(std::span<const uint8_t> level, LevelHierarchy& output,
           std::string& reason);

// Result of a bounded traversal. LimitExceeded means output is partial and
// must never be published as a frame; InvalidHierarchy means the input or
// budget is invalid. Complete is the only success state.
enum class CollectStatus
{
    Complete,
    LimitExceeded,
    InvalidHierarchy,
};

struct CollectBudget
{
    uint32_t maxInstances = 0;
    uint32_t maxNodeVisits = 0;
    uint32_t maxEdgeVisits = 0;
};

struct CollectStats
{
    // nodeVisits/edgeVisits are totals: index construction + traversal.
    // The breakdown is explicit so a small budget cannot hide indexing work.
    uint64_t nodeVisits = 0;
    uint64_t edgeVisits = 0;
    uint64_t indexNodeVisits = 0;
    uint64_t indexEdgeVisits = 0;
    uint64_t traversalNodeVisits = 0;
    uint64_t traversalEdgeVisits = 0;
    uint32_t instances = 0;
    uint32_t appliedMaxInstances = 0;
    uint32_t appliedMaxNodeVisits = 0;
    uint32_t appliedMaxEdgeVisits = 0;
};

// Deterministic depth-first traversal over all sectors in sector-id order.
// Children keep their authored order. Every MT_NORMAL occurrence is emitted
// as a separate instance, so a shared visual reference keeps all real draw
// instances. Node and edge budgets include index construction and are checked
// before the corresponding operation; reaching maxInstances exactly at the end
// of a complete traversal is valid, but any further drawable instance yields
// LimitExceeded.
CollectStatus CollectLeaves(const LevelHierarchy& hierarchy,
                            const CollectBudget& budget,
                            std::vector<LeafInstance>& output,
                            CollectStats& stats, std::string& reason);

// Same contract as CollectLeaves, but traverses only one real sector id.
CollectStatus CollectSectorLeaves(const LevelHierarchy& hierarchy,
                                  uint32_t sectorId,
                                  const CollectBudget& budget,
                                  std::vector<LeafInstance>& output,
                                  CollectStats& stats, std::string& reason);

enum class SectorLookupStatus
{
    Found,
    NotFound,
    LimitExceeded,
    InvalidHierarchy,
};

struct SectorLookupStats
{
    // nodeVisits/edgeVisits are totals: index/parents construction + reverse
    // traversal. The initial target visual is a charged traversal node.
    uint64_t nodeVisits = 0;
    uint64_t edgeVisits = 0;
    uint64_t indexNodeVisits = 0;
    uint64_t indexEdgeVisits = 0;
    uint64_t traversalNodeVisits = 0;
    uint64_t traversalEdgeVisits = 0;
    uint32_t appliedMaxNodeVisits = 0;
    uint32_t appliedMaxEdgeVisits = 0;
};

// Bounded reverse reachability from visualId to sector roots. Only parents
// that the collector can actually traverse are added: an MT_NORMAL node is
// terminal, so its children never create reverse edges. The parents map build
// and the reverse walk both count against the supplied limits, including the
// initial visual node. When more than one sector can reach the visual, the
// lowest sector id wins. The target must itself be a drawable MT_NORMAL node.
SectorLookupStatus FindSectorForVisual(const LevelHierarchy& hierarchy,
                                       uint32_t visualId,
                                       uint32_t maxNodeVisits,
                                       uint32_t maxEdgeVisits,
                                       uint32_t& sectorId,
                                       SectorLookupStats& stats,
                                       std::string& reason);

// Unique visual ids in first-seen order. Used by the owner to read/validate
// each OGF once while still preserving all instance packets.
bool BuildUniqueOrder(const std::vector<LeafInstance>& instances,
                      std::vector<uint32_t>& uniqueVisualIds,
                      std::string& reason);

// CPU self-tests for the container parser/traversal. Returns false on the
// first failed invariant. No engine or GPU state is touched.
bool RunTests();
} // namespace levelhierarchy12
