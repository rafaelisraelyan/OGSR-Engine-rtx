#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12LevelDetails.h"
#include "GameDx12StaticScene.h"

class IGame_Level;

namespace gameaux12
{
// Игровые указатели доступны только в Prepare на потоке игры до seqFrameMT.
class GameVisuals
{
public:
    bool Prepare(IGame_Level& level, const float eye[3], GameDx12StaticScene12& scene,
                 const GameDx12StaticSceneOwner12& owner, std::string& reason);
    void Reset();
    void InvalidateFrame(uint64_t epoch);
    const Frame& Snapshot() const { return m_frame; }
    LevelDetails* ShadowDetailSource() { return m_detailsLoaded ? &m_details : nullptr; }
    void LogPresent(uint64_t present, uint64_t fence, const GameDx12StaticScene12::AuxStats& draws,
                    const GameDx12StaticScene12::AuxStats& resources) const;

private:
    struct Model
    {
        std::vector<Part> parts;
        std::string reason;
        uint32_t identity = 0;
        uint32_t boneCount = 0;
    };
    struct Counts
    {
        uint32_t supported = 0;
        uint32_t destroyed = 0, hidden = 0, validPose = 0, childHidden = 0;
        uint32_t invalidPose = 0, waitingModel = 0, missingModel = 0;
        uint32_t missingParts = 0, poseBudget = 0, newModels = 0, reused = 0;
        std::unordered_map<std::string, uint32_t> rejections;
    };

    bool LoadPending(IGame_Level& level, GameDx12StaticScene12& scene,
                     const GameDx12StaticSceneOwner12& owner, std::string& reason);
    void Capture(IGame_Level& level, const float eye[3]);

    Frame m_frame;
    Counts m_rigidCounts;
    Counts m_skinnedCounts;
    uint32_t m_nextHemiObject = 0;
    uint32_t m_objects = 0;
    uint32_t m_visuals = 0;
    uint32_t m_unsupported = 0;
    LevelDetails m_details;
    DetailStats m_detailStats;
    std::vector<std::vector<DetailInstance>> m_detailInstances;
    std::unordered_map<std::string, Model> m_models;
    bool m_detailsAttempted = false;
    bool m_detailsLoaded = false;
    uint32_t m_discoveryFrames = 0;
    uint32_t m_skinnedDiscoveryFrames = 0;
    uint32_t m_rigidWarnings = 0;
    uint32_t m_skinnedWarnings = 0;
    uint32_t m_skinnedAssetLogs = 0;
    uint32_t m_materialLogs = 0;
    uint64_t m_detailTextureBytes = 0;
};
}

#endif
