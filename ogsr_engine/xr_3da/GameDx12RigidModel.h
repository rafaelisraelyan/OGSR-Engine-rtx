#pragma once

#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE

#include "GameDx12AuxGeometry.h"

#include <span>

namespace gameaux12
{
bool ParseRigidModel(std::span<const uint8_t> source, std::vector<Mesh>& meshes,
                     std::string& reason);
bool ReadRigidModel(const char* visualName, std::vector<Mesh>& meshes,
                    std::string& reason);
}

#endif
