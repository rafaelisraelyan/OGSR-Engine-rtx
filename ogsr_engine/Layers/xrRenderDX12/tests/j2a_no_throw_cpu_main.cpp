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
#include <stdexcept>
#include <string>
#include <vector>

#ifndef OGSR_J2A_R7_PREFIX
#define R7_ERR(e) static_cast<unsigned>(GameDx12J2AError12::e)
#else
#define R7_ERR(e) 0u
#endif

namespace
{
std::vector<uint8_t> g_levelBytes;
std::vector<uint8_t> g_geomBytes;
bool g_persistentOom = false;
unsigned g_failAfter = 0;
unsigned g_injectedAllocations = 0;
unsigned g_passLines = 0;
unsigned g_failLines = 0;
unsigned g_detailLines = 0;
unsigned g_detailWithResult = 0;
unsigned g_messageCount = 0;

enum class MsgMode
{
    None,
    ThrowAll,
    ThrowOnTerminal,
    ThrowOnException,
    ThrowOnDetail,
};

MsgMode g_msgMode = MsgMode::None;

enum class FaultMode
{
    None,
    PersistentOomBeforeReserve,
    LongWhatBeforeCommit,
    ShortWhatBeforeCommit,
};

FaultMode g_faultMode = FaultMode::None;
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

class LongAllocationFailure : public std::bad_alloc
{
public:
    const char* what() const noexcept override
    {
        return "injected allocation failure with a deliberately long diagnostic "
               "text that cannot fit into the small-string buffer and therefore "
               "requires dynamic allocation while the process is already under "
               "persistent memory pressure; this string is intentionally longer "
               "than one hundred and twenty eight characters to force the "
               "reason assignment path to allocate and fail";
    }
};

void ResetCounters()
{
    g_passLines = 0;
    g_failLines = 0;
    g_detailLines = 0;
    g_detailWithResult = 0;
    g_messageCount = 0;
    g_injectedAllocations = 0;
    g_readVisualCalls = 0;
    g_failAfter = 0;
    g_persistentOom = false;
    g_msgMode = MsgMode::None;
    g_faultMode = FaultMode::None;
    g_targetError = false;
}

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

bool Contains(const char* text, const char* needle)
{
    return std::strstr(text, needle) != nullptr;
}

struct Outcome
{
    bool escaped = false;
    bool returned = false;
    GameDx12J2AResult12 result;
    unsigned passLines = 0;
    unsigned failLines = 0;
    unsigned detailLines = 0;
    unsigned detailWithResult = 0;
    unsigned readVisualCalls = 0;
};

Outcome RunOnce(FaultMode faultMode, bool targetError,
                const char* maxInstances, MsgMode msgMode = MsgMode::None)
{
    ResetCounters();
    g_faultMode = faultMode;
    g_targetError = targetError;
    g_msgMode = msgMode;
    g_levelBytes = MakeLevel();
    g_geomBytes = {0xAA, 0xBB, 0xCC, 0xDD};
    SetEnvMaxInstances(maxInstances);

    Outcome outcome;
    try
    {
        outcome.returned = GameDx12J2A_Run(
            "j2a_r7_tx", 1, 1, MakeView(), MakeProjection(), outcome.result);
    }
    catch (...)
    {
        outcome.escaped = true;
    }

    g_persistentOom = false;
    g_failAfter = 0;
    g_msgMode = MsgMode::None;
    g_faultMode = FaultMode::None;
    SetEnvMaxInstances(nullptr);

    outcome.passLines = g_passLines;
    outcome.failLines = g_failLines;
    outcome.detailLines = g_detailLines;
    outcome.detailWithResult = g_detailWithResult;
    outcome.readVisualCalls = g_readVisualCalls;
    return outcome;
}

#ifndef OGSR_J2A_R7_PREFIX
Outcome RunBridgeOnce(FaultMode faultMode, bool targetError,
                      const char* maxInstances,
                      MsgMode msgMode = MsgMode::None)
{
    ResetCounters();
    g_faultMode = faultMode;
    g_targetError = targetError;
    g_msgMode = msgMode;
    g_levelBytes = MakeLevel();
    g_geomBytes = {0xAA, 0xBB, 0xCC, 0xDD};
    SetEnvMaxInstances(maxInstances);

    Outcome outcome;
    try
    {
        outcome.returned = GameDx12J2A_RunForBridge(
            "j2a_r7_bridge", 1, 1, MakeView(), MakeProjection(),
            outcome.result);
    }
    catch (...)
    {
        outcome.escaped = true;
    }

    g_persistentOom = false;
    g_failAfter = 0;
    g_msgMode = MsgMode::None;
    g_faultMode = FaultMode::None;
    SetEnvMaxInstances(nullptr);

    outcome.passLines = g_passLines;
    outcome.failLines = g_failLines;
    outcome.detailLines = g_detailLines;
    outcome.detailWithResult = g_detailWithResult;
    outcome.readVisualCalls = g_readVisualCalls;
    return outcome;
}
#endif

bool SuccessOnlyFieldsZero(const GameDx12J2AResult12& result)
{
    return result.instances == 0 && result.uniqueVisuals == 0 &&
           result.packets == 0 && result.visiblePackets == 0 &&
           result.culledPackets == 0 && result.batches == 0 &&
           result.lists == 0 && result.stableHash == 0 &&
           result.serialMs == 0.0 && result.parallelMs == 0.0 &&
           result.verifyMs == 0.0;
}

bool CheckSuccess(const char* name, const Outcome& outcome)
{
    bool ok = !outcome.escaped && outcome.returned && outcome.result.ok;
    ok &= outcome.result.instances == 4;
    ok &= outcome.result.uniqueVisuals == 4;
    ok &= outcome.result.packets == 4;
    ok &= outcome.result.visiblePackets == 4;
    ok &= outcome.result.culledPackets == 0;
    ok &= outcome.result.batches > 0;
    ok &= outcome.result.lists > 0;
    ok &= outcome.result.stableHash != 0;
    ok &= outcome.passLines == 1;
    ok &= outcome.failLines == 0;
    ok &= outcome.detailLines == 0;
#ifndef OGSR_J2A_R7_PREFIX
    ok &= outcome.result.error == GameDx12J2AError12::None;
#endif
    std::printf("R7_CASE %-30s status=%s escaped=%d ok=%d result_ok=%d "
                "packets=%u hash=%016llX pass=%u fail=%u detail=%u\n",
                name, ok ? "PASS" : "FAIL", outcome.escaped ? 1 : 0,
                outcome.returned ? 1 : 0, outcome.result.ok ? 1 : 0,
                outcome.result.packets,
                static_cast<unsigned long long>(outcome.result.stableHash),
                outcome.passLines, outcome.failLines, outcome.detailLines);
    return ok;
}

bool CheckFailure(const char* name, const Outcome& outcome,
                  bool expectEscapeInPrefix, unsigned maxFailLines,
                  unsigned expectedError)
{
    bool ok = true;
    if (g_prefixMode && expectEscapeInPrefix)
    {
        ok &= outcome.escaped;
        ok &= !outcome.result.ok;
        ok &= outcome.passLines == 0;
    }
    else
    {
        ok &= !outcome.escaped;
        ok &= !outcome.returned && !outcome.result.ok;
        ok &= SuccessOnlyFieldsZero(outcome.result);
        ok &= outcome.passLines == 0;
        ok &= outcome.failLines <= maxFailLines;
    }
#ifndef OGSR_J2A_R7_PREFIX
    if (!g_prefixMode &&
        static_cast<unsigned>(outcome.result.error) != expectedError)
        ok = false;
#endif
    std::printf("R7_CASE %-30s status=%s escaped=%d ok=%d result_ok=%d "
                "instances=%u packets=%u hash=%016llX pass=%u fail=%u "
                "detail=%u detail_result=%u\n",
                name,
                ok ? (g_prefixMode && expectEscapeInPrefix ? "BUG_REPRODUCED"
                                                           : "PASS")
                   : "FAIL",
                outcome.escaped ? 1 : 0, outcome.returned ? 1 : 0,
                outcome.result.ok ? 1 : 0, outcome.result.instances,
                outcome.result.packets,
                static_cast<unsigned long long>(outcome.result.stableHash),
                outcome.passLines, outcome.failLines, outcome.detailLines,
                outcome.detailWithResult);
    return ok;
}

bool CheckTerminalOom(const char* name, const Outcome& outcome,
                      bool expectEscapeInPrefix)
{
    if (g_prefixMode && expectEscapeInPrefix)
    {
        const bool ok = outcome.escaped && outcome.result.ok;
        std::printf("R7_CASE %-30s status=%s escaped=%d result_ok=%d "
                    "pass=%u fail=%u\n",
                    name, ok ? "BUG_REPRODUCED" : "FAIL",
                    outcome.escaped ? 1 : 0, outcome.result.ok ? 1 : 0,
                    outcome.passLines, outcome.failLines);
        return ok;
    }

    bool ok = !outcome.escaped && outcome.returned && outcome.result.ok;
    ok &= outcome.result.instances == 4 && outcome.result.packets == 4;
    ok &= outcome.result.stableHash != 0;
    ok &= outcome.passLines == 0 && outcome.failLines == 0;
#ifndef OGSR_J2A_R7_PREFIX
    ok &= outcome.result.error == GameDx12J2AError12::None;
#endif
    std::printf("R7_CASE %-30s status=%s escaped=%d result_ok=%d packets=%u "
                "hash=%016llX pass=%u fail=%u\n",
                name, ok ? "PASS" : "FAIL", outcome.escaped ? 1 : 0,
                outcome.result.ok ? 1 : 0, outcome.result.packets,
                static_cast<unsigned long long>(outcome.result.stableHash),
                outcome.passLines, outcome.failLines);
    return ok;
}

bool CheckBridgeFailure(const char* name, const Outcome& outcome,
                        unsigned expectedDetailLines, unsigned maxFailLines,
                        unsigned expectedError)
{
    bool ok = !outcome.escaped;
    ok &= !outcome.returned && !outcome.result.ok;
    ok &= SuccessOnlyFieldsZero(outcome.result);
    ok &= outcome.passLines == 0;
    ok &= outcome.failLines <= maxFailLines;
    ok &= outcome.detailLines == expectedDetailLines;
    ok &= outcome.detailWithResult == 0;
#ifndef OGSR_J2A_R7_PREFIX
    ok &= static_cast<unsigned>(outcome.result.error) == expectedError;
#endif
    std::printf("R7_CASE %-30s status=%s escaped=%d result_ok=%d pass=%u "
                "fail=%u detail=%u detail_result=%u\n",
                name, ok ? "PASS" : "FAIL", outcome.escaped ? 1 : 0,
                outcome.result.ok ? 1 : 0, outcome.passLines,
                outcome.failLines, outcome.detailLines,
                outcome.detailWithResult);
    return ok;
}

bool CheckReuse(const char* name)
{
    ResetCounters();
    g_faultMode = FaultMode::PersistentOomBeforeReserve;
    g_levelBytes = MakeLevel();
    g_geomBytes = {0xAA, 0xBB, 0xCC, 0xDD};

    GameDx12J2AResult12 shared;
    try
    {
        (void)GameDx12J2A_Run("j2a_r7_reuse_fail", 1, 1, MakeView(),
                              MakeProjection(), shared);
    }
    catch (...)
    {
    }

    g_persistentOom = false;
    g_msgMode = MsgMode::None;
    g_faultMode = FaultMode::None;
    ResetCounters();
    g_levelBytes = MakeLevel();
    g_geomBytes = {0xAA, 0xBB, 0xCC, 0xDD};

    bool returned = false;
    try
    {
        returned = GameDx12J2A_Run("j2a_r7_reuse_ok", 1, 1, MakeView(),
                                   MakeProjection(), shared);
    }
    catch (...)
    {
        returned = false;
    }

    bool ok = returned && shared.ok && shared.instances == 4 &&
              shared.packets == 4 && shared.stableHash != 0 &&
              g_passLines == 1 && g_failLines == 0;
#ifndef OGSR_J2A_R7_PREFIX
    ok &= shared.error == GameDx12J2AError12::None;
#endif
    std::printf("R7_CASE %-30s status=%s returned=%d result_ok=%d "
                "packets=%u hash=%016llX pass=%u fail=%u\n",
                name, ok ? "PASS" : "FAIL", returned ? 1 : 0,
                shared.ok ? 1 : 0, shared.packets,
                static_cast<unsigned long long>(shared.stableHash),
                g_passLines, g_failLines);
    return ok;
}
} // namespace

void Msg(const char* format, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (written < 0)
        return;
    buffer[sizeof(buffer) - 1] = '\0';
    ++g_messageCount;

    bool willThrow = false;
    switch (g_msgMode)
    {
    case MsgMode::ThrowAll:
        willThrow = true;
        break;
    case MsgMode::ThrowOnTerminal:
        willThrow = Contains(buffer, "result=");
        break;
    case MsgMode::ThrowOnException:
        willThrow = Contains(buffer, "exception");
        break;
    case MsgMode::ThrowOnDetail:
        willThrow = Contains(buffer, "failure_detail");
        break;
    case MsgMode::None:
        break;
    }
    if (willThrow)
        throw std::bad_alloc();

    if (Contains(buffer, "result=PASS"))
        ++g_passLines;
    if (Contains(buffer, "result=FAIL"))
        ++g_failLines;
    if (Contains(buffer, "failure_detail"))
    {
        ++g_detailLines;
        if (Contains(buffer, "result="))
            ++g_detailWithResult;
    }
}

void* operator new(std::size_t size)
{
    if (g_persistentOom)
    {
        ++g_injectedAllocations;
        throw std::bad_alloc();
    }
    if (g_failAfter != 0 && --g_failAfter == 0)
    {
        ++g_injectedAllocations;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return ::operator new(size, std::nothrow);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

namespace j2a_r6_fault
{
void MaybeThrow(Point point)
{
    if (g_faultMode == FaultMode::PersistentOomBeforeReserve &&
        point == Point::BeforePacketIdsReserve)
    {
        g_persistentOom = true;
        g_msgMode = MsgMode::ThrowAll;
        throw InjectedAllocationFailure();
    }
    if (g_faultMode == FaultMode::LongWhatBeforeCommit &&
        point == Point::BeforeCommit)
    {
        g_persistentOom = true;
        g_msgMode = MsgMode::ThrowAll;
        throw LongAllocationFailure();
    }
    if (g_faultMode == FaultMode::ShortWhatBeforeCommit &&
        point == Point::BeforeCommit)
    {
        g_msgMode = MsgMode::ThrowOnException;
        throw std::runtime_error("short injected failure");
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

    allOk &= CheckSuccess("normal_success", RunOnce(FaultMode::None, false,
                                                    nullptr));
    allOk &= CheckFailure(
        "persistent_oom_reserve",
        RunOnce(FaultMode::PersistentOomBeforeReserve, false, nullptr),
        true, 0, R7_ERR(OutOfMemory));
    allOk &= CheckFailure(
        "long_what_reason_oom",
        RunOnce(FaultMode::LongWhatBeforeCommit, false, nullptr), true, 0,
        R7_ERR(OutOfMemory));
    allOk &= CheckFailure(
        "short_what_msg_oom",
        RunOnce(FaultMode::ShortWhatBeforeCommit, false, nullptr), true, 1,
        R7_ERR(Exception));
    allOk &= CheckFailure("readvisual_error",
                          RunOnce(FaultMode::None, true, nullptr), false, 1,
                          R7_ERR(Target));
    allOk &= CheckFailure("collect_limit", RunOnce(FaultMode::None, false, "1"),
                          false, 1, R7_ERR(Collect));
    allOk &= CheckTerminalOom(
        "terminal_oom",
        RunOnce(FaultMode::None, false, nullptr, MsgMode::ThrowOnTerminal),
        false);
    allOk &= CheckReuse("repeated_after_failure");

#ifndef OGSR_J2A_R7_PREFIX
    allOk &= CheckSuccess("bridge_success",
                          RunBridgeOnce(FaultMode::None, false, nullptr));
    allOk &= CheckBridgeFailure(
        "bridge_readvisual_error",
        RunBridgeOnce(FaultMode::None, true, nullptr), 1, 1,
        R7_ERR(Target));
    allOk &= CheckBridgeFailure(
        "bridge_persistent_oom",
        RunBridgeOnce(FaultMode::PersistentOomBeforeReserve, false, nullptr),
        0, 0, R7_ERR(OutOfMemory));
    allOk &= CheckBridgeFailure(
        "bridge_detail_oom",
        RunBridgeOnce(FaultMode::None, true, nullptr,
                      MsgMode::ThrowOnDetail),
        0, 1, R7_ERR(Target));
#endif

    GameDx12J2A_Shutdown();
    std::printf("J2A_NO_THROW_CPU_RESULT %s mode=%s cases=%d\n",
                allOk ? "PASS" : "FAIL",
                g_prefixMode ? "prefix-r6" : "r7",
#ifndef OGSR_J2A_R7_PREFIX
                12
#else
                8
#endif
    );
    return allOk ? 0 : 1;
}
