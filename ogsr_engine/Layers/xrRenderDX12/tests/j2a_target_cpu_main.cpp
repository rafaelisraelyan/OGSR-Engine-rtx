#include "GameDx12J2APreparation.h"
#include "dx12levelhierarchy.h"
#include "dx12xraylevelgeometry.h"

#include "j2a_target_cpu_shims/FS.h"

#include <DirectXMath.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
std::vector<uint8_t> g_levelBytes;
std::vector<uint8_t> g_geomBytes;
std::vector<std::string> g_messages;

enum class TargetMode
{
    Error,
    WrongIdentity,
    ZeroCounts,
    ValidNear,
    ValidFar,
};

TargetMode g_targetMode = TargetMode::ValidNear;
unsigned g_readVisualCalls = 0;
bool g_overrideSector = false;
uint32_t g_overrideSectorId = 0;
bool g_prefixMode = false;

void AppendU32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
}

void AppendChunk(std::vector<uint8_t>& out, uint32_t type,
                 const std::vector<uint8_t>& payload)
{
    AppendU32(out, type);
    AppendU32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> MakeHeader(uint8_t type)
{
    std::vector<uint8_t> payload(levelhierarchy12::kOgfHeaderSize, 0);
    payload[0] = 4;
    payload[1] = type;
    payload[2] = 1;
    payload[3] = 0;
    const float minimum = -1.0f;
    const float maximum = 1.0f;
    for (int i = 0; i < 3; ++i)
    {
        std::memcpy(payload.data() + 4 + i * 4, &minimum, sizeof(float));
        std::memcpy(payload.data() + 16 + i * 4, &maximum, sizeof(float));
    }
    return payload;
}

std::vector<uint8_t> MakeVisual(uint8_t type,
                                const std::vector<uint32_t>& children)
{
    std::vector<uint8_t> childrenPayload;
    AppendU32(childrenPayload, static_cast<uint32_t>(children.size()));
    for (const uint32_t child : children)
        AppendU32(childrenPayload, child);

    std::vector<uint8_t> visual;
    AppendChunk(visual, levelhierarchy12::kOgfChildrenLink, childrenPayload);
    AppendChunk(visual, levelhierarchy12::kOgfHeader, MakeHeader(type));
    return visual;
}

std::vector<uint8_t> MakeSector(uint32_t rootVisualId)
{
    std::vector<uint8_t> root;
    AppendU32(root, rootVisualId);
    std::vector<uint8_t> sector;
    AppendChunk(sector, levelhierarchy12::kSectorRootChunk, root);
    return sector;
}

std::vector<uint8_t> MakeLevel()
{
    // Sector 0 is a valid but target-free sector: 100 -> 105/106/107.
    // Sector 1 contains visual 9 next to the same three valid visuals:
    // 200 -> 9/105/106/107. Real lookup must choose sector 1. The CPU fault
    // wrapper can force sector 0 to prove the adapter rejects a lookup
    // result whose collected leaves do not contain the requested target.
    std::vector<uint8_t> visuals;
    AppendChunk(visuals, 100, MakeVisual(levelhierarchy12::kMtHierarchy,
                                         {105, 106, 107}));
    AppendChunk(visuals, 105, MakeVisual(levelhierarchy12::kMtNormal, {}));
    AppendChunk(visuals, 106, MakeVisual(levelhierarchy12::kMtNormal, {}));
    AppendChunk(visuals, 107, MakeVisual(levelhierarchy12::kMtNormal, {}));
    AppendChunk(visuals, 200, MakeVisual(levelhierarchy12::kMtHierarchy,
                                         {9, 105, 106, 107}));
    AppendChunk(visuals, 9, MakeVisual(levelhierarchy12::kMtNormal, {}));

    std::vector<uint8_t> level;
    AppendChunk(level, levelhierarchy12::kLevelVisualsChunk, visuals);

    std::vector<uint8_t> sectors;
    AppendChunk(sectors, 0, MakeSector(100));
    AppendChunk(sectors, 1, MakeSector(200));
    AppendChunk(level, levelhierarchy12::kLevelSectorsChunk, sectors);
    return level;
}

DirectX::XMFLOAT4X4 MakeView()
{
    DirectX::XMFLOAT4X4 result;
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(
        DirectX::XMVectorSet(0.0f, 0.0f, -10.0f, 1.0f),
        DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    DirectX::XMStoreFloat4x4(&result, view);
    return result;
}

DirectX::XMFLOAT4X4 MakeProjection()
{
    DirectX::XMFLOAT4X4 result;
    const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
    DirectX::XMStoreFloat4x4(&result, projection);
    return result;
}

void SetEnvMaxInstances(const char* value)
{
    _putenv_s("OGSR_DX12_J2A_MAX_INSTANCES", value == nullptr ? "" : value);
}

void SetLevelBytes()
{
    g_levelBytes = MakeLevel();
    g_geomBytes = {0xAA, 0xBB, 0xCC, 0xDD};
}

bool StartsWith(const std::string& text, const char* prefix)
{
    if (prefix == nullptr)
        return true;
    const size_t length = std::strlen(prefix);
    return text.size() >= length && text.compare(0, length, prefix) == 0;
}

struct Outcome
{
    bool ok = false;
    GameDx12J2AResult12 result;
    unsigned readVisualCalls = 0;
};

Outcome RunOnce(TargetMode mode, bool overrideSector, uint32_t overrideSectorId,
                const char* maxInstances)
{
    g_targetMode = mode;
    g_overrideSector = overrideSector;
    g_overrideSectorId = overrideSectorId;
    g_readVisualCalls = 0;
    g_messages.clear();
    SetLevelBytes();
    SetEnvMaxInstances(maxInstances);

    GameDx12J2AResult12 result;
    const DirectX::XMFLOAT4X4 view = MakeView();
    const DirectX::XMFLOAT4X4 projection = MakeProjection();
    const bool ok = GameDx12J2A_Run("j2a_target_cpu", 1, 1, view, projection,
                                   result);

    SetEnvMaxInstances(nullptr);
    Outcome outcome;
    outcome.ok = ok;
    outcome.result = result;
    outcome.readVisualCalls = g_readVisualCalls;
    return outcome;
}

bool Check(const char* name, const Outcome& outcome, bool expectOk,
           const char* expectedReason, uint32_t expectedInstances,
           uint32_t expectedPackets, uint32_t expectedVisible,
           uint32_t expectedCulled, unsigned expectedReadVisualCalls,
           uint32_t expectedInstanceLimit)
{
    bool ok = true;
    if (outcome.ok != expectOk)
        ok = false;
    if (!StartsWith(outcome.result.reason, expectedReason))
        ok = false;
    if (outcome.result.instances != expectedInstances)
        ok = false;
    if (outcome.result.packets != expectedPackets)
        ok = false;
    if (outcome.result.visiblePackets != expectedVisible)
        ok = false;
    if (outcome.result.culledPackets != expectedCulled)
        ok = false;
    if (outcome.readVisualCalls != expectedReadVisualCalls)
        ok = false;
    if (outcome.result.collectMaxInstances != expectedInstanceLimit)
        ok = false;

    // A rejected target must never expose a partial snapshot or a prepared
    // frame. The production result struct is the only publication surface
    // used by the owner thread.
    if (!expectOk)
    {
        if (outcome.result.ok || outcome.result.stableHash != 0 ||
            outcome.result.packets != 0 || outcome.result.visiblePackets != 0 ||
            outcome.result.batches != 0 || outcome.result.lists != 0 ||
            outcome.result.prepareMs != 0.0)
            ok = false;
    }

    std::printf(
        "CASE %-28s status=%s ok=%d reason='%s' instances=%u packets=%u "
        "visible=%u culled=%u readvisual_calls=%u prepare_ms=%.3f "
        "instance_limit=%u\n",
        name, ok ? "PASS" : "FAIL", outcome.ok ? 1 : 0,
        outcome.result.reason.c_str(), outcome.result.instances,
        outcome.result.packets, outcome.result.visiblePackets,
        outcome.result.culledPackets, outcome.readVisualCalls,
        outcome.result.prepareMs, outcome.result.collectMaxInstances);
    if (!ok)
    {
        for (const std::string& message : g_messages)
            std::printf("  msg: %s\n", message.c_str());
    }
    return ok;
}
} // namespace

void Msg(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (written < 0)
        return;
    buffer[sizeof(buffer) - 1] = '\0';
    g_messages.emplace_back(buffer);
}

FSShim FS;

IReader* FSShim::r_open(fsgame::ESearchPath, const char* name)
{
    const std::vector<uint8_t>* bytes = nullptr;
    if (std::strcmp(name, fsgame::level_files::level) == 0)
        bytes = &g_levelBytes;
    else if (std::strcmp(name, fsgame::level_files::level_geom) == 0)
        bytes = &g_geomBytes;
    if (bytes == nullptr || bytes->empty())
        return nullptr;
    return new IReader(bytes->data(), bytes->size());
}

void FSShim::r_close(IReader* reader)
{
    delete reader;
}

namespace xraylevel12
{
const char* ReadErrorMessage(ReadError error) noexcept
{
    return error == ReadError::None ? "" : "injected ReadVisual failure";
}

void Geometry::clear() noexcept
{
    vertex_bytes.clear();
    indices.clear();
}

ReadError ReadVisual(std::span<const uint8_t> /*levelBytes*/,
                     std::span<const uint8_t> /*geomBytes*/, uint32_t visualId,
                     Geometry& output, std::string& reason,
                     const Limits& /*limits*/) noexcept
{
    ++g_readVisualCalls;
    reason.clear();
    output = Geometry{};

    if (visualId == 9 && g_targetMode == TargetMode::Error)
    {
        reason = "injected ReadVisual failure for visual 9";
        return ReadError::UnsupportedVisualType;
    }

    output.visual_id =
        (visualId == 9 && g_targetMode == TargetMode::WrongIdentity) ? 9009u
                                                                    : visualId;
    output.index_count = 3;
    output.vertex_count = 3;
    output.first_index = 0;
    output.base_vertex = 0;

    if (visualId == 9 && g_targetMode == TargetMode::ZeroCounts)
    {
        output.index_count = 0;
        output.vertex_count = 0;
    }

    const bool farTarget =
        visualId == 9 && g_targetMode == TargetMode::ValidFar;
    const float minimum = farTarget ? 10000.0f : -1.0f;
    const float maximum = farTarget ? 10001.0f : 1.0f;
    for (int i = 0; i < 3; ++i)
    {
        output.bounds.minimum[i] = minimum;
        output.bounds.maximum[i] = maximum;
        output.sphere.center[i] = (minimum + maximum) * 0.5f;
    }
    output.sphere.radius = farTarget ? 1.0f : 2.0f;
    return ReadError::None;
}
} // namespace xraylevel12

namespace levelhierarchy12
{
SectorLookupStatus FindSectorForVisualFault(
    const LevelHierarchy& hierarchy, uint32_t visualId, uint32_t maxNodeVisits,
    uint32_t maxEdgeVisits, uint32_t& sectorId, SectorLookupStats& stats,
    std::string& reason)
{
    const SectorLookupStatus status =
        FindSectorForVisual(hierarchy, visualId, maxNodeVisits, maxEdgeVisits,
                            sectorId, stats, reason);
    if (status == SectorLookupStatus::Found && g_overrideSector)
        sectorId = g_overrideSectorId;
    return status;
}
} // namespace levelhierarchy12

int main(int argc, char** argv)
{
    g_prefixMode = argc > 1 && std::strcmp(argv[1], "--prefix") == 0;
    bool allOk = true;

    const uint32_t defaultLimit = 24u;
    const uint32_t bugInstances = g_prefixMode ? 3u : 0u;
    const uint32_t bugPackets = g_prefixMode ? 3u : 0u;
    const bool bugExpected = g_prefixMode;

    {
        const Outcome outcome =
            RunOnce(TargetMode::Error, false, 0, nullptr);
        allOk &= Check("readvisual_error", outcome, bugExpected,
                       bugExpected ? "" : "target visual 9 was not validated",
                       bugInstances, bugPackets, bugPackets, 0, 4,
                       defaultLimit);
    }
    {
        const Outcome outcome =
            RunOnce(TargetMode::WrongIdentity, false, 0, nullptr);
        allOk &= Check("wrong_geometry_identity", outcome, bugExpected,
                       bugExpected ? "" : "target visual 9 was not validated",
                       bugInstances, bugPackets, bugPackets, 0, 4,
                       defaultLimit);
    }
    {
        const Outcome outcome =
            RunOnce(TargetMode::ZeroCounts, false, 0, nullptr);
        allOk &= Check("zero_geometry_counts", outcome, bugExpected,
                       bugExpected ? "" : "target visual 9 was not validated",
                       bugInstances, bugPackets, bugPackets, 0, 4,
                       defaultLimit);
    }
    {
        const Outcome outcome =
            RunOnce(TargetMode::ValidNear, false, 0, nullptr);
        allOk &= Check("valid_visible", outcome, true, "", 4, 4, 4, 0, 4,
                       defaultLimit);
    }
    {
        const Outcome outcome =
            RunOnce(TargetMode::ValidFar, false, 0, nullptr);
        allOk &= Check("valid_outside_frustum", outcome, true, "", 4, 3, 3,
                       1, 4, defaultLimit);
    }
    {
        const Outcome outcome =
            RunOnce(TargetMode::ValidNear, false, 0, "4");
        allOk &= Check("exact_capacity_four", outcome, true, "", 4, 4, 4, 0,
                       4, 4);
    }
    {
        const Outcome outcome =
            RunOnce(TargetMode::ValidNear, false, 0, "3");
        allOk &= Check("below_capacity_three", outcome, false,
                       "sector collect failed: collect instance limit "
                       "exceeded",
                       0, 0, 0, 0, 0, 3);
    }
    {
        const Outcome outcome =
            RunOnce(TargetMode::ValidNear, false, 0, "1");
        allOk &= Check("collect_limit_one", outcome, false,
                       "sector collect failed: collect instance limit "
                       "exceeded",
                       0, 0, 0, 0, 0, 1);
    }
    {
        const Outcome outcome =
            RunOnce(TargetMode::ValidNear, true, 0, nullptr);
        allOk &= Check("target_absent", outcome, false,
                       "collected sector does not contain visual 9", 0, 0, 0,
                       0, 0, defaultLimit);
    }

    GameDx12J2A_Shutdown();
    std::printf("J2A_TARGET_CPU_RESULT %s mode=%s cases=9\n",
                allOk ? "PASS" : "FAIL", g_prefixMode ? "prefix-r4" : "r5");
    return allOk ? 0 : 1;
}
