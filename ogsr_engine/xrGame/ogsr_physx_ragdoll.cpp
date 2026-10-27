#include "stdafx.h"

#ifdef OGSR_PHYSX_RAGDOLL

#include "ogsr_physx_ragdoll.h"
#include "ogsr_physx_runtime.h"

#include "Level.h"
#include "Hit.h"
#include "Actor.h"
#include "xrMessages.h"
#include "CameraFirstEye.h"
#include "CharacterPhysicsSupport.h"
#include "ai/stalker/ai_stalker.h"
#include "PHMovementControl.h"
#include "../xr_3da/pure.h"
#include "../xr_3da/xr_area.h"
#include "../xr_3da/xr_object.h"
#include "../Layers/PhysicsAdapterOGSR/ogsr/ogsr_capture.h"
#include "../Layers/PhysicsPortable/include/physics_portable/row_matrix.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace pp = physics_portable;

namespace
{
constexpr std::uint32_t kStaticCapTriangles = 8192;
constexpr float kStaticSizeX = 48.0f;
constexpr float kStaticSizeY = 24.0f;
constexpr float kStaticSizeZ = 48.0f;
constexpr std::uint32_t kSmokeGiveUpFrame = 1800;

Fvector g_smokeActorStart{};
Fvector g_smokeActorDir{};
bool g_smokeActorStartValid = false;
u16 g_smokeTargetId = u16(-1);

Fvector g_smokeCameraTarget{};
bool g_smokeCameraActive = false;
bool g_smokeResetRequested = false;

class SmokeCamera final : public pureFrame
{
public:
    void OnFrame() override
    {
        if (!g_smokeCameraActive)
            return;
        Fvector position = g_smokeCameraTarget;
        position.y += 6.0f;
        Device.vCameraPosition.set(position);
        Device.vCameraDirection.set(0.0f, -1.0f, 0.0f);
        Device.vCameraTop.set(0.0f, 0.0f, 1.0f);
        Device.vCameraRight.set(1.0f, 0.0f, 0.0f);
    }
};

SmokeCamera g_smokeCamera;

} // namespace

struct BoneSlot
{
    OgsrPhysXRagdollState* state = nullptr;
    std::uint16_t bone = 0;
};

struct OgsrPhysXRagdollState
{
    OgsrPhysXRagdollCapture capture;
    IKinematics* kinematics = nullptr;
    std::vector<Fmatrix> boneModel;
    std::vector<BoneSlot> slots;
    std::vector<pp::Transform> lastBodyWorld;
    std::vector<std::uint8_t> bodyDynamic;
    std::vector<std::uint16_t> debugBones;
    std::uint32_t publishFrames = 0;
    std::atomic<std::uint32_t> callbackCalls{0};
    std::mutex boneMutex;
    bool callbacksReported = false;
    Fmatrix entityWorld;
    bool reportedInitialError = false;
    bool active = false;
};

namespace
{
void boneCallback(CBoneInstance* instance)
{
    auto* slot = static_cast<BoneSlot*>(instance->callback_param());
    if (!slot || !slot->state)
        return;
    ++slot->state->callbackCalls;
    const std::size_t index = slot->bone;
    std::lock_guard<std::mutex> lock(slot->state->boneMutex);
    if (index < slot->state->boneModel.size())
        instance->mTransform = slot->state->boneModel[index];
}

pp::Transform portable(const Fmatrix& matrix)
{
    pp::RowMatrix4 row{};
    std::memcpy(row.m, matrix.m, sizeof(row.m));
    return pp::transformFromRowMatrix(row);
}

Fmatrix engineMatrix(const pp::Transform& transform)
{
    const pp::RowMatrix4 row = pp::rowMatrixFromTransform(transform);
    Fmatrix matrix;
    std::memcpy(matrix.m, row.m, sizeof(row.m));
    return matrix;
}

pp::Transform rowTransform(const float* values)
{
    pp::RowMatrix4 row{};
    std::memcpy(row.m, values, sizeof(row.m));
    return pp::transformFromRowMatrix(row);
}

void storeRow(const pp::Transform& transform, float* out)
{
    const pp::RowMatrix4 row = pp::rowMatrixFromTransform(transform);
    std::memcpy(out, row.m, sizeof(row.m));
}

const char* safeName(const shared_str& value) { return value.c_str(); }

std::uint32_t countBoneCallbacks(IKinematics* kinematics)
{
    if (!kinematics)
        return 0;
    std::uint32_t count = 0;
    const u16 bones = kinematics->LL_BoneCount();
    for (u16 bone = 0; bone < bones; ++bone)
    {
        if (kinematics->LL_GetBoneInstance(bone).callback() != nullptr)
            ++count;
    }
    return count;
}

bool findTarget(CAI_Stalker*& result, CObject*& object, std::string& description)
{
    result = nullptr;
    object = nullptr;
    description.clear();
    const u32 count = Level().Objects.o_count();
    for (u32 index = 0; index < count; ++index)
    {
        CObject* candidate = Level().Objects.o_get_by_iterator(index);
        CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(candidate);
        if (!stalker)
            continue;
        const char* name = safeName(stalker->cName());
        const char* section = safeName(stalker->cNameSect());
        if (!OgsrPhysXRagdollMatches(name, section))
            continue;
        if (result)
        {
            description = "ambiguous NPC target";
            result = nullptr;
            object = nullptr;
            return false;
        }
        result = stalker;
        object = candidate;
        description = std::string(name) + " / " + section;
    }
    return result != nullptr;
}

std::uint16_t rootBoneId(IKinematics& kinematics)
{
    const u16 pelvis = kinematics.LL_BoneID("bip01_pelvis");
    return pelvis == BI_NONE ? kinematics.LL_GetBoneRoot() : pelvis;
}

bool isAliveExtra(CAI_Stalker* stalker, u16 excludeA, u16 excludeB)
{
    return stalker && stalker->ID() != excludeA && stalker->ID() != excludeB && stalker->g_Alive() &&
           stalker->Visual() && !stalker->PPhysicsShell();
}

CAI_Stalker* findAliveExtra(u16 excludeA, u16 excludeB)
{
    for (u32 index = 0; index < Level().Objects.o_count(); ++index)
    {
        CAI_Stalker* candidate = smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index));
        if (isAliveExtra(candidate, excludeA, excludeB))
            return candidate;
    }
    return nullptr;
}

void applyLethalSmokeHit(CAI_Stalker* stalker, const char* label)
{
    if (!stalker)
        return;
    IKinematics* kinematics = smart_cast<IKinematics*>(stalker->Visual());
    if (!kinematics)
        return;
    const u16 bone = rootBoneId(*kinematics);
    Fvector direction(0.0f, 0.0f, 1.0f);
    Fvector position(0.0f, 0.0f, 0.0f);
    SHit hit(1000.0f, direction, Level().CurrentEntity(), bone, position, 8.0f, ALife::eHitTypeFireWound,
             0.0f, false);
    Msg("ph_ragdoll_smoke: multi lethal hit %s id=%u bone=%u pos=(%.2f,%.2f,%.2f)", label, stalker->ID(),
        bone, stalker->Position().x, stalker->Position().y, stalker->Position().z);
    stalker->Hit(&hit);
}
} // namespace

bool OgsrPhysXRagdollBuildCapture(IKinematics& kinematics,
                                  CPhysicsShell& shell,
                                  const Fmatrix& entityWorld,
                                  const Fvector& velocity,
                                  OgsrPhysXRagdollCapture& capture,
                                  std::string* error)
{
    physics_portable::RagdollCaptureDocument document;
    if (!physics_adapter_ogsr::captureFromOgsr(kinematics, shell, entityWorld, velocity, document, error))
        return false;
    if (document.description.bodies.empty() || document.description.bones.empty())
    {
        if (error)
            *error = "capture produced an empty ragdoll";
        return false;
    }

    capture.description = std::move(document.description);
    capture.initial = std::move(document.initial);

    // OGSR's capture marks the synthetic pelvis/spine root as a static body
    // with zero ODE mass. The PhysX backend promotes it to a dynamic body and
    // derives its mass/inertia from the actual shapes; the description keeps
    // the ODE parity role here.
    if (!capture.description.bodies.empty() && capture.description.rootBody < capture.description.bodies.size())
    {
        const pp::RagdollBodyDesc& root = capture.description.bodies[capture.description.rootBody];
        Msg("ph_ragdoll: capture root body=%u bone=%u role=%u mass=%.3f shapes=%zu", root.id, root.boneId,
            static_cast<unsigned>(root.role), root.mass, root.shapes.size());
    }

    const std::size_t boneCount = kinematics.LL_BoneCount();
    const std::size_t bodyCount = capture.description.bodies.size();
    if (capture.description.bones.size() != boneCount || capture.initial.bodyWorld.size() != bodyCount)
    {
        if (error)
            *error = "capture bone or body count is inconsistent";
        return false;
    }

    const pp::Transform entity = portable(entityWorld);
    capture.initialBoneWorld.resize(boneCount);
    capture.animationBoneModel.resize(boneCount);
    for (std::size_t bone = 0; bone < boneCount; ++bone)
    {
        const pp::Transform model = portable(kinematics.LL_GetTransform(static_cast<u16>(bone)));
        capture.animationBoneModel[bone] = model;
        capture.initialBoneWorld[bone] = entity * model;
    }

    capture.boneBody.assign(boneCount, pp::kInvalidBody);
    capture.boneKind.assign(boneCount, 2);
    capture.mergedLocal.assign(boneCount, pp::Transform{});
    capture.bodyBone.assign(bodyCount, pp::kInvalidBone);
    for (std::size_t body = 0; body < bodyCount; ++body)
    {
        const pp::RagdollBodyDesc& bodyDesc = capture.description.bodies[body];
        if (bodyDesc.boneId >= boneCount)
        {
            if (error)
                *error = "body bone id is out of range";
            return false;
        }
        capture.bodyBone[body] = bodyDesc.boneId;
        capture.boneBody[bodyDesc.boneId] = static_cast<pp::BodyId>(body);
        capture.boneKind[bodyDesc.boneId] = 0;
    }

    for (std::size_t bone = 0; bone < boneCount; ++bone)
    {
        const pp::RagdollBoneDesc& boneDesc = capture.description.bones[bone];
        if (boneDesc.disposition == pp::BoneDisposition::MergedIntoParent)
        {
            if (boneDesc.body >= bodyCount)
            {
                if (error)
                    *error = "merged bone body is out of range";
                return false;
            }
            capture.boneBody[bone] = boneDesc.body;
            capture.boneKind[bone] = 1;
            capture.mergedLocal[bone] =
                pp::inverse(capture.initial.bodyWorld[boneDesc.body]) * capture.initialBoneWorld[bone];
        }
    }

    const std::size_t mappedBodies = std::count_if(capture.boneKind.begin(), capture.boneKind.end(),
                                                   [](std::uint8_t kind) { return kind == 0; });
    if (mappedBodies != bodyCount)
    {
        if (error)
            *error = "not every PhysX body has a source bone";
        return false;
    }
    return true;
}

bool OgsrPhysXRagdollSetStaticFragment(const Fvector& center, std::string* error)
{
    Fvector zAxis(0.0f, 0.0f, 1.0f);
    Fvector yAxis(0.0f, 1.0f, 0.0f);
    Fvector sizes(kStaticSizeX, kStaticSizeY, kStaticSizeZ);
    std::vector<Fvector> triangleVertices;
    if (!Level().ObjectSpace.BoxQuery(center, zAxis, yAxis, sizes, &triangleVertices) || triangleVertices.empty())
    {
        if (error)
            *error = "no static level triangles around the NPC";
        return false;
    }

    const std::size_t sourceTriangles = triangleVertices.size() / 3;
    const std::size_t usedTriangles = std::min<std::size_t>(sourceTriangles, kStaticCapTriangles);
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(usedTriangles * 9);
    indices.reserve(usedTriangles * 3);
    Fvector minimum(FLT_MAX, FLT_MAX, FLT_MAX);
    Fvector maximum(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (std::size_t triangle = 0; triangle < usedTriangles; ++triangle)
    {
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const Fvector& source = triangleVertices[triangle * 3 + corner];
            vertices.push_back(source.x);
            vertices.push_back(source.y);
            vertices.push_back(source.z);
            indices.push_back(static_cast<std::uint32_t>(indices.size()));
            minimum.x = std::min(minimum.x, source.x);
            minimum.y = std::min(minimum.y, source.y);
            minimum.z = std::min(minimum.z, source.z);
            maximum.x = std::max(maximum.x, source.x);
            maximum.y = std::max(maximum.y, source.y);
            maximum.z = std::max(maximum.z, source.z);
        }
    }

    if (!OgsrPhysXRagdollRuntimeSetStaticMesh(vertices.data(), static_cast<uint32_t>(vertices.size() / 3),
                                              indices.data(), static_cast<uint32_t>(indices.size()), error))
        return false;

    Msg("ph_ragdoll: static fragment center=(%.2f,%.2f,%.2f) box=(%.1fx%.1fx%.1f) tris=%zu/%zu "
        "bounds_min=(%.2f,%.2f,%.2f) bounds_max=(%.2f,%.2f,%.2f)",
        center.x, center.y, center.z, kStaticSizeX, kStaticSizeY, kStaticSizeZ, usedTriangles, sourceTriangles,
        minimum.x, minimum.y, minimum.z, maximum.x, maximum.y, maximum.z);
    return true;
}

bool OgsrPhysXRagdollActivate(const OgsrPhysXRagdollCapture& capture,
                              IKinematics& kinematics,
                              const Fmatrix& entityWorld,
                              const Fvector& center,
                              OgsrPhysXRagdollState*& state,
                              std::uint32_t& bodyCount,
                              std::uint32_t& jointCount,
                              std::string* error)
{
    state = nullptr;
    bodyCount = 0;
    jointCount = 0;
    if (!OgsrPhysXRagdollRuntimeAlive())
    {
        if (error)
            *error = "NPC ragdoll runtime is not alive";
        return false;
    }
    if (capture.description.bodies.size() > OGSR_PHYSX_RAGDOLL_MAX_BODIES ||
        capture.description.bones.size() > OGSR_PHYSX_RAGDOLL_MAX_BONES ||
        capture.description.joints.size() > OGSR_PHYSX_RAGDOLL_MAX_JOINTS)
    {
        if (error)
            *error = "capture exceeds the C ABI caps";
        return false;
    }
    if (!OgsrPhysXRagdollSetStaticFragment(center, error))
        return false;

    std::vector<OgsrPhysXRagdollBone> bones;
    std::vector<OgsrPhysXRagdollBody> bodies;
    std::vector<OgsrPhysXRagdollJoint> joints;
    std::vector<OgsrPhysXRagdollShape> shapes;
    std::vector<OgsrPhysXRagdollMaterial> materials;
    bones.reserve(capture.description.bones.size());
    for (const pp::RagdollBoneDesc& source : capture.description.bones)
    {
        OgsrPhysXRagdollBone bone{};
        bone.id = source.id;
        bone.parent = source.parent;
        bone.visible = source.visible ? 1 : 0;
        bone.disposition = static_cast<std::uint8_t>(source.disposition);
        bone.body = source.body;
        bone.flags = source.sourceShapeFlags;
        storeRow(source.bindLocal, bone.bindLocal);
        std::snprintf(bone.name, sizeof(bone.name), "%s", source.name.c_str());
        bones.push_back(bone);
    }

    materials.reserve(capture.description.materials.size());
    for (const pp::RagdollMaterialDesc& source : capture.description.materials)
    {
        OgsrPhysXRagdollMaterial material{};
        material.id = source.id;
        material.friction = source.friction;
        material.restitution = source.restitution;
        material.spring = source.spring;
        material.damping = source.damping;
        std::snprintf(material.name, sizeof(material.name), "%s", source.name.c_str());
        materials.push_back(material);
    }

    bodies.reserve(capture.description.bodies.size());
    for (const pp::RagdollBodyDesc& source : capture.description.bodies)
    {
        OgsrPhysXRagdollBody body{};
        body.id = source.id;
        body.boneId = source.boneId;
        body.parentBody = source.parentBody;
        body.role = static_cast<std::uint8_t>(source.role);
        body.isFixed = source.isFixed ? 1 : 0;
        body.mass = source.mass;
        body.centerOfMass[0] = source.centerOfMass.x;
        body.centerOfMass[1] = source.centerOfMass.y;
        body.centerOfMass[2] = source.centerOfMass.z;
        std::memcpy(body.inertia, source.inertia, sizeof(body.inertia));
        storeRow(source.bindPose, body.bindPose);
        body.firstShape = static_cast<std::uint32_t>(shapes.size());
        body.shapeCount = static_cast<std::uint32_t>(source.shapes.size());
        body.collisionGroup = source.collisionGroup;
        for (const pp::RagdollShapeDesc& shapeSource : source.shapes)
        {
            OgsrPhysXRagdollShape shape{};
            shape.type = static_cast<std::uint8_t>(shapeSource.type);
            storeRow(shapeSource.local, shape.local);
            shape.halfExtents[0] = shapeSource.halfExtents.x;
            shape.halfExtents[1] = shapeSource.halfExtents.y;
            shape.halfExtents[2] = shapeSource.halfExtents.z;
            shape.radius = shapeSource.radius;
            shape.height = shapeSource.height;
            shape.margin = shapeSource.margin;
            shape.materialId = shapeSource.materialId;
            shape.sourceBoneId = shapeSource.sourceBoneId;
            shape.flags = shapeSource.flags;
            shapes.push_back(shape);
        }
        bodies.push_back(body);
    }

    joints.reserve(capture.description.joints.size());
    for (const pp::RagdollJointDesc& source : capture.description.joints)
    {
        OgsrPhysXRagdollJoint joint{};
        joint.id = source.id;
        joint.boneId = source.boneId;
        joint.parentBody = source.parentBody;
        joint.childBody = source.childBody;
        joint.kind = static_cast<std::uint8_t>(source.kind);
        joint.axisCount = source.axisCount;
        joint.breakable = source.breakable ? 1 : 0;
        joint.swingLimitMode = static_cast<std::uint8_t>(source.swingLimitMode);
        storeRow(source.parentFrame, joint.parentFrame);
        storeRow(source.childFrame, joint.childFrame);
        joint.erp = source.erp;
        joint.cfm = source.cfm;
        joint.friction = source.friction;
        joint.maxForce = source.maxForce;
        joint.targetVelocity = source.targetVelocity;
        joint.breakForce = source.breakForce;
        joint.breakTorque = source.breakTorque;
        joint.flags = source.sourceFlags;
        for (std::size_t axis = 0; axis < source.axisCount && axis < OGSR_PHYSX_RAGDOLL_MAX_AXES; ++axis)
        {
            const pp::RagdollAxisLimit& sourceAxis = source.axes[axis];
            OgsrPhysXRagdollAxis& axisOut = joint.axes[axis];
            axisOut.axis[0] = sourceAxis.axis.x;
            axisOut.axis[1] = sourceAxis.axis.y;
            axisOut.axis[2] = sourceAxis.axis.z;
            axisOut.space = static_cast<std::uint8_t>(sourceAxis.space);
            axisOut.lower = sourceAxis.lower;
            axisOut.upper = sourceAxis.upper;
            axisOut.sourceLower = sourceAxis.sourceLower;
            axisOut.sourceUpper = sourceAxis.sourceUpper;
            axisOut.spring = sourceAxis.spring;
            axisOut.damping = sourceAxis.damping;
            axisOut.zero = sourceAxis.zero;
        }
        joints.push_back(joint);
    }

    const std::size_t bodyCountValue = capture.description.bodies.size();
    std::vector<float> bodyWorld(bodyCountValue * 16);
    std::vector<float> linearVelocity(bodyCountValue * 3);
    std::vector<float> angularVelocity(bodyCountValue * 3);
    std::vector<std::uint8_t> enabled(bodyCountValue, 1);
    for (std::size_t body = 0; body < bodyCountValue; ++body)
    {
        storeRow(capture.initial.bodyWorld[body], bodyWorld.data() + body * 16);
        const pp::Vec3& linear = capture.initial.linearVelocity[body];
        linearVelocity[body * 3 + 0] = linear.x;
        linearVelocity[body * 3 + 1] = linear.y;
        linearVelocity[body * 3 + 2] = linear.z;
        const pp::Vec3& angular = capture.initial.angularVelocity[body];
        angularVelocity[body * 3 + 0] = angular.x;
        angularVelocity[body * 3 + 1] = angular.y;
        angularVelocity[body * 3 + 2] = angular.z;
        enabled[body] = capture.initial.enabled.empty() || capture.initial.enabled[body] ? 1 : 0;
    }

    OgsrPhysXRagdollDefinition definition{};
    definition.rootBody = capture.description.rootBody;
    definition.boneCount = static_cast<std::uint32_t>(bones.size());
    definition.bodyCount = static_cast<std::uint32_t>(bodies.size());
    definition.jointCount = static_cast<std::uint32_t>(joints.size());
    definition.shapeCount = static_cast<std::uint32_t>(shapes.size());
    definition.materialCount = static_cast<std::uint32_t>(materials.size());
    definition.bones = bones.data();
    definition.bodies = bodies.data();
    definition.joints = joints.data();
    definition.shapes = shapes.data();
    definition.materials = materials.data();
    storeRow(capture.initial.entityWorld, definition.entityWorld);
    storeRow(capture.initial.rootToEntity, definition.rootToEntity);
    definition.initialBodyWorld = bodyWorld.data();
    definition.initialLinearVelocity = linearVelocity.data();
    definition.initialAngularVelocity = angularVelocity.data();
    definition.initialEnabled = enabled.data();

    if (!OgsrPhysXRagdollRuntimeCreateInstance(definition, error, bodyCount, jointCount))
        return false;

    auto* created = new OgsrPhysXRagdollState();
    created->capture = capture;
    created->kinematics = &kinematics;
    created->entityWorld = entityWorld;
    created->lastBodyWorld = capture.initial.bodyWorld;
    created->bodyDynamic.resize(capture.description.bodies.size(), 0);
    for (std::size_t body = 0; body < capture.description.bodies.size(); ++body)
    {
        const pp::RagdollBodyDesc& bodyDesc = capture.description.bodies[body];
        const bool promotedRoot = bodyDesc.role == pp::BodyRole::StaticRoot &&
                                  body == capture.description.rootBody && !bodyDesc.shapes.empty();
        created->bodyDynamic[body] =
            (promotedRoot || (bodyDesc.role != pp::BodyRole::StaticRoot && !bodyDesc.isFixed)) ? 1 : 0;
    }
    const char* debugNames[] = {"bip01_pelvis", "bip01_head", "bip01_l_hand", "bip01_r_hand", "bip01_l_foot",
                                "bip01_r_foot"};
    for (const char* name : debugNames)
    {
        const u16 bone = kinematics.LL_BoneID(name);
        if (bone != BI_NONE)
            created->debugBones.push_back(bone);
    }
    created->boneModel.resize(capture.animationBoneModel.size());
    for (std::size_t bone = 0; bone < created->boneModel.size(); ++bone)
        created->boneModel[bone] = engineMatrix(capture.animationBoneModel[bone]);
    created->slots.resize(capture.animationBoneModel.size());
    for (std::size_t bone = 0; bone < created->slots.size(); ++bone)
    {
        created->slots[bone].state = created;
        created->slots[bone].bone = static_cast<std::uint16_t>(bone);
        if (!kinematics.LL_GetBoneVisible(static_cast<u16>(bone)))
            continue;
        kinematics.LL_GetBoneInstance(static_cast<u16>(bone))
            .set_callback(bctCustom, &boneCallback, &created->slots[bone], TRUE);
    }
    created->active = true;
    kinematics.CalculateBones_Invalidate();

    state = created;
    OgsrPhysXRagdollInfo info{};
    if (OgsrPhysXRagdollRuntimeGetInfo(info))
    {
        Msg("ph_ragdoll: PhysX ragdoll created bodies=%u joints=%u bones=%zu total_mass=%.2f root_mass=%.2f "
            "root_body=%u root_dynamic=%u",
            bodyCount, jointCount, created->boneModel.size(), info.totalMass, info.rootMass,
            capture.description.rootBody,
            capture.description.rootBody < created->bodyDynamic.size()
                ? created->bodyDynamic[capture.description.rootBody]
                : 0);
    }
    else
    {
        Msg("ph_ragdoll: PhysX ragdoll created bodies=%u joints=%u bones=%zu", bodyCount, jointCount,
            created->boneModel.size());
    }
    return true;
}

void OgsrPhysXRagdollRelease(OgsrPhysXRagdollState* state)
{
    if (!state)
        return;
    if (g_smokeCameraActive)
    {
        Device.seqFrame.Remove(&g_smokeCamera);
        g_smokeCameraActive = false;
    }
    for (std::size_t bone = 0; bone < state->slots.size(); ++bone)
    {
        if (!state->kinematics)
            break;
        CBoneInstance& instance = state->kinematics->LL_GetBoneInstance(static_cast<u16>(bone));
        if (instance.callback() == &boneCallback && instance.callback_param() == &state->slots[bone])
        {
            instance.reset_callback();
        }
    }
    if (state->kinematics)
        state->kinematics->CalculateBones_Invalidate();
    if (state->active && OgsrPhysXRagdollRuntimeAlive())
    {
        OgsrPhysXRagdollRuntimeDestroyInstance();
        Msg("ph_ragdoll: PhysX ragdoll instance released");
    }
    delete state;
}

bool OgsrPhysXRagdollPublish(OgsrPhysXRagdollState& state, IKinematics& kinematics, Fmatrix& entityWorld, std::string* error)
{
    if (!state.active)
        return false;
    OgsrPhysXRagdollPose pose{};
    std::vector<float> bodyWorld(OGSR_PHYSX_RAGDOLL_MAX_BODIES * 16);
    if (!OgsrPhysXRagdollRuntimeGetPose(pose, bodyWorld.data(), OGSR_PHYSX_RAGDOLL_MAX_BODIES))
    {
        if (error)
            *error = "cannot read the PhysX ragdoll pose";
        return false;
    }
    if (pose.bodyCount != state.capture.description.bodies.size())
    {
        if (error)
            *error = "PhysX pose body count changed";
        return false;
    }

    const pp::Transform entity = rowTransform(pose.entityWorld);
    entityWorld = engineMatrix(entity);
    state.entityWorld = entityWorld;
    if (g_smokeCameraActive)
        g_smokeCameraTarget.set(entityWorld.c.x, entityWorld.c.y, entityWorld.c.z);
    state.lastBodyWorld.resize(pose.bodyCount);
    for (std::uint32_t body = 0; body < pose.bodyCount; ++body)
        state.lastBodyWorld[body] = rowTransform(bodyWorld.data() + static_cast<std::size_t>(body) * 16);

    const pp::Transform inverseEntity = pp::inverse(entity);
    float maxTranslation = 0.0f;
    {
        std::lock_guard<std::mutex> lock(state.boneMutex);
        for (std::size_t bone = 0; bone < state.boneModel.size(); ++bone)
        {
        const std::uint8_t kind = state.capture.boneKind[bone];
        pp::Transform world;
        if (kind == 0)
        {
            world = state.lastBodyWorld[state.capture.boneBody[bone]];
        }
        else if (kind == 1)
        {
            world = state.lastBodyWorld[state.capture.boneBody[bone]] * state.capture.mergedLocal[bone];
        }
        else
        {
            world = entity * state.capture.animationBoneModel[bone];
        }
        const pp::Transform model = inverseEntity * world;
        state.boneModel[bone] = engineMatrix(model);

        if (!state.reportedInitialError)
        {
            const pp::Vec3 delta{model.translation.x - state.capture.animationBoneModel[bone].translation.x,
                                 model.translation.y - state.capture.animationBoneModel[bone].translation.y,
                                 model.translation.z - state.capture.animationBoneModel[bone].translation.z};
            maxTranslation = std::max(maxTranslation, std::sqrt(pp::dot(delta, delta)));
        }
    }
    if (!state.reportedInitialError)
    {
        state.reportedInitialError = true;
        Msg("ph_ragdoll: first-frame bone continuity max_translation=%.5f m", maxTranslation);
    }
    if (!state.callbacksReported && state.callbackCalls.load() > 0)
    {
        state.callbacksReported = true;
        Msg("ph_ragdoll: bone callbacks active calls=%u", state.callbackCalls.load());
    }

    if (++state.publishFrames % 120 == 0)
    {
        Msg("ph_ragdoll: pose entity=(%.2f,%.2f,%.2f)", entity.translation.x, entity.translation.y,
            entity.translation.z);
        for (std::uint16_t bone : state.debugBones)
        {
            if (bone >= state.boneModel.size())
                continue;
            Fmatrix world;
            world.mul_43(state.entityWorld, state.boneModel[bone]);
            Msg("ph_ragdoll: bone id=%u world=(%.2f,%.2f,%.2f)", bone, world.c.x, world.c.y, world.c.z);
        }
        OgsrPhysXRagdollDiagnostics diagnostics{};
        if (OgsrPhysXRagdollRuntimeGetDiagnostics(0.0f, diagnostics))
        {
            Msg("ph_ragdoll: energy ke=%.2f pe=%.2f total=%.2f max_w=%.2f anchor=%.4f limit=%.4f contacts=%u "
                "found=%u persist=%u lost=%u active=%u pen=%.4f awake=%u/%u impulses=%u",
                diagnostics.kineticEnergy, diagnostics.potentialEnergy, diagnostics.totalEnergy,
                diagnostics.maxAngularSpeed, diagnostics.maxJointAnchorError, diagnostics.maxJointLimitError,
                diagnostics.contactCount, diagnostics.contactFound, diagnostics.contactPersist,
                diagnostics.contactLost, diagnostics.activeContactPairs, diagnostics.maxPenetration,
                diagnostics.awakeBodies, diagnostics.dynamicBodies, diagnostics.externalImpulses);
        }
    }
    }

    kinematics.CalculateBones_Invalidate();
    return true;
}

bool OgsrPhysXRagdollApplyBoneHit(OgsrPhysXRagdollState& state,
                                  const SHit& hit,
                                  const pp::Vec3& boneSpacePosition,
                                  std::string* error)
{
    if (hit.phys_impulse() <= 0.0f)
        return true;
    if (state.capture.description.bodies.empty())
    {
        if (error)
            *error = "no PhysX body for the hit";
        return false;
    }

    const std::uint16_t bone = hit.bone();
    std::uint16_t body = state.capture.description.rootBody;
    if (bone < state.capture.boneBody.size())
    {
        const std::uint16_t mapped = state.capture.boneBody[bone];
        if (mapped != pp::kInvalidBody)
            body = mapped;
    }
    if (body >= state.capture.description.bodies.size())
        body = state.capture.description.rootBody;
    if (body >= state.capture.description.bodies.size())
        body = 0;
    if (!state.bodyDynamic.empty() && !state.bodyDynamic[body])
    {
        std::uint16_t replacement = pp::kInvalidBody;
        for (std::size_t index = 0; index < state.bodyDynamic.size(); ++index)
        {
            if (state.bodyDynamic[index])
            {
                replacement = static_cast<std::uint16_t>(index);
                break;
            }
        }
        if (replacement == pp::kInvalidBody)
        {
            if (error)
                *error = "no dynamic PhysX body for the hit";
            return false;
        }
        body = replacement;
    }

    OgsrPhysXRagdollBodyMass mass{};
    if (!OgsrPhysXRagdollRuntimeGetBodyMass(body, mass, error))
        return false;
    if (mass.body != body)
    {
        if (error)
            *error = "PhysX body ID does not match the requested body";
        return false;
    }

    const pp::Transform bodyWorld = rowTransform(mass.actorWorld);
    const pp::Vec3 comWorld{mass.worldCenterOfMass[0], mass.worldCenterOfMass[1], mass.worldCenterOfMass[2]};

    pp::BoneWorldKind boneKind = pp::BoneWorldKind::Body;
    pp::Transform mergedLocal{};
    pp::Transform animationBoneModel{};
    if (bone < state.capture.boneKind.size())
    {
        if (state.capture.boneKind[bone] == 1)
        {
            boneKind = pp::BoneWorldKind::Merged;
            mergedLocal = state.capture.mergedLocal[bone];
        }
        else if (state.capture.boneKind[bone] == 2)
        {
            boneKind = pp::BoneWorldKind::Frozen;
            animationBoneModel = state.capture.animationBoneModel[bone];
        }
    }
    const pp::Transform boneWorld = pp::resolveBoneWorld(
        boneKind, bodyWorld, mergedLocal, portable(state.entityWorld), animationBoneModel);
    const pp::Vec3 hitWorld = pp::resolveWorldHitPoint(boneWorld, boneSpacePosition);

    Fvector direction = hit.direction();
    if (direction.magnitude() < EPS)
        direction.set(0.0f, 1.0f, 0.0f);
    else
        direction.normalize();
    const pp::Vec3 impulse{direction.x * hit.phys_impulse(), direction.y * hit.phys_impulse(),
                           direction.z * hit.phys_impulse()};
    const pp::Vec3 lever{hitWorld.x - comWorld.x, hitWorld.y - comWorld.y, hitWorld.z - comWorld.z};
    const pp::Vec3 angular = pp::cross(lever, impulse);

    const float worldPosition[3] = {hitWorld.x, hitWorld.y, hitWorld.z};
    const float worldImpulse[3] = {impulse.x, impulse.y, impulse.z};
    const bool applied = OgsrPhysXRagdollRuntimeApplyImpulse(body, worldPosition, worldImpulse, error);
    if (applied)
    {
        Msg("ph_ragdoll: bone impulse body=%u bone=%u hit=(%.3f,%.3f,%.3f) com=(%.3f,%.3f,%.3f) "
            "lever=(%.3f,%.3f,%.3f) impulse=(%.2f,%.2f,%.2f) angular=(%.2f,%.2f,%.2f)",
            body, bone, hitWorld.x, hitWorld.y, hitWorld.z, comWorld.x, comWorld.y, comWorld.z, lever.x, lever.y,
            lever.z, impulse.x, impulse.y, impulse.z, angular.x, angular.y, angular.z);
    }
    return applied;
}

bool OgsrPhysXRagdollApplyCentralImpulse(OgsrPhysXRagdollState& state,
                                         const pp::Vec3& impulse,
                                         std::string* error)
{
    if (state.capture.description.bodies.empty())
    {
        if (error)
            *error = "no PhysX body for the central impulse";
        return false;
    }

    std::uint16_t body = state.capture.description.rootBody;
    if (!state.bodyDynamic.empty() && !state.bodyDynamic[body])
    {
        body = pp::kInvalidBody;
        for (std::size_t index = 0; index < state.bodyDynamic.size(); ++index)
        {
            if (state.bodyDynamic[index])
            {
                body = static_cast<std::uint16_t>(index);
                break;
            }
        }
        if (body == pp::kInvalidBody)
        {
            if (error)
                *error = "no dynamic PhysX body for the central impulse";
            return false;
        }
    }

    OgsrPhysXRagdollBodyMass mass{};
    if (!OgsrPhysXRagdollRuntimeGetBodyMass(body, mass, error))
        return false;
    const pp::Vec3 comWorld{mass.worldCenterOfMass[0], mass.worldCenterOfMass[1], mass.worldCenterOfMass[2]};
    const float worldImpulse[3] = {impulse.x, impulse.y, impulse.z};
    const bool applied = OgsrPhysXRagdollRuntimeApplyCentralImpulse(body, worldImpulse, error);
    if (applied)
    {
        Msg("ph_ragdoll: central impulse body=%u com=(%.3f,%.3f,%.3f) impulse=(%.2f,%.2f,%.2f) "
            "lever=(0.000,0.000,0.000) angular=(0.00,0.00,0.00)",
            body, comWorld.x, comWorld.y, comWorld.z, impulse.x, impulse.y, impulse.z);
    }
    return applied;
}

void OgsrPhysXRagdollSmokeReset()
{
    g_smokeActorStartValid = false;
    g_smokeTargetId = u16(-1);
    g_smokeCameraActive = false;
    g_smokeResetRequested = true;
}

void OgsrPhysXRagdollSmokeTick()
{
    if (!OgsrPhysXRagdollTargetRequested() || !OgsrPhysXRagdollSmokeRequested())
        return;
    if (!OgsrPhysXRagdollRuntimeAlive() && !OgsrPhysXRagdollOdeSmokeRequested())
        return;
    static std::uint32_t frame = 0;
    static bool fired = false;
    static bool listed = false;
    static bool reloadRequested = false;
    static std::uint32_t hitFrame = 0;
    static bool destroyed = false;
    static bool visualReplaced = false;
    static bool odeVisualReplaced = false;
    static bool multiSpawned = false;
    static bool multiSecondHit = false;
    static bool multiFirstDestroyed = false;
    static bool multiThirdHit = false;
    static bool multiFinished = false;
    static bool staged = false;
    static std::uint32_t stagedFrame = 0;
    static u16 multiSecondId = u16(-1);
    static u16 multiThirdId = u16(-1);
    if (g_smokeResetRequested)
    {
        g_smokeResetRequested = false;
        frame = 0;
        fired = false;
        listed = false;
        reloadRequested = false;
        hitFrame = 0;
        destroyed = false;
        visualReplaced = false;
        odeVisualReplaced = false;
        multiSpawned = false;
        multiSecondHit = false;
        multiFirstDestroyed = false;
        multiThirdHit = false;
        multiFinished = false;
        staged = false;
        stagedFrame = 0;
        multiSecondId = u16(-1);
        multiThirdId = u16(-1);
    }
    ++frame;
    if (frame == 1 || frame % 300 == 0)
        Msg("ph_ragdoll_smoke: tick frame=%u", frame);
    if (fired)
    {
        if (hitFrame == 0)
            hitFrame = frame;

        if (OgsrPhysXRagdollMultiRequested())
        {
            if (!multiSpawned && frame - hitFrame >= 30)
            {
                multiSpawned = true;
                CAI_Stalker* target = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_smokeTargetId));
                if (target)
                {
                    Fvector base = target->Position();
                    Fvector first = base;
                    first.x += 2.0f;
                    Fvector second = base;
                    second.z += 2.0f;
                    Level().g_cl_Spawn("sim_default_stalker_2", 0xff, M_SPAWN_OBJECT_LOCAL, first);
                    Level().g_cl_Spawn("sim_default_stalker_2", 0xff, M_SPAWN_OBJECT_LOCAL, second);
                    Msg("ph_ragdoll_smoke: multi spawned two extra stalkers at (%.2f,%.2f,%.2f)",
                        base.x, base.y, base.z);
                }
            }
            if (!multiSecondHit && frame - hitFrame >= 120)
            {
                CAI_Stalker* extra = findAliveExtra(g_smokeTargetId, u16(-1));
                if (extra)
                {
                    multiSecondHit = true;
                    multiSecondId = extra->ID();
                    applyLethalSmokeHit(extra, "second/ODE-fallback");
                    Msg("ph_ragdoll_smoke: multi second hit id=%u physx_instance=%d", extra->ID(),
                        OgsrPhysXRagdollRuntimeHasInstance() ? 1 : 0);
                }
                else
                {
                    Msg("! ph_ragdoll_smoke: multi second NPC not spawned yet");
                }
            }
            if (!multiFirstDestroyed && frame - hitFrame >= 180)
            {
                multiFirstDestroyed = true;
                CAI_Stalker* target = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_smokeTargetId));
                if (target)
                {
                    Msg("ph_ragdoll_smoke: multi destroying first PhysX NPC id=%u", target->ID());
                    target->DestroyObject();
                }
            }
            if (!multiThirdHit && frame - hitFrame >= 240)
            {
                CAI_Stalker* extra = findAliveExtra(g_smokeTargetId, multiSecondId);
                if (extra)
                {
                    multiThirdHit = true;
                    multiThirdId = extra->ID();
                    applyLethalSmokeHit(extra, "third/reuse");
                    Msg("ph_ragdoll_smoke: multi third hit id=%u physx_instance_before=%d", extra->ID(),
                        OgsrPhysXRagdollRuntimeHasInstance() ? 1 : 0);
                }
                else
                {
                    Msg("! ph_ragdoll_smoke: multi third NPC not found");
                }
            }
            if (!multiFinished && frame - hitFrame >= 300)
            {
                multiFinished = true;
                CAI_Stalker* second = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(multiSecondId));
                CAI_Stalker* third = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(multiThirdId));
                Msg("ph_ragdoll_smoke: multi result second=%d shell=%d third=%d shell=%d physx_instance=%d",
                    second ? 1 : 0, second && second->PPhysicsShell() ? 1 : 0, third ? 1 : 0,
                    third && third->PPhysicsShell() ? 1 : 0,
                    OgsrPhysXRagdollRuntimeHasInstance() ? 1 : 0);
            }
            if (OgsrPhysXRagdollReloadRequested() && !reloadRequested && frame - hitFrame >= 300)
            {
                reloadRequested = true;
                Msg("ph_ragdoll_smoke: reload gate requesting level disconnect with active ragdoll");
                OgsrPhysXRagdollRequestLevelDisconnect();
            }
            if (reloadRequested)
                return;
            if (frame - hitFrame > 600 && !destroyed)
            {
                destroyed = true;
                CAI_Stalker* third = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(multiThirdId));
                if (third)
                {
                    Msg("ph_ragdoll_smoke: multi destroying third PhysX NPC id=%u", third->ID());
                    third->DestroyObject();
                }
            }
            return;
        }
        if (OgsrPhysXRagdollReloadRequested())
        {
            if (!reloadRequested && frame - hitFrame >= 300)
            {
                reloadRequested = true;
                Msg("ph_ragdoll_smoke: reload gate requesting level disconnect with active ragdoll");
                OgsrPhysXRagdollRequestLevelDisconnect();
            }
            return;
        }

        if (OgsrPhysXRagdollOdeSmokeRequested() && frame - hitFrame == 60)
        {
            CAI_Stalker* target = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_smokeTargetId));
            Msg("ph_ragdoll_smoke: ODE-only death check target=%d shell=%d physx_instance=%d",
                target ? 1 : 0, target && target->PPhysicsShell() ? 1 : 0,
                OgsrPhysXRagdollRuntimeHasInstance() ? 1 : 0);
        }
        if (OgsrPhysXRagdollVisualReplaceRequested() && !visualReplaced && frame - hitFrame >= 300)
        {
            visualReplaced = true;
            CAI_Stalker* target = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_smokeTargetId));
            if (target)
            {
                const shared_str oldVisual = target->cNameVisual();
                shared_str replacement;
                for (u32 index = 0; index < Level().Objects.o_count(); ++index)
                {
                    CAI_Stalker* other = smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index));
                    if (!other || other == target)
                        continue;
                    const shared_str candidate = other->cNameVisual();
                    if (!candidate.size() || candidate == oldVisual)
                        continue;
                    replacement = candidate;
                    break;
                }
                if (replacement.size())
                {
                    IKinematics* oldK = smart_cast<IKinematics*>(target->Visual());
                    const UpdateCallback oldCallback = oldK ? oldK->GetUpdateCallback() : nullptr;
                    void* const oldParam = oldK ? oldK->GetUpdateCallbackParam() : nullptr;
                    const std::uint32_t oldBoneCallbacks = countBoneCallbacks(oldK);
                    Msg("ph_ragdoll_smoke: replacing visual of NPC with active PhysX ragdoll id=%u "
                        "old=[%s] new=[%s] old_per_bone_callbacks=%u",
                        target->ID(), safeName(oldVisual), safeName(replacement), oldBoneCallbacks);
                    target->cNameVisual_set(replacement);
                    IKinematics* newK = smart_cast<IKinematics*>(target->Visual());
                    const bool globalPreserved =
                        newK && newK->GetUpdateCallback() == oldCallback && newK->GetUpdateCallbackParam() == oldParam;
                    Msg("ph_ragdoll_smoke: visual replacement completed without crash "
                        "global_callback_preserved=%d new_per_bone_callbacks=%u",
                        globalPreserved ? 1 : 0, countBoneCallbacks(newK));
                    if (CCharacterPhysicsSupport* support = target->character_physics_support())
                    {
                        support->OnBeforeVisualChange();
                        support->OnBeforeVisualChange();
                        Msg("ph_ragdoll_smoke: repeated visual cleanup completed without crash");
                    }
                }
                else
                {
                    Msg("! ph_ragdoll_smoke: no replacement visual available for id=%u", target->ID());
                }
            }
        }
        if (OgsrPhysXRagdollVisualReplaceOdeRequested() && !odeVisualReplaced && frame - hitFrame == 360)
        {
            odeVisualReplaced = true;
            CAI_Stalker* odeTarget = nullptr;
            for (u32 index = 0; index < Level().Objects.o_count(); ++index)
            {
                CAI_Stalker* candidate = smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index));
                if (!candidate || candidate->ID() == g_smokeTargetId || !candidate->PPhysicsShell() ||
                    !candidate->cNameVisual().size())
                    continue;
                odeTarget = candidate;
                break;
            }
            if (odeTarget)
            {
                const shared_str oldVisual = odeTarget->cNameVisual();
                shared_str replacement;
                for (u32 index = 0; index < Level().Objects.o_count(); ++index)
                {
                    CAI_Stalker* other = smart_cast<CAI_Stalker*>(Level().Objects.o_get_by_iterator(index));
                    if (!other || other == odeTarget)
                        continue;
                    const shared_str candidate = other->cNameVisual();
                    if (candidate.size() && candidate != oldVisual)
                    {
                        replacement = candidate;
                        break;
                    }
                }
                if (replacement.size())
                {
                    Msg("ph_ragdoll_smoke: replacing visual of ODE NPC id=%u old=[%s] new=[%s] shell=%d",
                        odeTarget->ID(), safeName(oldVisual), safeName(replacement),
                        odeTarget->PPhysicsShell() ? 1 : 0);
                    odeTarget->cNameVisual_set(replacement);
                    Msg("ph_ragdoll_smoke: ODE visual replacement completed without crash shell=%d",
                        odeTarget->PPhysicsShell() ? 1 : 0);
                }
                else
                {
                    Msg("! ph_ragdoll_smoke: no ODE replacement visual available");
                }
            }
            else
            {
                Msg("! ph_ragdoll_smoke: no ODE corpse with a visual found");
            }
        }
        if (!destroyed && frame - hitFrame > 600)
        {
            destroyed = true;
            CAI_Stalker* target = smart_cast<CAI_Stalker*>(Level().Objects.net_Find(g_smokeTargetId));
            if (target)
            {
                Msg("ph_ragdoll_smoke: destroying NPC with active PhysX ragdoll id=%u", target->ID());
                target->DestroyObject();
            }
        }
        return;
    }
    if (frame < 300)
        return;
    if (frame == 300)
    {
        Msg("ph_ragdoll_smoke: loaded stalkers:");
        for (u32 index = 0; index < Level().Objects.o_count(); ++index)
        {
            CObject* candidate = Level().Objects.o_get_by_iterator(index);
            CAI_Stalker* other = smart_cast<CAI_Stalker*>(candidate);
            if (other)
                Msg("ph_ragdoll_smoke:   [%s / %s] id=%u pos=(%.2f,%.2f,%.2f) alive=%d shell=%d",
                    safeName(other->cName()), safeName(other->cNameSect()), other->ID(), other->Position().x,
                    other->Position().y, other->Position().z, other->g_Alive() ? 1 : 0,
                    other->PPhysicsShell() ? 1 : 0);
        }
    }
    if (!g_smokeActorStartValid)
    {
        if (CActor* actor = smart_cast<CActor*>(Level().CurrentEntity()))
        {
            if (CCameraFirstEye* camera = static_cast<CCameraFirstEye*>(actor->cam_FirstEye()))
            {
                g_smokeActorStart = actor->Position();
                g_smokeActorDir = camera->Direction();
                if (g_smokeActorDir.magnitude() > EPS)
                    g_smokeActorDir.normalize();
                g_smokeActorStartValid = true;
            }
        }
    }

    CAI_Stalker* stalker = nullptr;
    CObject* object = nullptr;
    std::string description;
    if (!findTarget(stalker, object, description))
    {
        if (!listed && frame >= kSmokeGiveUpFrame)
        {
            listed = true;
            Msg("! ph_ragdoll_smoke: NPC target not found: %s", description.c_str());
            for (u32 index = 0; index < Level().Objects.o_count(); ++index)
            {
                CObject* candidate = Level().Objects.o_get_by_iterator(index);
                CAI_Stalker* other = smart_cast<CAI_Stalker*>(candidate);
                if (other)
                    Msg("ph_ragdoll_smoke: available [%s / %s] id=%u", safeName(other->cName()),
                        safeName(other->cNameSect()), other->ID());
            }
        }
        return;
    }
    if (!stalker->g_Alive() || stalker->PPhysicsShell())
    {
        if (!listed)
        {
            listed = true;
            Msg("! ph_ragdoll_smoke: target [%s] is not alive or already has a shell", description.c_str());
        }
        return;
    }

    if (!staged)
    {
        staged = true;
        stagedFrame = frame;
        Msg("ph_ragdoll_smoke: staging at actor_start=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f)", g_smokeActorStart.x,
            g_smokeActorStart.y, g_smokeActorStart.z, g_smokeActorDir.x, g_smokeActorDir.y, g_smokeActorDir.z);
        return;
    }
    if (frame - stagedFrame < 180)
        return;

    IKinematics* kinematics = smart_cast<IKinematics*>(stalker->Visual());
    if (!kinematics)
        return;

    // Diagnostic staging: move the target to the actor's front, refresh its
    // spatial sector (otherwise the renderer culls it at the old location), and
    // look at it from the actor's first-person camera.
    CActor* actor = smart_cast<CActor*>(Level().CurrentEntity());
    if (actor && actor->character_physics_support() && actor->character_physics_support()->movement())
    {
        if (CCameraFirstEye* camera = static_cast<CCameraFirstEye*>(actor->cam_FirstEye()))
        {
            Fvector forward = camera->Direction();
            if (forward.magnitude() > EPS)
                forward.normalize();
            Fvector right = camera->Right();
            if (right.magnitude() > EPS)
                right.normalize();
            forward.mul(3.0f);
            right.mul(2.0f);
            Fvector npcPosition = actor->Position();
            npcPosition.add(forward);
            npcPosition.add(right);
            npcPosition.y = actor->Position().y;
            stalker->XFORM().c.set(npcPosition);
            if (CCharacterPhysicsSupport* support = stalker->character_physics_support())
            {
                if (support->movement())
                    support->movement()->SetPosition(npcPosition);
            }
            stalker->spatial_move();
            Fvector look = npcPosition;
            look.y += 1.0f;
            camera->LookAtPoint(look);

            g_smokeCameraTarget = npcPosition;
            if (!g_smokeCameraActive)
            {
                g_smokeCameraActive = true;
                Device.seqFrame.Add(&g_smokeCamera, REG_PRIORITY_LOW);
            }
            Msg("ph_ragdoll_smoke: camera staged actor=(%.2f,%.2f,%.2f) npc=(%.2f,%.2f,%.2f)", actor->Position().x,
                actor->Position().y, actor->Position().z, npcPosition.x, npcPosition.y, npcPosition.z);
        }
    }

    const std::uint16_t bone = rootBoneId(*kinematics);
    Fvector direction(0.0f, 0.0f, 1.0f);
    Fvector position(0.0f, 0.0f, 0.0f);
    SHit hit(1000.0f, direction, Level().CurrentEntity(), bone, position, 8.0f, ALife::eHitTypeFireWound, 0.0f, false);
    fired = true;
    g_smokeTargetId = stalker->ID();
    Msg("ph_ragdoll_smoke: lethal hit target=[%s] id=%u bone=%u pos=(%.2f,%.2f,%.2f) ode_only=%d",
        description.c_str(), stalker->ID(), bone, stalker->Position().x, stalker->Position().y,
        stalker->Position().z, OgsrPhysXRagdollOdeSmokeRequested() ? 1 : 0);
    stalker->Hit(&hit);
}

#endif // OGSR_PHYSX_RAGDOLL
