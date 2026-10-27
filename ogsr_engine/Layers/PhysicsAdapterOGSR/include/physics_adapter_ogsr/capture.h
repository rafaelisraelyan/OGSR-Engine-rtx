#pragma once

// Portable OGSR capture builder.
//
// This layer accepts a neutral snapshot of the data that a debug-only OGSR
// collector reads from IKinematics and a prepared ODE shell. It performs the
// OGSR-specific disposition rules (jtRigid, invisible bones, jtNone), but does
// not include OGSR, ODE, PhysX or DirectX headers.

#include "physics_portable/json.h"
#include "physics_portable/ragdoll.h"

#include <cstdint>
#include <string>
#include <vector>

namespace physics_adapter_ogsr
{

enum class SourceJointType : std::uint8_t
{
    Rigid,
    Cloth,
    Joint,
    Wheel,
    None,
    Slider
};

struct CaptureShape
{
    physics_portable::ShapeType type = physics_portable::ShapeType::Box;
    physics_portable::Transform local{};
    physics_portable::Vec3 halfExtents = physics_portable::makeVec3(0.05f, 0.05f, 0.05f);
    float radius = 0.05f;
    float height = 0.1f;
    float margin = 0.0f;
    std::uint16_t materialId = 0;
    std::uint32_t flags = 0;
};

struct CaptureBone
{
    physics_portable::BoneId id = physics_portable::kInvalidBone;
    physics_portable::BoneId parent = physics_portable::kInvalidBone;
    std::string name;
    physics_portable::Transform bindLocal{};
    bool visible = true;

    bool hasShape = false;
    CaptureShape shape;

    // Final ODE mass properties for body bones. For MergedIntoParent bones the
    // collector must already have folded the mass into the parent body; these
    // fields are ignored for merged bones.
    float mass = 0.0f;
    physics_portable::Vec3 centerOfMass{};
    float inertia[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    SourceJointType jointType = SourceJointType::Rigid;
    physics_portable::RagdollAxisLimit limits[3]{};
    std::uint8_t limitCount = 0;
    physics_portable::SwingLimitMode swingLimitMode = physics_portable::SwingLimitMode::Auto;
    bool breakable = false;
    float breakForce = 0.0f;
    float breakTorque = 0.0f;
    float friction = 0.0f;
    std::uint32_t sourceFlags = 0;
};

struct SkeletonCapture
{
    std::vector<CaptureBone> bones; // indexed by BoneId; parent must precede child
    std::vector<physics_portable::Transform> currentBoneModelPose; // same size as bones
    physics_portable::Transform entityWorld{};
    physics_portable::Vec3 linearVelocity{};
    std::uint16_t materialId = 0;
    std::string materialName = "ogsr_capture";
    float friction = 0.8f;
    float restitution = 0.0f;
    std::string skeletonName;
    std::string ogsrCommit;
    std::string visual;
    std::uint32_t modelHash = 0;
    bool fixCopJoints = false;
};

bool buildRagdollCapture(const SkeletonCapture& capture,
                         physics_portable::RagdollCaptureDocument& document,
                         physics_portable::RagdollBuildReport& report,
                         std::string* error);

} // namespace physics_adapter_ogsr
