#include "physx_ragdoll_v2_internal.h"

#include "PxPhysicsAPI.h"
#include "extensions/PxD6Joint.h"
#include "extensions/PxJointLimit.h"
#include "extensions/PxRigidBodyExt.h"
#include "geometry/PxConvexCoreGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace physics_physx
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kAngleEpsilon = 1.0e-4f;

bool fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}

bool finiteVec3(const physics_portable::Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

physics_portable::Transform fromPx(const PxTransform& value)
{
    physics_portable::Transform result;
    result.rotation = physics_portable::Quat{value.q.x, value.q.y, value.q.z, value.q.w};
    result.translation = physics_portable::Vec3{value.p.x, value.p.y, value.p.z};
    return result;
}

PxTransform toPx(const physics_portable::Transform& value)
{
    return PxTransform(PxVec3(value.translation.x, value.translation.y, value.translation.z),
                       PxQuat(value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w));
}

PxVec3 toPx(const physics_portable::Vec3& value)
{
    return PxVec3(value.x, value.y, value.z);
}

PxQuat toPx(const physics_portable::Quat& value)
{
    return PxQuat(value.x, value.y, value.z, value.w);
}

physics_portable::Vec3 fromPx(const PxVec3& value)
{
    return physics_portable::Vec3{value.x, value.y, value.z};
}

bool isFreeRange(const physics_portable::RagdollAxisLimit& limit)
{
    return limit.upper - limit.lower >= 2.0f * kPi - kAngleEpsilon;
}

bool isLockedRange(const physics_portable::RagdollAxisLimit& limit)
{
    return std::abs(limit.upper - limit.lower) <= kAngleEpsilon;
}

float clampConeAngle(float value)
{
    return std::min(std::max(std::abs(value), kAngleEpsilon), kPi - kAngleEpsilon);
}

std::pair<float, float> clampAngularPair(float lower, float upper, float minimum, float maximum)
{
    float low = std::min(std::max(lower, minimum), maximum);
    float high = std::min(std::max(upper, minimum), maximum);
    if (low > high)
        low = high = std::min(std::max(0.5f * (lower + upper), minimum), maximum);
    return {low, high};
}

struct ContactStats
{
    std::uint32_t contactCount = 0;
    std::uint32_t found = 0;
    std::uint32_t persist = 0;
    std::uint32_t lost = 0;
    // Number of contact pairs that reported TOUCH_FOUND or TOUCH_PERSISTS in
    // the last simulation step. Per-step value, not a cumulative counter, so
    // it cannot drift when a pair disappears without a TOUCH_LOST event.
    std::uint32_t stepActivePairs = 0;
    float totalImpulse = 0.0f;
    float maxPenetration = 0.0f;
};
} // namespace

struct PhysXRagdollInstance::Impl
{
    Impl(PhysXRagdollInstance& owner, PhysXRagdollScene& scene, const RagdollInstanceCreateInfo& info)
        : owner(owner), scene(scene), handle(info.handle), collisionInstanceId(info.collisionInstanceId)
    {
        if (!info.definition || !info.config)
        {
            conversionError = "ragdoll definition or instance config is null";
            return;
        }
        instanceConfig = *info.config;
        if (!v2::convertDefinition(*info.definition, description, initial, &conversionError))
            return;
        simulationConfig = v2::makeSimulationConfig(scene.config(), instanceConfig);
        rootToEntity = initial.rootToEntity;
    }

    PhysXRagdollInstance& owner;
    PhysXRagdollScene& scene;
    OgsrPhysX2RagdollHandle handle;
    std::uint32_t collisionInstanceId = 0;
    OgsrPhysX2RagdollInstanceConfig instanceConfig{};
    physics_portable::RagdollSimulationConfig simulationConfig{};
    physics_portable::RagdollDescription description;
    physics_portable::RagdollInitialState initial;
    physics_portable::Transform rootToEntity{};
    std::vector<physics_portable::Transform> bodyBindWorld;
    std::vector<PxRigidActor*> actors;
    std::vector<PxRigidDynamic*> dynamics;
    std::vector<PxD6Joint*> joints;
    std::vector<PxRigidActor*> bodyActors;
    float totalMass = 0.0f;
    float rootMass = 0.0f;
    std::uint32_t externalImpulses = 0;
    ContactStats contacts;
    bool created = false;
    std::string conversionError;
    std::string lastError;

    std::uint32_t filterGroup() const
    {
        return instanceConfig.collisionGroup ? instanceConfig.collisionGroup : OGSR_PHYSX2_GROUP_RAGDOLL;
    }

    std::uint32_t filterMask() const
    {
        return instanceConfig.collisionMask
                   ? instanceConfig.collisionMask
                   : (OGSR_PHYSX2_GROUP_RAGDOLL | OGSR_PHYSX2_GROUP_STATIC);
    }

    bool disableSelfCollision() const { return instanceConfig.disableSelfCollision != 0; }

    physics_portable::RagdollMaterialDesc materialDesc(std::uint16_t id) const
    {
        for (const physics_portable::RagdollMaterialDesc& material : description.materials)
        {
            if (material.id == id)
                return material;
        }
        physics_portable::RagdollMaterialDesc fallback;
        fallback.friction = 0.8f;
        fallback.restitution = 0.0f;
        return fallback;
    }

    bool computeBindWorlds()
    {
        bodyBindWorld.assign(description.bodies.size(), physics_portable::Transform{});
        for (physics_portable::BodyId body = 0; body < description.bodies.size(); ++body)
        {
            physics_portable::Transform world = description.bodies[body].bindPose;
            physics_portable::BodyId parent = description.bodies[body].parentBody;
            std::size_t depth = 0;
            while (parent != physics_portable::kInvalidBody)
            {
                if (parent >= description.bodies.size() || ++depth > description.bodies.size())
                    return fail(&lastError, "body bind hierarchy is cyclic or out of range");
                world = description.bodies[parent].bindPose * world;
                parent = description.bodies[parent].parentBody;
            }
            if (!physics_portable::isFinite(world))
                return fail(&lastError, "body bind hierarchy produces a non-finite transform");
            bodyBindWorld[body] = world;
        }
        return true;
    }

    bool createShape(PxRigidActor& actor, const physics_portable::RagdollShapeDesc& source)
    {
        if (source.flags & physics_portable::shapeFlag(physics_portable::ShapeFlag::NoPhysics))
            return true;

        PxGeometry* geometry = nullptr;
        PxBoxGeometry boxGeometry;
        PxSphereGeometry sphereGeometry;
        PxConvexCoreGeometry cylinderGeometry;

        switch (source.type)
        {
        case physics_portable::ShapeType::Box:
            boxGeometry = PxBoxGeometry(source.halfExtents.x, source.halfExtents.y, source.halfExtents.z);
            geometry = &boxGeometry;
            break;
        case physics_portable::ShapeType::Sphere:
            sphereGeometry = PxSphereGeometry(source.radius);
            geometry = &sphereGeometry;
            break;
        case physics_portable::ShapeType::Cylinder:
        {
            const PxConvexCore::Cylinder core(source.height, source.radius);
            cylinderGeometry = PxConvexCoreGeometry(core, source.margin);
            geometry = &cylinderGeometry;
            break;
        }
        default:
            return fail(&lastError, "unsupported shape type");
        }

        PxMaterial* material = scene.materialFor(source.materialId, materialDesc(source.materialId), &lastError);
        if (!material)
            return false;
        PxShape* shape = scene.physics().createShape(*geometry, *material, true);
        if (!shape)
            return fail(&lastError, "PxShape creation failed");

        shape->setLocalPose(toPx(source.local));
        shape->setContactOffset(std::max(1.0e-4f, std::max(simulationConfig.restOffset, simulationConfig.contactOffset)));
        shape->setRestOffset(simulationConfig.restOffset);
        shape->setSimulationFilterData(PxFilterData(filterGroup(), filterMask(), collisionInstanceId,
                                                    OGSR_PHYSX2_FILTER_RAGDOLL |
                                                        (disableSelfCollision()
                                                             ? OGSR_PHYSX2_FILTER_DISABLE_SELF_COLLISION
                                                             : 0)));
        actor.attachShape(*shape);
        shape->release();
        return true;
    }

    bool makeJointFrames(const physics_portable::RagdollJointDesc& source, PxTransform& parentFrame,
                         PxTransform& childFrame)
    {
        parentFrame = toPx(source.parentFrame);
        childFrame = toPx(source.childFrame);
        if (source.kind != physics_portable::JointKind::Hinge &&
            source.kind != physics_portable::JointKind::Slider &&
            source.kind != physics_portable::JointKind::Euler3)
            return true;
        if (source.axisCount == 0)
            return fail(&lastError, "joint needs an axis but has none");

        const physics_portable::Transform parentBind = bodyBindWorld[source.parentBody];
        const physics_portable::Transform childBind = bodyBindWorld[source.childBody];
        const physics_portable::Quat relativeRotation = physics_portable::inverse(parentBind).rotation * childBind.rotation;

        auto axisInChildSpace = [&](const physics_portable::RagdollAxisLimit& axis) {
            physics_portable::Vec3 result = axis.axis;
            switch (axis.space)
            {
            case physics_portable::AxisSpace::Child:
                break;
            case physics_portable::AxisSpace::Parent:
                result = physics_portable::rotate(physics_portable::conjugate(relativeRotation), axis.axis);
                break;
            case physics_portable::AxisSpace::World:
                result = physics_portable::rotate(physics_portable::conjugate(childBind.rotation), axis.axis);
                break;
            }
            return physics_portable::normalized(result);
        };

        if (source.kind == physics_portable::JointKind::Hinge ||
            source.kind == physics_portable::JointKind::Slider)
        {
            const physics_portable::Vec3 axis = axisInChildSpace(source.axes[0]);
            if (physics_portable::lengthSquared(axis) < 1.0e-8f)
                return fail(&lastError, "joint axis is zero");
            const PxQuat childRotation = PxShortestRotation(PxVec3(1.0f, 0.0f, 0.0f), toPx(axis));
            const PxQuat parentRotation = toPx(relativeRotation) * childRotation;
            parentFrame = PxTransform(parentFrame.p, parentRotation);
            childFrame = PxTransform(childFrame.p, childRotation);
            return true;
        }

        if (source.axisCount < 3)
            return fail(&lastError, "Euler3 joint needs three axes");
        const PxVec3 x = toPx(axisInChildSpace(source.axes[0]));
        const PxVec3 zInput = toPx(axisInChildSpace(source.axes[2]));
        const PxVec3 y = zInput.cross(x).getNormalized();
        if (x.magnitudeSquared() < 1.0e-8f || y.magnitudeSquared() < 1.0e-8f)
            return fail(&lastError, "Euler3 axes are degenerate");
        const PxVec3 z = x.cross(y);
        const PxQuat childRotation = PxQuat(PxMat33(x, y, z));
        const PxQuat parentRotation = toPx(relativeRotation) * childRotation;
        parentFrame = PxTransform(parentFrame.p, parentRotation);
        childFrame = PxTransform(childFrame.p, childRotation);
        return true;
    }

    bool configureJoint(PxD6Joint& joint, const physics_portable::RagdollJointDesc& source)
    {
        auto motionFor = [](const physics_portable::RagdollAxisLimit& limit) -> PxD6Motion::Enum {
            if (isFreeRange(limit))
                return PxD6Motion::eFREE;
            if (isLockedRange(limit))
                return PxD6Motion::eLOCKED;
            return PxD6Motion::eLIMITED;
        };
        auto linearMotionFor = [](const physics_portable::RagdollAxisLimit& limit) -> PxD6Motion::Enum {
            return isLockedRange(limit) ? PxD6Motion::eLOCKED : PxD6Motion::eLIMITED;
        };

        joint.setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
        joint.setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
        joint.setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
        joint.setMotion(PxD6Axis::eTWIST, PxD6Motion::eLOCKED);
        joint.setMotion(PxD6Axis::eSWING1, PxD6Motion::eLOCKED);
        joint.setMotion(PxD6Axis::eSWING2, PxD6Motion::eLOCKED);

        switch (source.kind)
        {
        case physics_portable::JointKind::Ball:
            joint.setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
            joint.setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
            joint.setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
            return true;
        case physics_portable::JointKind::Fixed:
            return true;
        case physics_portable::JointKind::Hinge:
            if (source.axisCount < 1)
                return fail(&lastError, "hinge joint has no axis");
            joint.setMotion(PxD6Axis::eTWIST, motionFor(source.axes[0]));
            if (joint.getMotion(PxD6Axis::eTWIST) == PxD6Motion::eLIMITED)
            {
                const auto range = clampAngularPair(source.axes[0].lower, source.axes[0].upper,
                                                    -kPi + kAngleEpsilon, kPi - kAngleEpsilon);
                joint.setTwistLimit(PxJointAngularLimitPair(range.first, range.second));
            }
            return true;
        case physics_portable::JointKind::Euler3:
        {
            if (source.axisCount < 3)
                return fail(&lastError, "Euler3 joint has fewer than three axes");
            const PxD6Motion::Enum twistMotion = motionFor(source.axes[0]);
            const PxD6Motion::Enum swing1Motion = motionFor(source.axes[1]);
            const PxD6Motion::Enum swing2Motion = motionFor(source.axes[2]);
            joint.setMotion(PxD6Axis::eTWIST, twistMotion);
            joint.setMotion(PxD6Axis::eSWING1, swing1Motion);
            joint.setMotion(PxD6Axis::eSWING2, swing2Motion);

            if (twistMotion == PxD6Motion::eLIMITED)
            {
                const auto range = clampAngularPair(source.axes[0].lower, source.axes[0].upper,
                                                    -kPi + kAngleEpsilon, kPi - kAngleEpsilon);
                joint.setTwistLimit(PxJointAngularLimitPair(range.first, range.second));
            }

            const bool swing1Locked = swing1Motion == PxD6Motion::eLOCKED;
            const bool swing2Locked = swing2Motion == PxD6Motion::eLOCKED;
            const bool swing1Limited = swing1Motion == PxD6Motion::eLIMITED;
            const bool swing2Limited = swing2Motion == PxD6Motion::eLIMITED;
            const bool bothSwingLimited = swing1Limited && swing2Limited;
            const bool oneSwingLimitedOneLocked =
                (swing1Limited && swing2Locked) || (swing2Limited && swing1Locked);
            const bool pyramidSupported = bothSwingLimited || oneSwingLimitedOneLocked;
            const bool pyramidRequested =
                source.swingLimitMode == physics_portable::SwingLimitMode::Pyramid ||
                (source.swingLimitMode == physics_portable::SwingLimitMode::Auto && pyramidSupported);

            if (pyramidRequested && pyramidSupported)
            {
                const auto yRange = clampAngularPair(
                    swing1Limited ? source.axes[1].lower : 0.0f,
                    swing1Limited ? source.axes[1].upper : 0.0f, -kPi + kAngleEpsilon, kPi - kAngleEpsilon);
                const auto zRange = clampAngularPair(
                    swing2Limited ? source.axes[2].lower : 0.0f,
                    swing2Limited ? source.axes[2].upper : 0.0f, -kPi + kAngleEpsilon, kPi - kAngleEpsilon);
                joint.setPyramidSwingLimit(PxJointLimitPyramid(yRange.first, yRange.second, zRange.first, zRange.second));
            }
            else if (swing1Motion == PxD6Motion::eLIMITED || swing2Motion == PxD6Motion::eLIMITED)
            {
                const float yAngle = swing1Motion == PxD6Motion::eLIMITED
                                         ? clampConeAngle(std::max(std::abs(source.axes[1].lower), std::abs(source.axes[1].upper)))
                                         : kPi - kAngleEpsilon;
                const float zAngle = swing2Motion == PxD6Motion::eLIMITED
                                         ? clampConeAngle(std::max(std::abs(source.axes[2].lower), std::abs(source.axes[2].upper)))
                                         : kPi - kAngleEpsilon;
                joint.setSwingLimit(PxJointLimitCone(yAngle, zAngle));
            }
            return true;
        }
        case physics_portable::JointKind::Slider:
            if (source.axisCount < 1)
                return fail(&lastError, "slider joint has no axis");
            joint.setMotion(PxD6Axis::eX, linearMotionFor(source.axes[0]));
            if (joint.getMotion(PxD6Axis::eX) == PxD6Motion::eLIMITED)
            {
                joint.setLinearLimit(PxD6Axis::eX,
                                     PxJointLinearLimitPair(scene.physics().getTolerancesScale(),
                                                            source.axes[0].lower, source.axes[0].upper));
            }
            if (source.axisCount >= 2)
            {
                joint.setMotion(PxD6Axis::eTWIST, motionFor(source.axes[1]));
                if (joint.getMotion(PxD6Axis::eTWIST) == PxD6Motion::eLIMITED)
                {
                    const auto range = clampAngularPair(source.axes[1].lower, source.axes[1].upper,
                                                        -kPi + kAngleEpsilon, kPi - kAngleEpsilon);
                    joint.setTwistLimit(PxJointAngularLimitPair(range.first, range.second));
                }
            }
            return true;
        case physics_portable::JointKind::Hinge2:
            return fail(&lastError, "Hinge2 is not implemented in P3.1-B");
        default:
            return fail(&lastError, "unsupported joint kind");
        }
    }

    bool createActors()
    {
        void* contactTag = scene.registerContact(collisionInstanceId, &owner);
        actors.reserve(description.bodies.size());
        dynamics.reserve(description.bodies.size());
        for (std::size_t index = 0; index < description.bodies.size(); ++index)
        {
            const physics_portable::RagdollBodyDesc& body = description.bodies[index];
            const physics_portable::Transform& world = initial.bodyWorld[index];
            if (!physics_portable::isFinite(world))
                return fail(&lastError, "initial body transform is not finite");

            const bool promoteRoot = body.role == physics_portable::BodyRole::StaticRoot &&
                                     body.id == description.rootBody && simulationConfig.promoteStaticRoot &&
                                     !body.shapes.empty();
            const bool isStatic =
                (body.role == physics_portable::BodyRole::StaticRoot || body.isFixed) && !promoteRoot;
            PxRigidActor* actor = nullptr;
            if (isStatic)
            {
                actor = scene.physics().createRigidStatic(toPx(world));
            }
            else
            {
                PxRigidDynamic* dynamic = scene.physics().createRigidDynamic(toPx(world));
                if (!dynamic)
                    return fail(&lastError, "PxRigidDynamic creation failed");
                dynamic->setSolverIterationCounts(simulationConfig.solverPositionIterations,
                                                  simulationConfig.solverVelocityIterations);
                dynamic->setMaxLinearVelocity(150.0f);
                dynamic->setMaxAngularVelocity(9.817f);
                dynamic->setLinearDamping(simulationConfig.linearDamping);
                dynamic->setAngularDamping(simulationConfig.angularDamping);
                dynamics.push_back(dynamic);
                actor = dynamic;
            }
            if (!actor)
                return fail(&lastError, "PxRigidActor creation failed");
            actor->userData = contactTag;
            actors.push_back(actor);

            for (const physics_portable::RagdollShapeDesc& shape : body.shapes)
            {
                if (!createShape(*actor, shape))
                    return false;
            }

            PxU32 simulationShapeCount = 0;
            {
                const PxU32 shapeCount = actor->getNbShapes();
                std::vector<PxShape*> attachedShapes(shapeCount);
                actor->getShapes(attachedShapes.data(), shapeCount);
                for (PxShape* attached : attachedShapes)
                {
                    if (attached && (attached->getFlags() & PxShapeFlag::eSIMULATION_SHAPE))
                        ++simulationShapeCount;
                }
            }

            if (!isStatic)
            {
                PxRigidDynamic* dynamic = static_cast<PxRigidDynamic*>(actor);
                if (promoteRoot)
                {
                    if (simulationShapeCount == 0)
                        return fail(&lastError, "promoted static root has no simulation shapes");
                    if (!PxRigidBodyExt::updateMassAndInertia(*dynamic, simulationConfig.staticRootDensity))
                        return fail(&lastError, "updateMassAndInertia failed for promoted static root");
                    rootMass = dynamic->getMass();
                    const PxVec3 rootInertia = dynamic->getMassSpaceInertiaTensor();
                    const PxTransform rootMassPose = dynamic->getCMassLocalPose();
                    if (!(rootMass > 0.0f) || !std::isfinite(rootMass) || !std::isfinite(rootInertia.x) ||
                        !std::isfinite(rootInertia.y) || !std::isfinite(rootInertia.z) ||
                        !(rootInertia.x > 0.0f) || !(rootInertia.y > 0.0f) || !(rootInertia.z > 0.0f) ||
                        !rootMassPose.isValid())
                        return fail(&lastError, "promoted static root has invalid mass properties");
                }
                else
                {
                    physics_portable::Vec3 eigenvalues{};
                    physics_portable::Quat principalAxes{};
                    if (!physics_portable::diagonalizeInertia(body.inertia, eigenvalues, principalAxes, &lastError))
                        return false;
                    dynamic->setMass(body.mass);
                    dynamic->setCMassLocalPose(PxTransform(toPx(body.centerOfMass), toPx(principalAxes)));
                    dynamic->setMassSpaceInertiaTensor(toPx(eigenvalues));
                }
                totalMass += dynamic->getMass();
                dynamic->setLinearVelocity(toPx(initial.linearVelocity[index]), false);
                dynamic->setAngularVelocity(toPx(initial.angularVelocity[index]), false);
                if (!initial.enabled.empty() && !initial.enabled[index])
                    dynamic->setActorFlag(PxActorFlag::eDISABLE_SIMULATION, true);
            }

            scene.scene().addActor(*actor);
        }

        bodyActors.resize(description.bodies.size(), nullptr);
        for (std::size_t index = 0; index < description.bodies.size(); ++index)
            bodyActors[index] = actors[index];
        return true;
    }

    bool createJoints()
    {
        for (const physics_portable::RagdollJointDesc& source : description.joints)
        {
            PxRigidActor* parent = source.parentBody < bodyActors.size() ? bodyActors[source.parentBody] : nullptr;
            PxRigidActor* child = source.childBody < bodyActors.size() ? bodyActors[source.childBody] : nullptr;
            if (!parent || !child)
                return fail(&lastError, "joint references an unknown body");
            PxTransform parentFrame;
            PxTransform childFrame;
            if (!makeJointFrames(source, parentFrame, childFrame))
                return false;
            PxD6Joint* joint = PxD6JointCreate(scene.physics(), parent, parentFrame, child, childFrame);
            if (!joint)
                return fail(&lastError, "PxD6JointCreate failed");
            if (!configureJoint(*joint, source))
            {
                joint->release();
                return false;
            }
            if (source.breakable)
                joint->setBreakForce(source.breakForce, source.breakTorque);
            joints.push_back(joint);
        }
        return true;
    }

    bool initialize(std::string* error)
    {
        if (!conversionError.empty())
            return fail(error, conversionError);
        if (!physics_portable::validateDescription(description, &lastError))
            return fail(error, lastError);
        if (!physics_portable::validateInitialState(description, initial, &lastError))
            return fail(error, lastError);
        if (!computeBindWorlds())
            return fail(error, lastError);
        if (!createActors())
        {
            destroy();
            return fail(error, lastError);
        }
        if (!createJoints())
        {
            destroy();
            return fail(error, lastError);
        }
        created = true;
        return true;
    }

    void destroy()
    {
        for (PxD6Joint* joint : joints)
        {
            if (joint)
                joint->release();
        }
        joints.clear();
        for (PxRigidActor* actor : actors)
        {
            if (!actor)
                continue;
            actor->userData = nullptr;
            actor->release();
        }
        actors.clear();
        dynamics.clear();
        bodyActors.clear();
        scene.unregisterContact(collisionInstanceId);
        created = false;
    }

    bool pose(physics_portable::RagdollPose& result, std::string* error) const
    {
        result = physics_portable::RagdollPose{};
        result.stepIndex = scene.stepIndex();
        if (!created)
            return fail(error, "ragdoll is not created");
        result.bodyWorld.resize(bodyActors.size());
        for (std::size_t index = 0; index < bodyActors.size(); ++index)
        {
            const PxRigidActor* actor = bodyActors[index];
            if (actor)
                result.bodyWorld[index] = fromPx(actor->getGlobalPose());
        }
        if (description.rootBody < result.bodyWorld.size())
            result.entityWorld = result.bodyWorld[description.rootBody] * rootToEntity;
        return true;
    }

    PxRigidDynamic* dynamicBody(std::uint16_t body) const
    {
        if (body >= bodyActors.size())
            return nullptr;
        PxRigidActor* actor = bodyActors[body];
        return actor ? actor->is<PxRigidDynamic>() : nullptr;
    }

    bool bodyMass(std::uint16_t body, BodyMassProperties& properties, std::string* error) const
    {
        properties = BodyMassProperties{};
        if (!created)
            return fail(error, "ragdoll is not created");
        PxRigidDynamic* dynamic = dynamicBody(body);
        if (!dynamic)
            return fail(error, "mass properties target is not a dynamic body");
        const PxTransform actorWorld = dynamic->getGlobalPose();
        const PxTransform massLocal = dynamic->getCMassLocalPose();
        const PxTransform massWorld = actorWorld * massLocal;
        const float mass = dynamic->getMass();
        const PxVec3 inertia = dynamic->getMassSpaceInertiaTensor();
        if (!(mass > 0.0f) || !std::isfinite(mass) || !std::isfinite(inertia.x) || !std::isfinite(inertia.y) ||
            !std::isfinite(inertia.z) || !massLocal.isValid())
            return fail(error, "dynamic body has invalid mass properties");
        properties.body = body;
        properties.mass = mass;
        properties.actorWorld = fromPx(actorWorld);
        properties.massWorld = fromPx(massWorld);
        properties.centerOfMassLocal = fromPx(massLocal.p);
        properties.worldCenterOfMass = fromPx(massWorld.p);
        properties.inertiaMassSpace = fromPx(inertia);
        return true;
    }

    bool applyImpulse(std::uint16_t body, const physics_portable::Vec3& worldPosition,
                      const physics_portable::Vec3& impulse, std::string* error)
    {
        if (!created)
            return fail(error, "ragdoll is not created");
        PxRigidDynamic* dynamic = dynamicBody(body);
        if (!dynamic)
            return fail(error, "impulse target is not a dynamic body");
        if (!finiteVec3(worldPosition) || !finiteVec3(impulse))
            return fail(error, "impulse input is not finite");
        PxRigidBodyExt::addForceAtPos(*dynamic, toPx(impulse), toPx(worldPosition), PxForceMode::eIMPULSE, true);
        ++externalImpulses;
        return true;
    }

    bool applyCentralImpulse(std::uint16_t body, const physics_portable::Vec3& impulse, std::string* error)
    {
        BodyMassProperties properties;
        if (!bodyMass(body, properties, error))
            return false;
        return applyImpulse(body, properties.worldCenterOfMass, impulse, error);
    }

    OgsrPhysXRagdollDiagnostics diagnostics(const physics_portable::Vec3& referencePosition) const
    {
        OgsrPhysXRagdollDiagnostics result{};
        result.stepIndex = scene.stepIndex();
        result.externalImpulses = externalImpulses;
        result.contactCount = contacts.contactCount;
        result.maxPenetration = contacts.maxPenetration;
        result.totalContactImpulse = contacts.totalImpulse;
        result.contactFound = contacts.found;
        result.contactPersist = contacts.persist;
        result.contactLost = contacts.lost;
        result.activeContactPairs = contacts.stepActivePairs;
        result.totalMass = totalMass;
        result.rootMass = rootMass;
        if (!created)
            return result;

        const PxVec3 gravity = PxVec3(0.0f, simulationConfig.gravity.y, 0.0f);
        const PxVec3 reference = toPx(referencePosition);
        for (const PxRigidDynamic* dynamic : dynamics)
        {
            if (!dynamic)
                continue;
            ++result.dynamicBodies;
            if (!dynamic->isSleeping())
                ++result.awakeBodies;
            const float mass = dynamic->getMass();
            if (!(mass > 0.0f))
                continue;
            const PxTransform actorWorld = dynamic->getGlobalPose();
            const PxTransform massWorld = actorWorld * dynamic->getCMassLocalPose();
            const PxVec3 linear = dynamic->getLinearVelocity();
            const PxVec3 angular = dynamic->getAngularVelocity();
            result.kineticEnergy += 0.5 * static_cast<double>(mass) * linear.magnitudeSquared();
            const PxVec3 angularMass = massWorld.q.rotateInv(angular);
            const PxVec3 inertia = dynamic->getMassSpaceInertiaTensor();
            result.kineticEnergy += 0.5 * static_cast<double>(inertia.x * angularMass.x * angularMass.x +
                                                              inertia.y * angularMass.y * angularMass.y +
                                                              inertia.z * angularMass.z * angularMass.z);
            result.potentialEnergy += static_cast<double>(-mass) * gravity.dot(massWorld.p - reference);
            result.maxAngularSpeed = std::max(result.maxAngularSpeed, angular.magnitude());
        }
        result.totalEnergy = result.kineticEnergy + result.potentialEnergy;

        for (std::size_t index = 0; index < joints.size(); ++index)
        {
            const PxD6Joint* joint = joints[index];
            if (!joint)
                continue;
            const PxTransform relative = joint->getRelativeTransform();
            result.maxJointAnchorError = std::max(result.maxJointAnchorError, relative.p.magnitude());
            if (index >= description.joints.size())
                continue;
            const physics_portable::RagdollJointDesc& source = description.joints[index];
            auto normalizeAngle = [](float angle) {
                while (angle > kPi)
                    angle -= 2.0f * kPi;
                while (angle <= -kPi)
                    angle += 2.0f * kPi;
                return angle;
            };
            auto checkLimit = [&](const physics_portable::RagdollAxisLimit& limit, float angle) {
                const float range = limit.upper - limit.lower;
                if (range >= 2.0f * kPi - kAngleEpsilon)
                    return;
                angle = normalizeAngle(angle);
                const float violation = std::max(0.0f, std::max(angle - limit.upper, limit.lower - angle));
                result.maxJointLimitError = std::max(result.maxJointLimitError, violation);
            };
            if (source.kind == physics_portable::JointKind::Hinge && source.axisCount >= 1)
                checkLimit(source.axes[0], joint->getTwistAngle());
            else if (source.kind == physics_portable::JointKind::Euler3 && source.axisCount >= 3)
            {
                checkLimit(source.axes[0], joint->getTwistAngle());
                checkLimit(source.axes[1], joint->getSwingYAngle());
                checkLimit(source.axes[2], joint->getSwingZAngle());
            }
        }
        return result;
    }

    OgsrPhysXRagdollInfo info() const
    {
        OgsrPhysXRagdollInfo result{};
        result.bodyCount = static_cast<std::uint32_t>(bodyActors.size());
        result.jointCount = static_cast<std::uint32_t>(joints.size());
        result.staticTriangles = scene.staticTriangleCount();
        result.totalMass = totalMass;
        result.rootMass = rootMass;
        return result;
    }

    void resetContactStep()
    {
        contacts.contactCount = 0;
        contacts.found = 0;
        contacts.persist = 0;
        contacts.lost = 0;
        contacts.stepActivePairs = 0;
        contacts.totalImpulse = 0.0f;
        contacts.maxPenetration = 0.0f;
    }

    void onContact(std::uint32_t contactCount, float impulse, float penetration, bool found, bool persist, bool lost)
    {
        contacts.contactCount += contactCount;
        contacts.totalImpulse += impulse;
        contacts.maxPenetration = std::max(contacts.maxPenetration, penetration);
        if (found || persist)
            ++contacts.stepActivePairs;
        if (found)
            ++contacts.found;
        if (persist)
            ++contacts.persist;
        if (lost)
            ++contacts.lost;
    }
};

PhysXRagdollInstance::PhysXRagdollInstance(PhysXRagdollScene& scene, const RagdollInstanceCreateInfo& info)
    : impl_(std::make_unique<Impl>(*this, scene, info))
{
}

PhysXRagdollInstance::~PhysXRagdollInstance()
{
    if (impl_)
        impl_->destroy();
}

bool PhysXRagdollInstance::initialize(std::string* error)
{
    return impl_->initialize(error);
}

void PhysXRagdollInstance::destroy()
{
    impl_->destroy();
}

bool PhysXRagdollInstance::pose(physics_portable::RagdollPose& pose, std::string* error) const
{
    return impl_->pose(pose, error);
}

bool PhysXRagdollInstance::applyImpulse(std::uint16_t body, const physics_portable::Vec3& worldPosition,
                                        const physics_portable::Vec3& impulse, std::string* error)
{
    return impl_->applyImpulse(body, worldPosition, impulse, error);
}

bool PhysXRagdollInstance::applyCentralImpulse(std::uint16_t body, const physics_portable::Vec3& impulse,
                                               std::string* error)
{
    return impl_->applyCentralImpulse(body, impulse, error);
}

bool PhysXRagdollInstance::bodyMass(std::uint16_t body, BodyMassProperties& properties, std::string* error) const
{
    return impl_->bodyMass(body, properties, error);
}

OgsrPhysXRagdollDiagnostics PhysXRagdollInstance::diagnostics(const physics_portable::Vec3& referencePosition) const
{
    return impl_->diagnostics(referencePosition);
}

OgsrPhysXRagdollInfo PhysXRagdollInstance::info() const
{
    return impl_->info();
}

std::uint32_t PhysXRagdollInstance::collisionInstanceId() const { return impl_->collisionInstanceId; }
OgsrPhysX2RagdollHandle PhysXRagdollInstance::handle() const { return impl_->handle; }
std::uint32_t PhysXRagdollInstance::bodyCount() const { return static_cast<std::uint32_t>(impl_->bodyActors.size()); }
std::uint32_t PhysXRagdollInstance::jointCount() const { return static_cast<std::uint32_t>(impl_->joints.size()); }
bool PhysXRagdollInstance::isCreated() const { return impl_->created; }
bool PhysXRagdollInstance::isSceneStepping() const { return impl_->scene.isStepping(); }

void PhysXRagdollInstance::resetContactStep() { impl_->resetContactStep(); }
void PhysXRagdollInstance::onContact(std::uint32_t contactCount, float impulse, float penetration, bool found,
                                     bool persist, bool lost)
{
    impl_->onContact(contactCount, impulse, penetration, found, persist, lost);
}

const std::vector<PxRigidActor*>& PhysXRagdollInstance::actors() const { return impl_->actors; }

} // namespace physics_physx
