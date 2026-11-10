#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include <DirectXMath.h>

#include <cstdint>
#include <string>

// F4.0-J2A: opt-in CPU-only multiple-visual preparation.
//
// The owner/game thread reads the real level container, traverses the
// sector/root/child hierarchy, validates each selected OGF through
// xraylevel12::ReadVisual, copies a bounded immutable snapshot and runs the
// existing renderprep12 / GR Job System in Serial, Parallel and Verify modes.
// No DX12 resource is created and no command list is recorded here.
enum class GameDx12J2AError12 : uint32_t
{
    None = 0,
    InvalidArguments = 1,
    JobSystem = 2,
    Vfs = 3,
    Parse = 4,
    Lookup = 5,
    Collect = 6,
    Target = 7,
    Geometry = 8,
    Prepare = 9,
    Exception = 10,
    OutOfMemory = 11,
    Unknown = 12,
};

struct GameDx12J2AResult12
{
    bool ok = false;
    GameDx12J2AError12 error = GameDx12J2AError12::None;
    uint32_t sectorIndex = 0;
    uint32_t sectorRoot = 0;
    uint32_t instances = 0;
    uint32_t uniqueVisuals = 0;
    uint32_t packets = 0;
    uint32_t visiblePackets = 0;
    uint32_t culledPackets = 0;
    uint32_t batches = 0;
    uint32_t lists = 0;
    uint64_t stableHash = 0;
    double serialMs = 0.0;
    double parallelMs = 0.0;
    double verifyMs = 0.0;
    double vfsMs = 0.0;
    double parseMs = 0.0;
    double lookupMs = 0.0;
    double collectMs = 0.0;
    double readVisualMs = 0.0;
    double prepareMs = 0.0;
    double totalMs = 0.0;
    uint64_t lookupNodeVisits = 0;
    uint64_t lookupEdgeVisits = 0;
    uint64_t collectNodeVisits = 0;
    uint64_t collectEdgeVisits = 0;
    uint32_t collectMaxNodeVisits = 0;
    uint32_t collectMaxEdgeVisits = 0;
    uint32_t collectMaxInstances = 0;
    std::string reason;
};

bool GameDx12J2A_IsEnabled();
bool GameDx12J2A_Run(const char* levelName, uint64_t levelSession,
                     uint64_t levelEpoch,
                     const DirectX::XMFLOAT4X4& view,
                     const DirectX::XMFLOAT4X4& projection,
                     GameDx12J2AResult12& result) noexcept;
// Bridge-facing boundary: never throws; emits one non-terminal
// failure_detail record on failure. The terminal result=PASS/FAIL record
// remains owned by GameDx12J2A_Run.
bool GameDx12J2A_RunForBridge(const char* levelName, uint64_t levelSession,
                              uint64_t levelEpoch,
                              const DirectX::XMFLOAT4X4& view,
                              const DirectX::XMFLOAT4X4& projection,
                              GameDx12J2AResult12& result) noexcept;
void GameDx12J2A_Shutdown();

#endif // OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
