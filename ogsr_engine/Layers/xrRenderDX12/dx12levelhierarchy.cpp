#include "dx12levelhierarchy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace levelhierarchy12
{
namespace
{
struct ChunkView
{
    uint32_t type = 0;
    uint64_t payload = 0;
    uint32_t size = 0;
};

bool ReadU32(std::span<const uint8_t> bytes, uint64_t offset, uint32_t& value)
{
    if (offset > bytes.size() || bytes.size() - offset < 4)
        return false;
    value = static_cast<uint32_t>(bytes[static_cast<size_t>(offset)]) |
            (static_cast<uint32_t>(bytes[static_cast<size_t>(offset + 1)]) << 8) |
            (static_cast<uint32_t>(bytes[static_cast<size_t>(offset + 2)]) << 16) |
            (static_cast<uint32_t>(bytes[static_cast<size_t>(offset + 3)]) << 24);
    return true;
}

bool ReadF32(std::span<const uint8_t> bytes, uint64_t offset, float& value)
{
    uint32_t raw = 0;
    if (!ReadU32(bytes, offset, raw))
        return false;
    std::memcpy(&value, &raw, sizeof(value));
    return true;
}

bool FindUniqueChunk(std::span<const uint8_t> bytes, uint32_t id,
                     ChunkView& found, std::string& reason)
{
    bool present = false;
    uint64_t pos = 0;
    while (pos < bytes.size())
    {
        if (bytes.size() - static_cast<size_t>(pos) < 8)
        {
            reason = "truncated chunk header";
            return false;
        }
        uint32_t type = 0;
        uint32_t size = 0;
        ReadU32(bytes, pos, type);
        ReadU32(bytes, pos + 4, size);
        if ((type & kCompressMark) != 0)
        {
            reason = "compressed chunk is not supported";
            return false;
        }
        const uint64_t payload = pos + 8;
        if (payload > bytes.size() || size > bytes.size() - payload)
        {
            reason = "chunk payload exceeds input";
            return false;
        }
        if (type == id)
        {
            if (present)
            {
                reason = "duplicate mandatory level chunk";
                return false;
            }
            present = true;
            found.type = type;
            found.payload = payload;
            found.size = size;
        }
        pos = payload + size;
    }
    if (!present)
    {
        reason = "missing mandatory level chunk";
        return false;
    }
    return true;
}

bool ParseHeader(std::span<const uint8_t> level, const ChunkView& header,
                 VisualNode& node, std::string& reason)
{
    if (header.size != kOgfHeaderSize)
    {
        reason = "invalid OGF header size";
        return false;
    }
    const uint64_t data = header.payload;
    if (level[static_cast<size_t>(data)] != 4)
    {
        reason = "unsupported OGF version";
        return false;
    }
    node.type = level[static_cast<size_t>(data + 1)];
    uint32_t shader = 0;
    if (!ReadU32(level, data + 2, shader))
    {
        reason = "truncated OGF shader id";
        return false;
    }
    node.shaderId = static_cast<uint16_t>(shader & 0xFFFFu);
    for (int i = 0; i < 3; ++i)
    {
        if (!ReadF32(level, data + 4 + i * 4, node.bounds.minimum[i]) ||
            !ReadF32(level, data + 16 + i * 4, node.bounds.maximum[i]))
        {
            reason = "truncated OGF bounds";
            return false;
        }
        if (!std::isfinite(node.bounds.minimum[i]) ||
            !std::isfinite(node.bounds.maximum[i]))
        {
            reason = "non-finite OGF bounds";
            return false;
        }
        if (node.bounds.minimum[i] > node.bounds.maximum[i])
        {
            reason = "invalid OGF bounds";
            return false;
        }
    }
    node.hasHeader = true;
    return true;
}

bool ParseChildren(std::span<const uint8_t> level, const ChunkView& children,
                   VisualNode& node, std::string& reason)
{
    if (children.size < 4)
    {
        reason = "truncated OGF children header";
        return false;
    }
    uint32_t count = 0;
    ReadU32(level, children.payload, count);
    if (count > kMaxChildren)
    {
        reason = "OGF children count exceeds limit";
        return false;
    }
    const uint64_t expected = 4ull + static_cast<uint64_t>(count) * 4ull;
    if (children.size != expected)
    {
        reason = "OGF children payload size mismatch";
        return false;
    }
    node.children.clear();
    node.children.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t id = 0;
        if (!ReadU32(level, children.payload + 4 + i * 4ull, id))
        {
            reason = "truncated OGF child id";
            return false;
        }
        node.children.push_back(id);
    }
    return true;
}

bool ParseVisuals(std::span<const uint8_t> level, const ChunkView& visuals,
                  LevelHierarchy& output, std::string& reason)
{
    uint64_t pos = visuals.payload;
    const uint64_t end = visuals.payload + visuals.size;
    // Deterministic O(N log N) duplicate detection with memory bounded by
    // kMaxVisuals. The published visual order remains the authored chunk order.
    std::set<uint32_t> seenIds;
    while (pos < end)
    {
        if (end - pos < 8)
        {
            reason = "truncated visual chunk header";
            return false;
        }
        uint32_t type = 0;
        uint32_t size = 0;
        ReadU32(level, pos, type);
        ReadU32(level, pos + 4, size);
        if ((type & kCompressMark) != 0)
        {
            reason = "compressed visual chunk is not supported";
            return false;
        }
        const uint64_t payload = pos + 8;
        if (payload > end || size > end - payload)
        {
            reason = "visual chunk payload exceeds container";
            return false;
        }
        if (seenIds.find(type) != seenIds.end())
        {
            reason = "duplicate visual id";
            return false;
        }
        if (output.visuals.size() >= kMaxVisuals)
        {
            reason = "visual count exceeds limit";
            return false;
        }
        seenIds.insert(type);

        VisualNode node;
        node.id = type;
        bool seenChildren = false;
        uint64_t sub = payload;
        const uint64_t visualEnd = payload + size;
        while (sub < visualEnd)
        {
            if (visualEnd - sub < 8)
            {
                reason = "truncated OGF subchunk header";
                return false;
            }
            uint32_t subType = 0;
            uint32_t subSize = 0;
            ReadU32(level, sub, subType);
            ReadU32(level, sub + 4, subSize);
            if ((subType & kCompressMark) != 0)
            {
                reason = "compressed OGF subchunk is not supported";
                return false;
            }
            const uint64_t subPayload = sub + 8;
            if (subPayload > visualEnd || subSize > visualEnd - subPayload)
            {
                reason = "OGF subchunk payload exceeds visual";
                return false;
            }
            ChunkView view{subType, subPayload, subSize};
            if (subType == kOgfHeader)
            {
                if (node.hasHeader)
                {
                    reason = "duplicate OGF header";
                    return false;
                }
                if (!ParseHeader(level, view, node, reason))
                    return false;
            }
            else if (subType == kOgfChildrenLink)
            {
                if (seenChildren)
                {
                    reason = "duplicate OGF children link";
                    return false;
                }
                seenChildren = true;
                if (!ParseChildren(level, view, node, reason))
                    return false;
            }
            sub = subPayload + subSize;
        }
        if (!node.hasHeader)
        {
            reason = "missing OGF header";
            return false;
        }
        output.visuals.push_back(std::move(node));
        pos = payload + size;
    }
    if (output.visuals.empty())
    {
        reason = "no visuals in level container";
        return false;
    }
    return true;
}

bool ParseSectors(std::span<const uint8_t> level, const ChunkView& sectors,
                  LevelHierarchy& output, std::string& reason)
{
    uint64_t pos = sectors.payload;
    const uint64_t end = sectors.payload + sectors.size;
    std::vector<uint8_t> seen(kMaxSectors, 0);
    std::vector<Sector> parsed;
    while (pos < end)
    {
        if (end - pos < 8)
        {
            reason = "truncated sector chunk header";
            return false;
        }
        uint32_t sectorId = 0;
        uint32_t size = 0;
        ReadU32(level, pos, sectorId);
        ReadU32(level, pos + 4, size);
        if ((sectorId & kCompressMark) != 0)
        {
            reason = "compressed sector chunk is not supported";
            return false;
        }
        const uint64_t payload = pos + 8;
        if (payload > end || size > end - payload)
        {
            reason = "sector chunk payload exceeds container";
            return false;
        }
        if (sectorId >= kMaxSectors)
        {
            reason = "sector id exceeds limit";
            return false;
        }
        if (seen[sectorId] != 0)
        {
            reason = "duplicate sector id";
            return false;
        }
        seen[sectorId] = 1;
        if (parsed.size() >= kMaxSectors)
        {
            reason = "sector count exceeds limit";
            return false;
        }

        Sector sector;
        sector.index = sectorId;
        bool hasRoot = false;
        uint64_t sub = payload;
        const uint64_t sectorEnd = payload + size;
        while (sub < sectorEnd)
        {
            if (sectorEnd - sub < 8)
            {
                reason = "truncated sector subchunk header";
                return false;
            }
            uint32_t subType = 0;
            uint32_t subSize = 0;
            ReadU32(level, sub, subType);
            ReadU32(level, sub + 4, subSize);
            if ((subType & kCompressMark) != 0)
            {
                reason = "compressed sector subchunk is not supported";
                return false;
            }
            const uint64_t subPayload = sub + 8;
            if (subPayload > sectorEnd || subSize > sectorEnd - subPayload)
            {
                reason = "sector subchunk payload exceeds sector";
                return false;
            }
            if (subType == kSectorRootChunk)
            {
                if (hasRoot || subSize != 4)
                {
                    reason = "invalid sector root chunk";
                    return false;
                }
                ReadU32(level, subPayload, sector.rootVisualId);
                hasRoot = true;
            }
            sub = subPayload + subSize;
        }
        if (!hasRoot)
        {
            reason = "sector root is missing";
            return false;
        }
        parsed.push_back(sector);
        pos = payload + size;
    }
    if (parsed.empty())
    {
        reason = "no sectors in level container";
        return false;
    }

    // R4 reads sectors with S->open_chunk(i), so the real ids must form a
    // zero-based contiguous range. Physical order is irrelevant.
    const uint32_t count = static_cast<uint32_t>(parsed.size());
    for (uint32_t id = 0; id < count; ++id)
    {
        if (seen[id] == 0)
        {
            reason = "sector id is missing";
            return false;
        }
    }
    std::sort(parsed.begin(), parsed.end(),
              [](const Sector& left, const Sector& right)
              { return left.index < right.index; });
    output.sectors = std::move(parsed);
    return true;
}

bool ValidateGraph(const LevelHierarchy& hierarchy, std::string& reason)
{
    std::unordered_map<uint32_t, size_t> index;
    index.reserve(hierarchy.visuals.size());
    for (size_t i = 0; i < hierarchy.visuals.size(); ++i)
        index.emplace(hierarchy.visuals[i].id, i);

    for (const Sector& sector : hierarchy.sectors)
    {
        if (index.find(sector.rootVisualId) == index.end())
        {
            reason = "sector root visual is missing";
            return false;
        }
    }
    for (const VisualNode& node : hierarchy.visuals)
    {
        for (uint32_t child : node.children)
        {
            if (index.find(child) == index.end())
            {
                reason = "child visual is missing";
                return false;
            }
        }
    }

    // Maximum depth must be independent of visual storage order and must account
    // for shared DAG children. A global "already visited" skip is not enough:
    // the same node can be reached by a short path first and by the deepest path
    // later. Compute a memoized subtree height (longest edge path to a
    // descendant) for every node, then check each sector root against kMaxDepth.
    std::vector<uint8_t> state(hierarchy.visuals.size(), 0);
    std::vector<uint32_t> height(hierarchy.visuals.size(), 0);
    for (size_t start = 0; start < hierarchy.visuals.size(); ++start)
    {
        if (state[start] == 2)
            continue;
        struct Frame
        {
            size_t node;
            size_t nextChild;
        };
        std::vector<Frame> stack;
        stack.push_back({start, 0});
        state[start] = 1;
        while (!stack.empty())
        {
            Frame& frame = stack.back();
            const VisualNode& node = hierarchy.visuals[frame.node];
            if (frame.nextChild < node.children.size())
            {
                const uint32_t child = node.children[frame.nextChild++];
                const size_t childIndex = index.at(child);
                if (state[childIndex] == 1)
                {
                    reason = "visual hierarchy contains a cycle";
                    return false;
                }
                if (state[childIndex] == 0)
                {
                    state[childIndex] = 1;
                    stack.push_back({childIndex, 0});
                }
                continue;
            }

            uint32_t subtreeHeight = 0;
            for (const uint32_t child : node.children)
            {
                const uint32_t childHeight = height[index.at(child)];
                if (childHeight + 1 > subtreeHeight)
                    subtreeHeight = childHeight + 1;
            }
            height[frame.node] = subtreeHeight;
            state[frame.node] = 2;
            stack.pop_back();
        }
    }

    for (const Sector& sector : hierarchy.sectors)
    {
        const uint32_t rootHeight = height[index.at(sector.rootVisualId)];
        if (rootHeight > kMaxDepth)
        {
            reason = "visual hierarchy depth exceeds limit";
            return false;
        }
    }
    return true;
}

void AppendU32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void AppendChunk(std::vector<uint8_t>& out, uint32_t type,
                 const std::vector<uint8_t>& payload)
{
    AppendU32(out, type);
    AppendU32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> MakeHeader(uint8_t type, float minimum,
                                float maximum)
{
    std::vector<uint8_t> payload(kOgfHeaderSize, 0);
    payload[0] = 4;
    payload[1] = type;
    payload[2] = 1;
    payload[3] = 0;
    for (int i = 0; i < 3; ++i)
    {
        std::memcpy(payload.data() + 4 + i * 4, &minimum, sizeof(float));
        std::memcpy(payload.data() + 16 + i * 4, &maximum, sizeof(float));
    }
    return payload;
}

std::vector<uint8_t> MakeChildren(const std::vector<uint32_t>& children)
{
    std::vector<uint8_t> payload;
    AppendU32(payload, static_cast<uint32_t>(children.size()));
    for (uint32_t child : children)
        AppendU32(payload, child);
    return payload;
}

std::vector<uint8_t> MakeVisual(uint8_t type,
                                const std::vector<uint32_t>& children)
{
    std::vector<uint8_t> payload;
    AppendChunk(payload, kOgfChildrenLink, MakeChildren(children));
    AppendChunk(payload, kOgfHeader, MakeHeader(type, -1.0f, 1.0f));
    return payload;
}

std::vector<uint8_t> MakeLevel()
{
    std::vector<uint8_t> level;
    std::vector<uint8_t> visuals;
    AppendChunk(visuals, 100, MakeVisual(kMtHierarchy, {101, 102, 103}));
    AppendChunk(visuals, 101, MakeVisual(kMtNormal, {}));
    AppendChunk(visuals, 102, MakeVisual(kMtNormal, {}));
    AppendChunk(visuals, 103, MakeVisual(kMtHierarchy, {101, 104}));
    AppendChunk(visuals, 104, MakeVisual(kMtNormal, {}));
    AppendChunk(level, kLevelVisualsChunk, visuals);

    std::vector<uint8_t> sectors;
    std::vector<uint8_t> sector0;
    std::vector<uint8_t> root0;
    AppendU32(root0, 100);
    AppendChunk(sector0, kSectorRootChunk, root0);
    AppendChunk(sectors, 0, sector0);
    std::vector<uint8_t> sector1;
    std::vector<uint8_t> root1;
    AppendU32(root1, 103);
    AppendChunk(sector1, kSectorRootChunk, root1);
    AppendChunk(sectors, 1, sector1);
    AppendChunk(level, kLevelSectorsChunk, sectors);
    return level;
}

std::vector<uint8_t> MakeVisualChunks(
    uint8_t type, const std::vector<std::vector<uint32_t>>& childrenChunks)
{
    std::vector<uint8_t> payload;
    for (const std::vector<uint32_t>& children : childrenChunks)
        AppendChunk(payload, kOgfChildrenLink, MakeChildren(children));
    AppendChunk(payload, kOgfHeader, MakeHeader(type, -1.0f, 1.0f));
    return payload;
}

std::vector<uint8_t> MakeLevelWithVisuals(
    const std::vector<std::pair<uint32_t, std::vector<uint8_t>>>& visuals,
    const std::vector<uint32_t>& sectorRoots)
{
    std::vector<uint8_t> level;
    std::vector<uint8_t> visualChunk;
    for (const auto& visual : visuals)
        AppendChunk(visualChunk, visual.first, visual.second);
    AppendChunk(level, kLevelVisualsChunk, visualChunk);

    std::vector<uint8_t> sectors;
    for (size_t i = 0; i < sectorRoots.size(); ++i)
    {
        std::vector<uint8_t> sector;
        std::vector<uint8_t> root;
        AppendU32(root, sectorRoots[i]);
        AppendChunk(sector, kSectorRootChunk, root);
        AppendChunk(sectors, static_cast<uint32_t>(i), sector);
    }
    AppendChunk(level, kLevelSectorsChunk, sectors);
    return level;
}

std::vector<uint8_t> MakeChainLevel(uint32_t nodeCount, bool leafFirst)
{
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> visuals;
    visuals.reserve(nodeCount);
    for (uint32_t i = 0; i < nodeCount; ++i)
    {
        const uint32_t id = 1000 + i;
        std::vector<uint32_t> children;
        if (i + 1 < nodeCount)
            children.push_back(id + 1);
        const uint8_t type =
            i + 1 == nodeCount ? kMtNormal : kMtHierarchy;
        visuals.emplace_back(id, MakeVisual(type, children));
    }
    if (leafFirst)
        std::reverse(visuals.begin(), visuals.end());
    return MakeLevelWithVisuals(visuals, {1000});
}

std::vector<uint8_t> MakeSharedDagLevel(uint32_t tailLength)
{
    // Root 100 has a short branch 100 -> 101 -> 102 (leaf) and a longer branch
    // 100 -> 200 -> ... -> 102. The shared leaf is stored first, so an
    // order-dependent walker sees it as already visited and can miss the long
    // path. The maximum depth must still come from the longest DAG path.
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> visuals;
    visuals.emplace_back(102, MakeVisual(kMtNormal, {}));
    for (uint32_t i = tailLength; i > 0; --i)
    {
        const uint32_t id = 200 + i - 1;
        const uint32_t child = i == tailLength ? 102 : id + 1;
        visuals.emplace_back(id, MakeVisual(kMtHierarchy, {child}));
    }
    visuals.emplace_back(101, MakeVisual(kMtHierarchy, {102}));
    visuals.emplace_back(100, MakeVisual(kMtHierarchy, {101, 200}));
    return MakeLevelWithVisuals(visuals, {100});
}

std::vector<uint8_t> MakeLevelWithSectorChunks(
    const std::vector<std::pair<uint32_t, std::vector<uint8_t>>>& visuals,
    const std::vector<std::pair<uint32_t, uint32_t>>& sectors)
{
    std::vector<uint8_t> level;
    std::vector<uint8_t> visualChunk;
    for (const auto& visual : visuals)
        AppendChunk(visualChunk, visual.first, visual.second);
    AppendChunk(level, kLevelVisualsChunk, visualChunk);

    std::vector<uint8_t> sectorChunk;
    for (const auto& sector : sectors)
    {
        std::vector<uint8_t> payload;
        std::vector<uint8_t> root;
        AppendU32(root, sector.second);
        AppendChunk(payload, kSectorRootChunk, root);
        AppendChunk(sectorChunk, sector.first, payload);
    }
    AppendChunk(level, kLevelSectorsChunk, sectorChunk);
    return level;
}

std::vector<uint8_t> MakeLeafFanoutLevel(uint32_t leafCount)
{
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> visuals;
    std::vector<uint32_t> children;
    for (uint32_t i = 0; i < leafCount; ++i)
        children.push_back(101 + i);
    visuals.emplace_back(100, MakeVisual(kMtHierarchy, children));
    for (uint32_t i = 0; i < leafCount; ++i)
        visuals.emplace_back(101 + i, MakeVisual(kMtNormal, {}));
    return MakeLevelWithVisuals(visuals, {100});
}

std::vector<uint8_t> MakePathExplosionLevel(uint32_t fanout, uint32_t layers,
                                            uint32_t targetVisualId,
                                            uint32_t& targetSectorId)
{
    // Each hierarchy node in a layer points to every node in the next layer.
    // The number of distinct root-to-leaf paths is fanout^layers, while the
    // number of nodes stays O(fanout * layers). A shared MT_NORMAL leaf keeps
    // all paths drawable but must not force exponential enumeration.
    std::vector<std::vector<uint32_t>> layerIds(layers + 1);
    layerIds[0].push_back(1000);
    uint32_t nextId = 1001;
    for (uint32_t layer = 1; layer <= layers; ++layer)
    {
        for (uint32_t i = 0; i < fanout; ++i)
            layerIds[layer].push_back(nextId++);
    }
    const uint32_t sharedLeaf = 9999;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> visuals;
    for (uint32_t layer = 0; layer < layers; ++layer)
    {
        for (const uint32_t id : layerIds[layer])
            visuals.emplace_back(id,
                                 MakeVisual(kMtHierarchy, layerIds[layer + 1]));
    }
    for (const uint32_t id : layerIds[layers])
        visuals.emplace_back(id, MakeVisual(kMtHierarchy, {sharedLeaf}));
    visuals.emplace_back(sharedLeaf, MakeVisual(kMtNormal, {}));
    visuals.emplace_back(3000, MakeVisual(kMtHierarchy, {targetVisualId}));
    visuals.emplace_back(targetVisualId, MakeVisual(kMtNormal, {}));
    targetSectorId = 1;
    return MakeLevelWithSectorChunks(visuals, {{0, 1000}, {1, 3000}});
}

std::vector<uint8_t> MakeDecoyTerminalLevel()
{
    // Sector 0 reaches visual 9 only through MT_NORMAL 101, which the
    // collector treats as terminal. Sector 1 reaches 9 through two legitimate
    // hierarchy paths, producing two real drawable instances.
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> visuals;
    visuals.emplace_back(9, MakeVisual(kMtNormal, {}));
    visuals.emplace_back(101, MakeVisual(kMtNormal, {9}));
    visuals.emplace_back(100, MakeVisual(kMtHierarchy, {101}));
    visuals.emplace_back(201, MakeVisual(kMtHierarchy, {9}));
    visuals.emplace_back(202, MakeVisual(kMtHierarchy, {9}));
    visuals.emplace_back(200, MakeVisual(kMtHierarchy, {201, 202}));
    return MakeLevelWithSectorChunks(visuals, {{0, 100}, {1, 200}});
}

std::vector<uint8_t> MakeSingleNodeLevel()
{
    return MakeLevelWithVisuals({{9, MakeVisual(kMtNormal, {})}}, {9});
}

std::vector<uint8_t> MakeLookupChainLevel()
{
    return MakeLevelWithVisuals(
        {{100, MakeVisual(kMtHierarchy, {9})},
         {9, MakeVisual(kMtNormal, {})}},
        {100});
}

bool Expect(bool condition, const char* text, int& failures)
{
    if (condition)
    {
        std::printf("PASS %s\n", text);
        return true;
    }
    std::printf("FAIL %s\n", text);
    ++failures;
    return false;
}

bool ExpectReason(bool condition, const char* expected,
                  const std::string& actual, const char* text, int& failures)
{
    if (condition && actual == expected)
    {
        std::printf("PASS %s\n", text);
        return true;
    }
    std::printf("FAIL %s (reason='%s')\n", text, actual.c_str());
    ++failures;
    return false;
}
} // namespace

bool Parse(std::span<const uint8_t> level, LevelHierarchy& output,
           std::string& reason)
{
    output = {};
    reason.clear();
    if (level.empty())
    {
        reason = "empty level input";
        return false;
    }

    ChunkView visuals{};
    ChunkView sectors{};
    if (!FindUniqueChunk(level, kLevelVisualsChunk, visuals, reason) ||
        !FindUniqueChunk(level, kLevelSectorsChunk, sectors, reason))
        return false;

    LevelHierarchy parsed;
    if (!ParseVisuals(level, visuals, parsed, reason) ||
        !ParseSectors(level, sectors, parsed, reason) ||
        !ValidateGraph(parsed, reason))
        return false;

    output = std::move(parsed);
    return true;
}

namespace
{
std::string CollectIndexNodeBudgetReason(const CollectStats& stats,
                                         const CollectBudget& budget)
{
    return "collect index node budget exceeded: visits=" +
           std::to_string(stats.indexNodeVisits) +
           " total=" + std::to_string(stats.nodeVisits) +
           " limit=" + std::to_string(budget.maxNodeVisits);
}

std::string CollectTraversalNodeBudgetReason(const CollectStats& stats,
                                             const CollectBudget& budget)
{
    return "collect traversal node budget exceeded: visits=" +
           std::to_string(stats.traversalNodeVisits) +
           " total=" + std::to_string(stats.nodeVisits) +
           " limit=" + std::to_string(budget.maxNodeVisits);
}

std::string CollectTraversalEdgeBudgetReason(const CollectStats& stats,
                                             const CollectBudget& budget)
{
    return "collect traversal edge budget exceeded: visits=" +
           std::to_string(stats.traversalEdgeVisits) +
           " total=" + std::to_string(stats.edgeVisits) +
           " limit=" + std::to_string(budget.maxEdgeVisits);
}

std::string InstanceBudgetReason(const CollectStats& stats,
                                 const CollectBudget& budget)
{
    return "collect instance limit exceeded: instances=" +
           std::to_string(stats.instances) +
           " limit=" + std::to_string(budget.maxInstances);
}

CollectStatus CollectFromSectors(const LevelHierarchy& hierarchy,
                                 std::span<const Sector> sectors,
                                 const CollectBudget& budget,
                                 std::vector<LeafInstance>& output,
                                 CollectStats& stats, std::string& reason)
{
    output.clear();
    stats = {};
    reason.clear();
    stats.appliedMaxInstances = budget.maxInstances;
    stats.appliedMaxNodeVisits = budget.maxNodeVisits;
    stats.appliedMaxEdgeVisits = budget.maxEdgeVisits;
    if (budget.maxInstances == 0)
    {
        reason = "maxInstances must be positive";
        return CollectStatus::InvalidHierarchy;
    }
    if (budget.maxNodeVisits == 0)
    {
        reason = "maxNodeVisits must be positive";
        return CollectStatus::InvalidHierarchy;
    }
    if (budget.maxEdgeVisits == 0)
    {
        reason = "maxEdgeVisits must be positive";
        return CollectStatus::InvalidHierarchy;
    }
    if (hierarchy.visuals.empty() || sectors.empty())
    {
        reason = "empty hierarchy";
        return CollectStatus::InvalidHierarchy;
    }

    // Index construction is charged to the node budget. Without this, a tiny
    // traversal budget would still scan the whole hierarchy before failing.
    std::unordered_map<uint32_t, size_t> index;
    for (size_t i = 0; i < hierarchy.visuals.size(); ++i)
    {
        if (stats.nodeVisits >= budget.maxNodeVisits)
        {
            stats.instances = static_cast<uint32_t>(output.size());
            reason = CollectIndexNodeBudgetReason(stats, budget);
            return CollectStatus::LimitExceeded;
        }
        ++stats.nodeVisits;
        ++stats.indexNodeVisits;
        if (!index.emplace(hierarchy.visuals[i].id, i).second)
        {
            reason = "duplicate visual id";
            return CollectStatus::InvalidHierarchy;
        }
    }

    std::unordered_map<uint32_t, uint32_t> ordinals;
    struct Frame
    {
        size_t node;
        uint32_t depth;
    };
    std::vector<Frame> stack;

    for (const Sector& sector : sectors)
    {
        const auto root = index.find(sector.rootVisualId);
        if (root == index.end())
        {
            reason = "sector root visual is missing";
            return CollectStatus::InvalidHierarchy;
        }
        if (stats.nodeVisits >= budget.maxNodeVisits)
        {
            stats.instances = static_cast<uint32_t>(output.size());
            reason = CollectTraversalNodeBudgetReason(stats, budget);
            return CollectStatus::LimitExceeded;
        }
        ++stats.nodeVisits;
        ++stats.traversalNodeVisits;
        stack.push_back({root->second, 0});
        while (!stack.empty())
        {
            const Frame frame = stack.back();
            stack.pop_back();
            const VisualNode& node = hierarchy.visuals[frame.node];
            if (node.type == kMtNormal)
            {
                if (output.size() >= budget.maxInstances)
                {
                    stats.instances = static_cast<uint32_t>(output.size());
                    reason = InstanceBudgetReason(stats, budget);
                    return CollectStatus::LimitExceeded;
                }
                const uint32_t ordinal = ordinals[node.id]++;
                output.push_back({node.id, sector.index, sector.rootVisualId,
                                  frame.depth, ordinal});
                continue;
            }
            for (auto it = node.children.rbegin(); it != node.children.rend();
                 ++it)
            {
                if (stats.edgeVisits >= budget.maxEdgeVisits)
                {
                    stats.instances = static_cast<uint32_t>(output.size());
                    reason = CollectTraversalEdgeBudgetReason(stats, budget);
                    return CollectStatus::LimitExceeded;
                }
                ++stats.edgeVisits;
                ++stats.traversalEdgeVisits;
                if (stats.nodeVisits >= budget.maxNodeVisits)
                {
                    stats.instances = static_cast<uint32_t>(output.size());
                    reason = CollectTraversalNodeBudgetReason(stats, budget);
                    return CollectStatus::LimitExceeded;
                }
                ++stats.nodeVisits;
                ++stats.traversalNodeVisits;
                const auto child = index.find(*it);
                if (child == index.end())
                {
                    reason = "child visual is missing";
                    return CollectStatus::InvalidHierarchy;
                }
                stack.push_back({child->second, frame.depth + 1});
            }
        }
    }

    stats.instances = static_cast<uint32_t>(output.size());
    if (output.empty())
    {
        reason = "no drawable MT_NORMAL visuals";
        return CollectStatus::InvalidHierarchy;
    }
    return CollectStatus::Complete;
}

std::string LookupIndexNodeBudgetReason(const SectorLookupStats& stats,
                                        uint32_t maxNodeVisits)
{
    return "lookup index node budget exceeded: visits=" +
           std::to_string(stats.indexNodeVisits) +
           " total=" + std::to_string(stats.nodeVisits) +
           " limit=" + std::to_string(maxNodeVisits);
}

std::string LookupIndexEdgeBudgetReason(const SectorLookupStats& stats,
                                        uint32_t maxEdgeVisits)
{
    return "lookup index edge budget exceeded: visits=" +
           std::to_string(stats.indexEdgeVisits) +
           " total=" + std::to_string(stats.edgeVisits) +
           " limit=" + std::to_string(maxEdgeVisits);
}

std::string LookupTraversalNodeBudgetReason(const SectorLookupStats& stats,
                                            uint32_t maxNodeVisits)
{
    return "lookup traversal node budget exceeded: visits=" +
           std::to_string(stats.traversalNodeVisits) +
           " total=" + std::to_string(stats.nodeVisits) +
           " limit=" + std::to_string(maxNodeVisits);
}

std::string LookupTraversalEdgeBudgetReason(const SectorLookupStats& stats,
                                            uint32_t maxEdgeVisits)
{
    return "lookup traversal edge budget exceeded: visits=" +
           std::to_string(stats.traversalEdgeVisits) +
           " total=" + std::to_string(stats.edgeVisits) +
           " limit=" + std::to_string(maxEdgeVisits);
}
} // namespace

CollectStatus CollectLeaves(const LevelHierarchy& hierarchy,
                            const CollectBudget& budget,
                            std::vector<LeafInstance>& output,
                            CollectStats& stats, std::string& reason)
{
    return CollectFromSectors(hierarchy, hierarchy.sectors, budget, output,
                              stats, reason);
}

CollectStatus CollectSectorLeaves(const LevelHierarchy& hierarchy,
                                  uint32_t sectorId,
                                  const CollectBudget& budget,
                                  std::vector<LeafInstance>& output,
                                  CollectStats& stats, std::string& reason)
{
    output.clear();
    stats = {};
    reason.clear();
    stats.appliedMaxInstances = budget.maxInstances;
    stats.appliedMaxNodeVisits = budget.maxNodeVisits;
    stats.appliedMaxEdgeVisits = budget.maxEdgeVisits;
    const Sector* found = nullptr;
    for (const Sector& sector : hierarchy.sectors)
    {
        if (sector.index == sectorId)
        {
            found = &sector;
            break;
        }
    }
    if (found == nullptr)
    {
        reason = "sector id not found";
        return CollectStatus::InvalidHierarchy;
    }
    return CollectFromSectors(hierarchy, std::span<const Sector>(found, 1),
                              budget, output, stats, reason);
}

SectorLookupStatus FindSectorForVisual(const LevelHierarchy& hierarchy,
                                       uint32_t visualId,
                                       uint32_t maxNodeVisits,
                                       uint32_t maxEdgeVisits,
                                       uint32_t& sectorId,
                                       SectorLookupStats& stats,
                                       std::string& reason)
{
    sectorId = 0;
    stats = {};
    reason.clear();
    stats.appliedMaxNodeVisits = maxNodeVisits;
    stats.appliedMaxEdgeVisits = maxEdgeVisits;
    if (maxNodeVisits == 0)
    {
        reason = "maxNodeVisits must be positive";
        return SectorLookupStatus::InvalidHierarchy;
    }
    if (maxEdgeVisits == 0)
    {
        reason = "maxEdgeVisits must be positive";
        return SectorLookupStatus::InvalidHierarchy;
    }
    if (hierarchy.visuals.empty() || hierarchy.sectors.empty())
    {
        reason = "empty hierarchy";
        return SectorLookupStatus::InvalidHierarchy;
    }

    // Index and reverse parent construction are charged to the node/edge
    // budgets. Children of MT_NORMAL nodes are scanned for accounting but do
    // not create reverse edges because the collector treats MT_NORMAL as a
    // terminal drawable node.
    std::unordered_map<uint32_t, size_t> index;
    std::unordered_map<uint32_t, std::vector<uint32_t>> parents;
    for (size_t i = 0; i < hierarchy.visuals.size(); ++i)
    {
        const VisualNode& node = hierarchy.visuals[i];
        if (stats.nodeVisits >= maxNodeVisits)
        {
            reason = LookupIndexNodeBudgetReason(stats, maxNodeVisits);
            return SectorLookupStatus::LimitExceeded;
        }
        ++stats.nodeVisits;
        ++stats.indexNodeVisits;
        if (!index.emplace(node.id, i).second)
        {
            reason = "duplicate visual id";
            return SectorLookupStatus::InvalidHierarchy;
        }
        for (const uint32_t child : node.children)
        {
            if (stats.edgeVisits >= maxEdgeVisits)
            {
                reason = LookupIndexEdgeBudgetReason(stats, maxEdgeVisits);
                return SectorLookupStatus::LimitExceeded;
            }
            ++stats.edgeVisits;
            ++stats.indexEdgeVisits;
            if (node.type != kMtNormal)
                parents[child].push_back(node.id);
        }
    }

    const auto target = index.find(visualId);
    if (target == index.end())
    {
        reason = "visual not found";
        return SectorLookupStatus::NotFound;
    }
    if (hierarchy.visuals[target->second].type != kMtNormal)
    {
        reason = "visual is not a drawable MT_NORMAL node";
        return SectorLookupStatus::NotFound;
    }

    std::unordered_set<uint32_t> reachable;
    std::vector<uint32_t> queue;
    if (stats.nodeVisits >= maxNodeVisits)
    {
        reason = LookupTraversalNodeBudgetReason(stats, maxNodeVisits);
        return SectorLookupStatus::LimitExceeded;
    }
    ++stats.nodeVisits;
    ++stats.traversalNodeVisits;
    reachable.insert(visualId);
    queue.push_back(visualId);
    for (size_t head = 0; head < queue.size(); ++head)
    {
        const uint32_t current = queue[head];
        const auto found = parents.find(current);
        if (found == parents.end())
            continue;
        for (const uint32_t parent : found->second)
        {
            if (stats.edgeVisits >= maxEdgeVisits)
            {
                reason = LookupTraversalEdgeBudgetReason(stats, maxEdgeVisits);
                return SectorLookupStatus::LimitExceeded;
            }
            ++stats.edgeVisits;
            ++stats.traversalEdgeVisits;
            if (reachable.find(parent) != reachable.end())
                continue;
            if (stats.nodeVisits >= maxNodeVisits)
            {
                reason = LookupTraversalNodeBudgetReason(stats, maxNodeVisits);
                return SectorLookupStatus::LimitExceeded;
            }
            reachable.insert(parent);
            ++stats.nodeVisits;
            ++stats.traversalNodeVisits;
            queue.push_back(parent);
        }
    }

    for (const Sector& sector : hierarchy.sectors)
    {
        if (reachable.find(sector.rootVisualId) != reachable.end())
        {
            sectorId = sector.index;
            return SectorLookupStatus::Found;
        }
    }
    reason = "visual is not reachable from any sector root";
    return SectorLookupStatus::NotFound;
}

bool BuildUniqueOrder(const std::vector<LeafInstance>& instances,
                      std::vector<uint32_t>& uniqueVisualIds,
                      std::string& reason)
{
    uniqueVisualIds.clear();
    reason.clear();
    std::unordered_set<uint32_t> seen;
    seen.reserve(instances.size());
    for (const LeafInstance& instance : instances)
    {
        if (seen.insert(instance.visualId).second)
            uniqueVisualIds.push_back(instance.visualId);
    }
    if (uniqueVisualIds.empty())
    {
        reason = "no unique visuals";
        return false;
    }
    return true;
}

bool RunTests()
{
    int failures = 0;
    const std::vector<uint8_t> level = MakeLevel();

    LevelHierarchy hierarchy;
    std::string reason;
    Expect(Parse(level, hierarchy, reason), "parse valid hierarchy", failures);
    Expect(hierarchy.visuals.size() == 5, "five visuals", failures);
    Expect(hierarchy.sectors.size() == 2, "two sectors", failures);

    std::vector<LeafInstance> leaves;
    CollectStats collectStats;
    const CollectBudget fullBudget{16, 1000, 2000};
    Expect(CollectLeaves(hierarchy, fullBudget, leaves, collectStats, reason) ==
               CollectStatus::Complete,
           "collect leaves", failures);
    Expect(leaves.size() == 6, "six real instances", failures);
    Expect(leaves[0].visualId == 101 && leaves[0].sectorIndex == 0 &&
               leaves[0].referenceOrdinal == 0,
           "first sector leaf provenance", failures);
    Expect(leaves[3].visualId == 104 && leaves[3].sectorIndex == 0,
           "nested leaf provenance", failures);
    Expect(leaves[4].visualId == 101 && leaves[4].sectorIndex == 1 &&
               leaves[4].referenceOrdinal == 2,
           "shared reference keeps later instance", failures);

    std::vector<LeafInstance> capped;
    CollectStats cappedStats;
    const CollectStatus cappedStatus =
        CollectLeaves(hierarchy, {3, 1000, 2000}, capped, cappedStats, reason);
    Expect(cappedStatus == CollectStatus::LimitExceeded,
           "bounded collect rejects truncation", failures);
    Expect(capped.size() == 3, "bounded collect size", failures);

    std::vector<uint32_t> unique;
    Expect(BuildUniqueOrder(leaves, unique, reason), "unique order", failures);
    Expect(unique.size() == 3 && unique[0] == 101 && unique[1] == 102 &&
               unique[2] == 104,
           "unique order preserves first sight", failures);

    std::vector<uint8_t> cycle = MakeLevel();
    // Build a minimal cycle level: root 100 -> 103 -> 100.
    std::vector<uint8_t> cycleVisuals;
    AppendChunk(cycleVisuals, 100, MakeVisual(kMtHierarchy, {103}));
    AppendChunk(cycleVisuals, 103, MakeVisual(kMtHierarchy, {100}));
    std::vector<uint8_t> cycleSectors;
    std::vector<uint8_t> cycleSector;
    std::vector<uint8_t> cycleRoot;
    AppendU32(cycleRoot, 100);
    AppendChunk(cycleSector, kSectorRootChunk, cycleRoot);
    AppendChunk(cycleSectors, 0, cycleSector);
    cycle.clear();
    AppendChunk(cycle, kLevelVisualsChunk, cycleVisuals);
    AppendChunk(cycle, kLevelSectorsChunk, cycleSectors);
    LevelHierarchy bad;
    Expect(!Parse(cycle, bad, reason), "cycle rejected", failures);

    std::vector<uint8_t> missingRoot = level;
    // Corrupt the final root payload (last 4 bytes of the sectors chunk).
    missingRoot[missingRoot.size() - 1] = 0x7F;
    Expect(!Parse(missingRoot, bad, reason), "missing root rejected", failures);

    std::vector<uint8_t> duplicate = level;
    std::vector<uint8_t> duplicateVisuals;
    AppendChunk(duplicateVisuals, 100, MakeVisual(kMtNormal, {}));
    AppendChunk(duplicateVisuals, 100, MakeVisual(kMtNormal, {}));
    std::vector<uint8_t> duplicateLevel;
    AppendChunk(duplicateLevel, kLevelVisualsChunk, duplicateVisuals);
    AppendChunk(duplicateLevel, kLevelSectorsChunk, std::vector<uint8_t>());
    Expect(!Parse(duplicateLevel, bad, reason), "duplicate visual rejected",
           failures);

    std::vector<uint8_t> compressedTop = level;
    compressedTop[3] |= 0x80;
    const bool compressedTopRejected = !Parse(compressedTop, bad, reason);
    ExpectReason(compressedTopRejected, "compressed chunk is not supported",
                 reason, "top-level compressed chunk rejected", failures);

    std::vector<uint8_t> compressedVisual = level;
    compressedVisual[8 + 3] |= 0x80;
    const bool compressedVisualRejected = !Parse(compressedVisual, bad, reason);
    ExpectReason(compressedVisualRejected,
                 "compressed visual chunk is not supported", reason,
                 "visual compressed chunk rejected", failures);

    std::vector<uint8_t> compressedSub = level;
    compressedSub[16 + 3] |= 0x80;
    const bool compressedSubRejected = !Parse(compressedSub, bad, reason);
    ExpectReason(compressedSubRejected,
                 "compressed OGF subchunk is not supported", reason,
                 "OGF subchunk compressed chunk rejected", failures);

    LevelHierarchy hierarchyOnly;
    std::vector<uint8_t> hierarchyVisuals;
    AppendChunk(hierarchyVisuals, 100, MakeVisual(kMtHierarchy, {}));
    std::vector<uint8_t> hierarchyLevel;
    AppendChunk(hierarchyLevel, kLevelVisualsChunk, hierarchyVisuals);
    AppendChunk(hierarchyLevel, kLevelSectorsChunk, std::vector<uint8_t>());
    Expect(!Parse(hierarchyLevel, hierarchyOnly, reason),
           "sectorless hierarchy rejected", failures);

    const std::vector<uint8_t> depth64RootFirst = MakeChainLevel(65, false);
    const std::vector<uint8_t> depth64LeafFirst = MakeChainLevel(65, true);
    const std::vector<uint8_t> depth65LeafFirst = MakeChainLevel(66, true);
    const std::vector<uint8_t> depth79LeafFirst = MakeChainLevel(80, true);
    const std::vector<uint8_t> sharedDagOverflow = MakeSharedDagLevel(70);
    Expect(Parse(depth64RootFirst, bad, reason),
           "depth 64 accepted root-first", failures);
    Expect(Parse(depth64LeafFirst, bad, reason),
           "depth 64 accepted leaf-first", failures);
    const bool depth65Rejected = !Parse(depth65LeafFirst, bad, reason);
    Expect(depth65Rejected &&
               reason == "visual hierarchy depth exceeds limit",
           "depth 65 rejected leaf-first", failures);
    const bool depth79Rejected = !Parse(depth79LeafFirst, bad, reason);
    Expect(depth79Rejected &&
               reason == "visual hierarchy depth exceeds limit",
           "depth 79 rejected leaf-first", failures);
    const bool sharedDagRejected = !Parse(sharedDagOverflow, bad, reason);
    Expect(sharedDagRejected &&
               reason == "visual hierarchy depth exceeds limit",
           "shared DAG depth overflow rejected", failures);

    const std::vector<uint8_t> missingChild = MakeLevelWithVisuals(
        {{100, MakeVisual(kMtHierarchy, {999})}}, {100});
    const bool missingChildRejected = !Parse(missingChild, bad, reason);
    Expect(missingChildRejected && reason == "child visual is missing",
           "missing child id rejected", failures);

    const std::vector<uint8_t> duplicateEmptyFirst = MakeLevelWithVisuals(
        {{100, MakeVisualChunks(kMtHierarchy, {{}, {102}})},
         {102, MakeVisual(kMtNormal, {})}},
        {100});
    const bool duplicateEmptyFirstRejected =
        !Parse(duplicateEmptyFirst, bad, reason);
    Expect(duplicateEmptyFirstRejected &&
               reason == "duplicate OGF children link",
           "duplicate children empty-then-nonempty rejected", failures);

    const std::vector<uint8_t> duplicateNonemptyFirst = MakeLevelWithVisuals(
        {{100, MakeVisualChunks(kMtHierarchy, {{102}, {}})},
         {102, MakeVisual(kMtNormal, {})}},
        {100});
    const bool duplicateNonemptyFirstRejected =
        !Parse(duplicateNonemptyFirst, bad, reason);
    Expect(duplicateNonemptyFirstRejected &&
               reason == "duplicate OGF children link",
           "duplicate children nonempty-then-empty rejected", failures);

    const std::vector<uint8_t> duplicateEmptyPair = MakeLevelWithVisuals(
        {{100, MakeVisualChunks(kMtHierarchy, {{}, {}})}}, {100});
    const bool duplicateEmptyPairRejected =
        !Parse(duplicateEmptyPair, bad, reason);
    Expect(duplicateEmptyPairRejected &&
               reason == "duplicate OGF children link",
           "duplicate children empty-then-empty rejected", failures);

    const std::vector<std::pair<uint32_t, std::vector<uint8_t>>>
        sectorVisuals{{100, MakeVisual(kMtHierarchy, {101})},
                      {101, MakeVisual(kMtNormal, {})},
                      {200, MakeVisual(kMtHierarchy, {201})},
                      {201, MakeVisual(kMtNormal, {})}};
    const std::vector<uint8_t> orderedSectors =
        MakeLevelWithSectorChunks(sectorVisuals, {{0, 100}, {1, 200}});
    const std::vector<uint8_t> reorderedSectors =
        MakeLevelWithSectorChunks(sectorVisuals, {{1, 200}, {0, 100}});
    LevelHierarchy sectorsOrdered;
    LevelHierarchy sectorsReordered;
    Expect(Parse(orderedSectors, sectorsOrdered, reason),
           "sector ids 0,1 parsed", failures);
    Expect(Parse(reorderedSectors, sectorsReordered, reason),
           "sector ids 1,0 parsed", failures);
    const bool orderedIds =
        sectorsOrdered.sectors.size() == 2 &&
        sectorsOrdered.sectors[0].index == 0 &&
        sectorsOrdered.sectors[0].rootVisualId == 100 &&
        sectorsOrdered.sectors[1].index == 1 &&
        sectorsOrdered.sectors[1].rootVisualId == 200;
    const bool reorderedIds =
        sectorsReordered.sectors.size() == 2 &&
        sectorsReordered.sectors[0].index == 0 &&
        sectorsReordered.sectors[0].rootVisualId == 100 &&
        sectorsReordered.sectors[1].index == 1 &&
        sectorsReordered.sectors[1].rootVisualId == 200;
    Expect(orderedIds, "sector ids preserve 0,1 provenance", failures);
    Expect(reorderedIds, "sector ids preserve 1,0 provenance", failures);

    const bool duplicateSectorRejected =
        !Parse(MakeLevelWithSectorChunks(sectorVisuals, {{0, 100}, {0, 200}}),
               bad, reason);
    ExpectReason(duplicateSectorRejected, "duplicate sector id", reason,
                 "duplicate sector id rejected", failures);
    const bool missingSectorRejected =
        !Parse(MakeLevelWithSectorChunks(sectorVisuals, {{0, 100}, {2, 200}}),
               bad, reason);
    ExpectReason(missingSectorRejected, "sector id is missing", reason,
                 "missing sector id rejected", failures);
    const bool outOfRangeSectorRejected = !Parse(
        MakeLevelWithSectorChunks(sectorVisuals, {{0, 100}, {kMaxSectors, 200}}),
        bad, reason);
    ExpectReason(outOfRangeSectorRejected, "sector id exceeds limit", reason,
                 "out-of-range sector id rejected", failures);

    std::vector<LeafInstance> sectorOneLeaves;
    CollectStats sectorOneStats;
    const CollectStatus sectorOneStatus = CollectSectorLeaves(
        sectorsReordered, 1, {4, 100, 100}, sectorOneLeaves, sectorOneStats,
        reason);
    Expect(sectorOneStatus == CollectStatus::Complete &&
               sectorOneLeaves.size() == 1 &&
               sectorOneLeaves[0].visualId == 201 &&
               sectorOneLeaves[0].sectorIndex == 1 &&
               sectorOneLeaves[0].rootVisualId == 200 &&
               sectorOneLeaves[0].referenceOrdinal == 0,
           "sector id not confused with root or ordinal", failures);

    const std::vector<uint8_t> threeLeaves = MakeLeafFanoutLevel(3);
    LevelHierarchy threeLeafHierarchy;
    Expect(Parse(threeLeaves, threeLeafHierarchy, reason),
           "three-leaf boundary parsed", failures);
    std::vector<LeafInstance> threeLeafOut;
    CollectStats threeLeafStats;
    const CollectStatus threeLeafStatus = CollectLeaves(
        threeLeafHierarchy, {3, 8, 3}, threeLeafOut, threeLeafStats, reason);
    Expect(threeLeafStatus == CollectStatus::Complete &&
               threeLeafOut.size() == 3 &&
               threeLeafStats.nodeVisits ==
                   threeLeafStats.indexNodeVisits +
                       threeLeafStats.traversalNodeVisits &&
               threeLeafStats.edgeVisits ==
                   threeLeafStats.indexEdgeVisits +
                       threeLeafStats.traversalEdgeVisits,
           "exact maxInstances complete", failures);

    const std::vector<uint8_t> fourLeaves = MakeLeafFanoutLevel(4);
    LevelHierarchy fourLeafHierarchy;
    Expect(Parse(fourLeaves, fourLeafHierarchy, reason),
           "four-leaf boundary parsed", failures);
    std::vector<LeafInstance> fourLeafOut;
    CollectStats fourLeafStats;
    const CollectStatus fourLeafStatus = CollectLeaves(
        fourLeafHierarchy, {3, 10, 4}, fourLeafOut, fourLeafStats, reason);
    Expect(fourLeafStatus == CollectStatus::LimitExceeded &&
               fourLeafOut.size() == 3 &&
               reason == "collect instance limit exceeded: instances=3 limit=3",
           "maxInstances plus one rejects", failures);

    const std::vector<uint8_t> oneLeaf = MakeLeafFanoutLevel(1);
    LevelHierarchy oneLeafHierarchy;
    Expect(Parse(oneLeaf, oneLeafHierarchy, reason), "one-leaf parsed",
           failures);
    std::vector<LeafInstance> oneLeafOut;
    CollectStats oneLeafStats;
    Expect(CollectLeaves(oneLeafHierarchy, {1, 4, 1}, oneLeafOut,
                         oneLeafStats, reason) == CollectStatus::Complete,
           "exact node and edge budgets complete", failures);
    const CollectStatus indexBudgetStatus =
        CollectLeaves(oneLeafHierarchy, {1, 1, 1}, oneLeafOut, oneLeafStats,
                      reason);
    Expect(indexBudgetStatus == CollectStatus::LimitExceeded &&
               reason == "collect index node budget exceeded: visits=1 "
                         "total=1 limit=1",
           "index node budget checked before scan", failures);
    const CollectStatus nodeBudgetStatus =
        CollectLeaves(oneLeafHierarchy, {1, 3, 1}, oneLeafOut, oneLeafStats,
                      reason);
    Expect(nodeBudgetStatus == CollectStatus::LimitExceeded &&
               reason == "collect traversal node budget exceeded: visits=1 "
                         "total=3 limit=3",
           "node budget checked before expansion", failures);

    const std::vector<uint8_t> twoLeaves = MakeLeafFanoutLevel(2);
    LevelHierarchy twoLeafHierarchy;
    Expect(Parse(twoLeaves, twoLeafHierarchy, reason), "two-leaf parsed",
           failures);
    std::vector<LeafInstance> twoLeafOut;
    CollectStats twoLeafStats;
    Expect(CollectLeaves(twoLeafHierarchy, {2, 6, 2}, twoLeafOut,
                         twoLeafStats, reason) == CollectStatus::Complete,
           "exact two-leaf edge budget complete", failures);
    const CollectStatus edgeBudgetStatus =
        CollectLeaves(twoLeafHierarchy, {2, 6, 1}, twoLeafOut, twoLeafStats,
                      reason);
    Expect(edgeBudgetStatus == CollectStatus::LimitExceeded &&
               reason == "collect traversal edge budget exceeded: visits=1 "
                         "total=1 limit=1",
           "edge budget checked before expansion", failures);

    const std::vector<uint8_t> decoyLevel = MakeDecoyTerminalLevel();
    LevelHierarchy decoyHierarchy;
    Expect(Parse(decoyLevel, decoyHierarchy, reason),
           "terminal decoy hierarchy parsed", failures);
    SectorLookupStats decoyLookupStats;
    uint32_t decoySector = 0;
    const SectorLookupStatus decoyLookup = FindSectorForVisual(
        decoyHierarchy, 9, 200, 200, decoySector, decoyLookupStats, reason);
    Expect(decoyLookup == SectorLookupStatus::Found && decoySector == 1,
           "lookup skips terminal MT_NORMAL decoy", failures);
    std::vector<LeafInstance> decoyLeaves;
    CollectStats decoyStats;
    const CollectStatus decoyCollect = CollectSectorLeaves(
        decoyHierarchy, decoySector, {8, 200, 200}, decoyLeaves, decoyStats,
        reason);
    Expect(decoyCollect == CollectStatus::Complete &&
               decoyLeaves.size() == 2 &&
               decoyLeaves[0].visualId == 9 &&
               decoyLeaves[0].sectorIndex == 1 &&
               decoyLeaves[0].rootVisualId == 200 &&
               decoyLeaves[0].referenceOrdinal == 0 &&
               decoyLeaves[1].visualId == 9 &&
               decoyLeaves[1].referenceOrdinal == 1,
           "decoy sector collects shared visual 9 instances", failures);
    std::vector<LeafInstance> decoySector0;
    CollectStats decoySector0Stats;
    const CollectStatus decoySector0Status = CollectSectorLeaves(
        decoyHierarchy, 0, {8, 200, 200}, decoySector0, decoySector0Stats,
        reason);
    bool decoySector0HasNine = false;
    for (const LeafInstance& instance : decoySector0)
        if (instance.visualId == 9)
            decoySector0HasNine = true;
    Expect(decoySector0Status == CollectStatus::Complete &&
               decoySector0.size() == 1 &&
               decoySector0[0].visualId == 101 && !decoySector0HasNine,
           "terminal decoy sector does not emit visual 9", failures);

    SectorLookupStats decoyDedupStats;
    uint32_t decoyDedupSector = 0;
    const SectorLookupStatus decoyDedup = FindSectorForVisual(
        decoyHierarchy, 9, 10, 200, decoyDedupSector, decoyDedupStats,
        reason);
    Expect(decoyDedup == SectorLookupStatus::Found &&
               decoyDedupSector == 1 &&
               decoyDedupStats.nodeVisits == 10 &&
               decoyDedupStats.traversalNodeVisits == 4,
           "lookup dedupes repeated parent without extra node", failures);

    SectorLookupStats decoyBudgetStats;
    uint32_t decoyBudgetSector = 0;
    const SectorLookupStatus decoyBudget = FindSectorForVisual(
        decoyHierarchy, 9, 9, 200, decoyBudgetSector, decoyBudgetStats,
        reason);
    Expect(decoyBudget == SectorLookupStatus::LimitExceeded &&
               reason == "lookup traversal node budget exceeded: visits=3 "
                         "total=9 limit=9" &&
               decoyBudgetStats.nodeVisits == 9 &&
               decoyBudgetStats.traversalNodeVisits == 3,
           "lookup checks node budget before new parent insert", failures);

    LevelHierarchy singleNodeHierarchy;
    Expect(Parse(MakeSingleNodeLevel(), singleNodeHierarchy, reason),
           "single-node hierarchy parsed", failures);
    SectorLookupStats singleNodeStats;
    uint32_t singleNodeSector = 0;
    const SectorLookupStatus singleNodeFound = FindSectorForVisual(
        singleNodeHierarchy, 9, 2, 1, singleNodeSector, singleNodeStats,
        reason);
    Expect(singleNodeFound == SectorLookupStatus::Found &&
               singleNodeSector == 0,
           "single-node lookup finds root visual", failures);
    const SectorLookupStatus singleNodeLimit = FindSectorForVisual(
        singleNodeHierarchy, 9, 1, 1, singleNodeSector, singleNodeStats,
        reason);
    Expect(singleNodeLimit == SectorLookupStatus::LimitExceeded &&
               reason == "lookup traversal node budget exceeded: visits=0 "
                         "total=1 limit=1",
           "single-node lookup counts initial visual", failures);

    LevelHierarchy lookupChainHierarchy;
    Expect(Parse(MakeLookupChainLevel(), lookupChainHierarchy, reason),
           "lookup chain hierarchy parsed", failures);
    SectorLookupStats lookupChainStats;
    uint32_t lookupChainSector = 0;
    const SectorLookupStatus lookupChainFound = FindSectorForVisual(
        lookupChainHierarchy, 9, 4, 2, lookupChainSector, lookupChainStats,
        reason);
    Expect(lookupChainFound == SectorLookupStatus::Found &&
               lookupChainSector == 0 &&
               lookupChainStats.indexNodeVisits == 2 &&
               lookupChainStats.traversalNodeVisits == 2 &&
               lookupChainStats.indexEdgeVisits == 1 &&
               lookupChainStats.traversalEdgeVisits == 1 &&
               lookupChainStats.nodeVisits ==
                   lookupChainStats.indexNodeVisits +
                       lookupChainStats.traversalNodeVisits &&
               lookupChainStats.edgeVisits ==
                   lookupChainStats.indexEdgeVisits +
                       lookupChainStats.traversalEdgeVisits,
           "lookup exact budget and stats split", failures);
    const SectorLookupStatus lookupNodeLimit = FindSectorForVisual(
        lookupChainHierarchy, 9, 3, 2, lookupChainSector, lookupChainStats,
        reason);
    Expect(lookupNodeLimit == SectorLookupStatus::LimitExceeded &&
               reason == "lookup traversal node budget exceeded: visits=1 "
                         "total=3 limit=3",
           "lookup reverse node budget exhaustion", failures);
    const SectorLookupStatus lookupEdgeLimit = FindSectorForVisual(
        lookupChainHierarchy, 9, 4, 1, lookupChainSector, lookupChainStats,
        reason);
    Expect(lookupEdgeLimit == SectorLookupStatus::LimitExceeded &&
               reason == "lookup traversal edge budget exceeded: visits=0 "
                         "total=1 limit=1",
           "lookup reverse edge budget exhaustion", failures);
    const SectorLookupStatus lookupIndexNodeLimit = FindSectorForVisual(
        lookupChainHierarchy, 9, 1, 2, lookupChainSector, lookupChainStats,
        reason);
    Expect(lookupIndexNodeLimit == SectorLookupStatus::LimitExceeded &&
               reason == "lookup index node budget exceeded: visits=1 "
                         "total=1 limit=1",
           "lookup index node budget exhaustion", failures);

    const SectorLookupStatus lookupIndexEdgeLimit = FindSectorForVisual(
        twoLeafHierarchy, 101, 10, 1, lookupChainSector, lookupChainStats,
        reason);
    Expect(lookupIndexEdgeLimit == SectorLookupStatus::LimitExceeded &&
               reason == "lookup index edge budget exceeded: visits=1 "
                         "total=1 limit=1",
           "lookup index edge budget exhaustion", failures);

    const SectorLookupStatus nonDrawableLookup = FindSectorForVisual(
        lookupChainHierarchy, 100, 10, 10, lookupChainSector,
        lookupChainStats, reason);
    Expect(nonDrawableLookup == SectorLookupStatus::NotFound &&
               reason == "visual is not a drawable MT_NORMAL node",
           "non-drawable lookup target rejected", failures);

    LevelHierarchy largeHierarchy;
    largeHierarchy.visuals.reserve(64000);
    for (uint32_t id = 1; id <= 64000; ++id)
    {
        VisualNode node;
        node.id = id;
        node.type = kMtNormal;
        node.hasHeader = true;
        largeHierarchy.visuals.push_back(std::move(node));
    }
    Sector largeSector;
    largeSector.index = 0;
    largeSector.rootVisualId = 1;
    largeHierarchy.sectors.push_back(largeSector);
    SectorLookupStats largeLookupStats;
    uint32_t largeSectorId = 0;
    const SectorLookupStatus largeLookup = FindSectorForVisual(
        largeHierarchy, 9, 1, 1, largeSectorId, largeLookupStats, reason);
    Expect(largeLookup == SectorLookupStatus::LimitExceeded &&
               reason == "lookup index node budget exceeded: visits=1 "
                         "total=1 limit=1" &&
               largeLookupStats.indexNodeVisits == 1 &&
               largeLookupStats.traversalNodeVisits == 0,
           "64k lookup index budget exhaustion", failures);
    std::vector<LeafInstance> largeCollectOut;
    CollectStats largeCollectStats;
    const CollectStatus largeCollect = CollectLeaves(
        largeHierarchy, {8, 1, 1}, largeCollectOut, largeCollectStats, reason);
    Expect(largeCollect == CollectStatus::LimitExceeded &&
               reason == "collect index node budget exceeded: visits=1 "
                         "total=1 limit=1" &&
               largeCollectStats.indexNodeVisits == 1 &&
               largeCollectStats.traversalNodeVisits == 0,
           "64k collect index budget exhaustion", failures);

    uint32_t targetSector = 0;
    const std::vector<uint8_t> explosion =
        MakePathExplosionLevel(10, 5, 9, targetSector);
    LevelHierarchy explosionHierarchy;
    Expect(Parse(explosion, explosionHierarchy, reason),
           "path explosion hierarchy parsed", failures);
    std::vector<LeafInstance> boundedExplosion;
    CollectStats boundedExplosionStats;
    const CollectStatus boundedExplosionStatus = CollectLeaves(
        explosionHierarchy, {100000, 1000, 2000}, boundedExplosion,
        boundedExplosionStats, reason);
    Expect(boundedExplosionStatus == CollectStatus::LimitExceeded &&
               boundedExplosionStats.nodeVisits <= 1000 &&
               boundedExplosionStats.edgeVisits <= 2000,
           "adversarial DAG bounded by budget", failures);

    std::vector<LeafInstance> truncatedExplosion;
    CollectStats truncatedExplosionStats;
    const CollectStatus truncatedExplosionStatus = CollectLeaves(
        explosionHierarchy, {100000, 300000, 300000}, truncatedExplosion,
        truncatedExplosionStats, reason);
    Expect(truncatedExplosionStatus == CollectStatus::LimitExceeded &&
               truncatedExplosion.size() == 100000,
           "100k references are not silent success", failures);

    SectorLookupStats lookupStats;
    uint32_t foundSector = 0;
    const SectorLookupStatus lookupStatus = FindSectorForVisual(
        explosionHierarchy, 9, 200000, 400000, foundSector, lookupStats,
        reason);
    Expect(lookupStatus == SectorLookupStatus::Found &&
               foundSector == targetSector && foundSector == 1,
           "target visual found in later sector", failures);
    Expect(lookupStats.nodeVisits < 1000 && lookupStats.edgeVisits < 1000,
           "target lookup is bounded", failures);

    std::vector<LeafInstance> targetLeaves;
    CollectStats targetStats;
    const CollectStatus targetStatus = CollectSectorLeaves(
        explosionHierarchy, targetSector, {8, 1000, 1000}, targetLeaves,
        targetStats, reason);
    Expect(targetStatus == CollectStatus::Complete &&
               targetLeaves.size() == 1 && targetLeaves[0].visualId == 9 &&
               targetLeaves[0].sectorIndex == targetSector,
           "targeted sector collect keeps visual 9 provenance", failures);

    const SectorLookupStatus missingLookup = FindSectorForVisual(
        explosionHierarchy, 123456, 200000, 400000, foundSector, lookupStats,
        reason);
    Expect(missingLookup == SectorLookupStatus::NotFound &&
               reason == "visual not found",
           "missing visual lookup rejected", failures);

    const SectorLookupStatus limitedLookup = FindSectorForVisual(
        explosionHierarchy, 9, 1, 1, foundSector, lookupStats, reason);
    Expect(limitedLookup == SectorLookupStatus::LimitExceeded,
           "lookup budget limit rejected", failures);

    std::vector<LeafInstance> missingSectorLeaves;
    CollectStats missingSectorStats;
    const CollectStatus missingSectorStatus = CollectSectorLeaves(
        explosionHierarchy, 999, {8, 1000, 1000}, missingSectorLeaves,
        missingSectorStats, reason);
    ExpectReason(missingSectorStatus == CollectStatus::InvalidHierarchy,
                 "sector id not found", reason,
                 "targeted collect missing sector rejected", failures);

    return failures == 0;
}
} // namespace levelhierarchy12
