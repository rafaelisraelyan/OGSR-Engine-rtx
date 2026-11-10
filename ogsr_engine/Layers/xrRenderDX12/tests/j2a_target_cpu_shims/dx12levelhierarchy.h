#pragma once

// The production GameDx12J2APreparation.cpp is copied into the test build
// directory, so this wrapper shadows the real hierarchy header for that one
// translation unit. It first exposes the real declarations, then redirects
// only the sector lookup call to the fault-injection wrapper. The hierarchy
// implementation itself is still linked from production dx12levelhierarchy.cpp.

#include "../../dx12levelhierarchy.h"

namespace levelhierarchy12
{
SectorLookupStatus FindSectorForVisualFault(
    const LevelHierarchy& hierarchy, uint32_t visualId, uint32_t maxNodeVisits,
    uint32_t maxEdgeVisits, uint32_t& sectorId, SectorLookupStats& stats,
    std::string& reason);
} // namespace levelhierarchy12

#define FindSectorForVisual FindSectorForVisualFault
