#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12AuxGeometry.h"

#include <span>

namespace gameaux12
{
struct SkeletalPartSource
{
    Mesh mesh;
    std::string materialRejection;
};

struct SkeletalModelSource
{
    uint32_t boneCount = 0;
    std::vector<SkeletalPartSource> parts;
};

bool ParseSkeletalModel(std::span<const uint8_t> source, bool skinned,
                        SkeletalModelSource& model, std::string& reason);
bool ReadSkeletalModel(const char* visualName, bool skinned,
                       SkeletalModelSource& model, std::string& reason);
}

#endif
