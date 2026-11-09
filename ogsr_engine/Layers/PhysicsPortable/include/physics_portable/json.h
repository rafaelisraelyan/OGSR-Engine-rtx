#pragma once

// JSON capture format for diagnostic OGSR dumps and standalone P1 fixtures.
// The format is deliberately independent of OGSR, ODE, PhysX and DirectX.

#include "physics_portable/ragdoll.h"

#include <cstdint>
#include <string>

namespace physics_portable
{

struct RagdollCaptureDocument
{
    RagdollDescription description;
    RagdollInitialState initial;
    std::string ogsrCommit;
    std::string visual;
    std::uint32_t modelHash = 0;
    bool fixCopJoints = false;
};

// Parses a capture document, then validates the portable contract.
// On failure, error contains a path and, for JSON syntax errors, line/column.
bool parseRagdollCapture(const std::string& json, RagdollCaptureDocument& document, std::string* error);

// Serializes a validated document. Returns false if the contract is invalid.
bool writeRagdollCapture(const RagdollCaptureDocument& document, std::string& json, std::string* error);

} // namespace physics_portable
