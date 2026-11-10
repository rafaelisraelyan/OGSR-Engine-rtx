#include "GameDx12J2APreparation.h"
#include "dx12levelhierarchy.h"
#include "dx12xraylevelgeometry.h"

#include "j2a_r6_fault.h"
#include "j2a_target_cpu_shims/FS.h"

#include <DirectXMath.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace
{
std::vector<uint8_t> g_levelBytes;
std::vector<uint8_t> g_geomBytes;
std::vector<std::string> g_messages;
j2a_r6_fault::Point g_faultPoint = j2a_r6_fault::Point::None;
bool g_targetError = false;
unsigned g_readVisualCalls = 0;
bool g_prefixMode = false;

class InjectedAllocationFailure : public std::bad_alloc
{
public:
    const char* what() const noexcept override
    {
        return "injected allocation failure";
    }
};

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

bool Contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

int CountOccurrences(const std::vector<std::string>& messages,
                     const char* needle)
{
    int count = 0;
    for (const std::string& message : messages)
        if (Contains(message, needle))
            ++count;
    return count;
}

struct Outcome
{
    bool ok = false;
    GameDx12J2AResult12 result;
    unsigned readVisualCalls = 0;
    std::vector<std::string> messages;
};

Outcome RunOnce(j2a_r6_fault::Point faultPoint, bool targetError,
                const char* maxInstances)
{
    g_faultPoint = faultPoint;
    g_targetError = targetError;
    g_readVisualCalls = 0;
    g_messages.clear();
    g_levelBytes = MakeLevel();
    g_geomBytes = {0xAA, 0xBB, 0xCC, 0xDD};
    SetEnvMaxInstances(maxInstances);

    GameDx12J2AResult12 result;
    const bool ok = GameDx12J2A_Run("j2a_r6_tx", 1, 1, MakeView(),
                                   MakeProjection(), result);

    SetEnvMaxInstances(nullptr);
    g_faultPoint = j2a_r6_fault::Point::None;

    Outcome outcome;
    outcome.ok = ok;
    outcome.result = result;
    outcome.readVisualCalls = g_readVisualCalls;
    outcome.messages = g_messages;
    return outcome;
}

bool SuccessOnlyFieldsZero(const GameDx12J2AResult12& result)
{
    return result.instances == 0 && result.uniqueVisuals == 0 &&
           result.packets == 0 && result.visiblePackets == 0 &&
           result.culledPackets == 0 && result.batches == 0 &&
           result.lists == 0 && result.stableHash == 0 &&
           result.serialMs == 0.0 && result.parallelMs == 0.0 &&
           result.verifyMs == 0.0;
}

bool CheckSuccess(const char* name, const Outcome& outcome, int expectedPasses)
{
    bool ok = outcome.ok && outcome.result.ok;
    ok &= outcome.result.instances == 4;
    ok &= outcome.result.uniqueVisuals == 4;
    ok &= outcome.result.packets == 4;
    ok &= outcome.result.visiblePackets == 4;
    ok &= outcome.result.culledPackets == 0;
    ok &= outcome.result.batches > 0;
    ok &= outcome.result.lists > 0;
    ok &= outcome.result.stableHash != 0;
    ok &= CountOccurrences(outcome.messages, "result=PASS") == expectedPasses;
    ok &= CountOccurrences(outcome.messages, "result=FAIL") == 0;

    std::printf(
        "TX_CASE %-28s status=%s ok=%d result_ok=%d instances=%u packets=%u "
        "hash=%016llX pass_lines=%d\n",
        name, ok ? "PASS" : "FAIL", outcome.ok ? 1 : 0,
        outcome.result.ok ? 1 : 0, outcome.result.instances,
        outcome.result.packets,
        static_cast<unsigned long long>(outcome.result.stableHash),
        CountOccurrences(outcome.messages, "result=PASS"));
    if (!ok)
        for (const std::string& message : outcome.messages)
            std::printf("  msg: %s\n", message.c_str());
    return ok;
}

bool CheckFailure(const char* name, const Outcome& outcome,
                  const char* expectedReason, unsigned expectedReadVisualCalls,
                  bool requireSectorProvenance, bool prefixDirtyAllowed)
{
    bool ok = !outcome.ok && !outcome.result.ok;
    ok &= Contains(outcome.result.reason, expectedReason);
    ok &= outcome.readVisualCalls == expectedReadVisualCalls;
    if (!prefixDirtyAllowed)
        ok &= CountOccurrences(outcome.messages, "result=PASS") == 0;
    else
        ok &= Contains(outcome.result.reason, "injected allocation failure");
    if (requireSectorProvenance)
    {
        ok &= outcome.result.sectorIndex == 1;
        ok &= outcome.result.sectorRoot == 200;
    }

    if (!prefixDirtyAllowed)
    {
        ok &= SuccessOnlyFieldsZero(outcome.result);
    }
    else
    {
        // R5 committed success metadata before its fallible diagnostics. The
        // prefix copy must show that dirty state to prove the bug existed.
        ok &= (outcome.result.instances == 4 ||
               outcome.result.uniqueVisuals == 4);
        if (outcome.result.packets == 4)
            ok &= outcome.result.stableHash != 0;
    }

    std::printf(
        "TX_CASE %-28s status=%s ok=%d result_ok=%d instances=%u unique=%u "
        "packets=%u hash=%016llX pass_lines=%d reason='%s'\n",
        name, ok ? (prefixDirtyAllowed ? "BUG_REPRODUCED" : "PASS") : "FAIL",
        outcome.ok ? 1 : 0, outcome.result.ok ? 1 : 0,
        outcome.result.instances, outcome.result.uniqueVisuals,
        outcome.result.packets,
        static_cast<unsigned long long>(outcome.result.stableHash),
        CountOccurrences(outcome.messages, "result=PASS"),
        outcome.result.reason.c_str());
    if (!ok)
        for (const std::string& message : outcome.messages)
            std::printf("  msg: %s\n", message.c_str());
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

namespace j2a_r6_fault
{
void MaybeThrow(Point point)
{
    if (g_faultPoint == point)
    {
        g_faultPoint = Point::None;
        throw InjectedAllocationFailure();
    }
}
} // namespace j2a_r6_fault

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
}void Geometry::clear() noexcept
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
    if (visualId == 9 && g_targetError)
    {
        reason = "injected ReadVisual failure for visual 9";
        return ReadError::UnsupportedVisualType;
    }

    output.visual_id = visualId;
    output.index_count = 3;
    output.vertex_count = 3;
    output.first_index = 0;
    output.base_vertex = 0;
    for (int i = 0; i < 3; ++i)
    {
        output.bounds.minimum[i] = -1.0f;
        output.bounds.maximum[i] = 1.0f;
        output.sphere.center[i] = 0.0f;
    }
    output.sphere.radius = 2.0f;
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
    return FindSectorForVisual(hierarchy, visualId, maxNodeVisits,
                               maxEdgeVisits, sectorId, stats, reason);
}
} // namespace levelhierarchy12

int main(int argc, char** argv)
{
    g_prefixMode = argc > 1 && std::strcmp(argv[1], "--prefix") == 0;
    bool allOk = true;

    const bool prefix = g_prefixMode;
    const int successPassLines = prefix ? 2 : 1;

    allOk &= CheckSuccess("normal_success", RunOnce(j2a_r6_fault::Point::None,
                                                   false, nullptr),
                          successPassLines);

    if (prefix)
    {
        allOk &= CheckFailure(
            "before_reserve", RunOnce(
                j2a_r6_fault::Point::BeforePacketIdsReserve, false, nullptr),
            "injected allocation failure", 4, true, true);
        allOk &= CheckFailure(
            "at_insert", RunOnce(
                j2a_r6_fault::Point::AtPacketIdsInsert, false, nullptr),
            "injected allocation failure", 4, true, true);
        allOk &= CheckFailure(
            "before_commit", RunOnce(j2a_r6_fault::Point::BeforeCommit, false,
                                     nullptr),
            "injected allocation failure", 4, true, true);
    }
    else
    {
        allOk &= CheckFailure(
            "before_reserve", RunOnce(
                j2a_r6_fault::Point::BeforePacketIdsReserve, false, nullptr),
            "injected allocation failure", 4, true, false);
        allOk &= CheckFailure(
            "at_insert", RunOnce(
                j2a_r6_fault::Point::AtPacketIdsInsert, false, nullptr),
            "injected allocation failure", 4, true, false);
        allOk &= CheckFailure(
            "before_commit", RunOnce(j2a_r6_fault::Point::BeforeCommit, false,
                                     nullptr),
            "injected allocation failure", 4, true, false);
    }

    allOk &= CheckFailure("readvisual_target_error",
                          RunOnce(j2a_r6_fault::Point::None, true, nullptr),
                          "target visual 9 was not validated", 4, true, false);
    allOk &= CheckFailure("collect_limit_one",
                          RunOnce(j2a_r6_fault::Point::None, false, "1"),
                          "sector collect failed: collect instance limit "
                          "exceeded",
                          0, true, false);

    GameDx12J2A_Shutdown();
    std::printf("J2A_TRANSACTION_CPU_RESULT %s mode=%s cases=6\n",
                allOk ? "PASS" : "FAIL",
                prefix ? "prefix-r5" : "r6");
    return allOk ? 0 : 1;
}
