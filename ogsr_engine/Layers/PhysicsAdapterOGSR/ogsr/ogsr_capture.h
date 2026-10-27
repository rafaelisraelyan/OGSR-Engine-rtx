#pragma once

// Debug-only OGSR collector. This file is intentionally NOT part of the
// production build and is not referenced by Engine.sln.
//
// It requires an OGSR debug build with a prepared CPhysicsShell whose elements
// and joints have already been created by Build() (preBuild alone has no ODE
// bodies and mass_Center() would assert). The caller owns shell cleanup and
// must Deactivate/destroy it after the dump. This function does not run the
// simulation, add bodies to CPHWorld, or create a PhysX object.
//
// Required include paths: ogsr_engine/Include, ogsr_engine/xr_3da,
// ogsr_engine/xrPhysics, ogsr_engine/xrCore.

#if defined(OGSR_DIAGNOSTIC_CAPTURE) || defined(OGSR_PHYSX_RAGDOLL)

#include "physics_adapter_ogsr/capture.h"
#include "physics_portable/json.h"
#include "physics_portable/row_matrix.h"

#include "../../../Include/xrRender/Kinematics.h"
#include "../../../xr_3da/bone.h"
#include "../../../xrPhysics/PhysicsShell.h"
#include "../../../xrPhysics/PHShell.h"
#include "../../../xrPhysics/PHElement.h"
#include "../../../xrPhysics/PHJoint.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <string>

namespace physics_adapter_ogsr
{
namespace detail
{
inline physics_portable::Vec3 fromFvector(const Fvector& value)
{
    return physics_portable::Vec3{value.x, value.y, value.z};
}

inline physics_portable::Transform fromFmatrix(const Fmatrix& value)
{
    physics_portable::RowMatrix4 matrix{};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
            matrix.m[row][column] = value.m[row][column];
    }
    return physics_portable::transformFromRowMatrix(matrix);
}

inline physics_portable::ShapeType shapeType(const SBoneShape& shape)
{
    switch (shape.type)
    {
    case SBoneShape::stBox: return physics_portable::ShapeType::Box;
    case SBoneShape::stSphere: return physics_portable::ShapeType::Sphere;
    case SBoneShape::stCylinder: return physics_portable::ShapeType::Cylinder;
    default: return physics_portable::ShapeType::Box;
    }
}

inline physics_portable::Transform boxTransform(const Fobb& box)
{
    Fmatrix local;
    local.identity();
    local.i.set(box.m_rotate.i);
    local.j.set(box.m_rotate.j);
    local.k.set(box.m_rotate.k);
    local.c.set(box.m_translate);
    return fromFmatrix(local);
}

inline physics_portable::Transform cylinderTransform(const Fcylinder& cylinder)
{
    Fvector x = cylinder.m_direction;
    x.normalize();
    Fvector y, z;
    Fvector::generate_orthonormal_basis(x, y, z);
    // generate_orthonormal_basis returns z = y x x (a left-handed basis).
    // The contract/PhysX cylinder needs a proper rotation, so rebuild z = x x y.
    z.crossproduct(x, y);
    Fmatrix local;
    local.identity();
    local.i.set(x);
    local.j.set(y);
    local.k.set(z);
    local.c.set(cylinder.m_center);
    return fromFmatrix(local);
}

inline SourceJointType sourceJointType(EJointType type)
{
    switch (type)
    {
    case jtRigid: return SourceJointType::Rigid;
    case jtCloth: return SourceJointType::Cloth;
    case jtJoint: return SourceJointType::Joint;
    case jtWheel: return SourceJointType::Wheel;
    case jtNone: return SourceJointType::None;
    case jtSlider: return SourceJointType::Slider;
    default: return SourceJointType::Rigid;
    }
}

inline physics_portable::AxisSpace axisSpace(CPhysicsJoint::eVs space)
{
    switch (space)
    {
    case CPhysicsJoint::vs_first: return physics_portable::AxisSpace::Parent;
    case CPhysicsJoint::vs_second: return physics_portable::AxisSpace::Child;
    case CPhysicsJoint::vs_global: return physics_portable::AxisSpace::World;
    default: return physics_portable::AxisSpace::Child;
    }
}
} // namespace detail

// The caller must provide a prepared shell whose elements/joints exist.
// A diagnostic shell can be created without RunSimulation; this function never
// activates it and never changes production state.
inline bool captureFromOgsr(IKinematics& kinematics,
                            CPhysicsShell& shell,
                            const Fmatrix& entityWorld,
                            const Fvector& linearVelocity,
                            physics_portable::RagdollCaptureDocument& document,
                            std::string* error)
{
    SkeletonCapture capture;
    capture.skeletonName = kinematics.getDebugName().c_str();
    capture.entityWorld = detail::fromFmatrix(entityWorld);
    capture.linearVelocity = detail::fromFvector(linearVelocity);
    capture.visual = capture.skeletonName;
    capture.ogsrCommit = "diagnostic";

    const u16 count = kinematics.LL_BoneCount();
    capture.bones.resize(count);
    capture.currentBoneModelPose.resize(count);

    for (u16 index = 0; index < count; ++index)
    {
        CBoneData& boneData = kinematics.LL_GetData(index);
        CaptureBone& bone = capture.bones[index];
        bone.id = index;
        bone.parent = boneData.GetParentID();
        bone.name = boneData.name.c_str();
        bone.bindLocal = detail::fromFmatrix(boneData.bind_transform);
        bone.visible = kinematics.LL_GetBoneVisible(index) != FALSE;
        capture.currentBoneModelPose[index] = detail::fromFmatrix(kinematics.LL_GetTransform(index));

        const SJointIKData& ik = boneData.IK_data;
        bone.jointType = detail::sourceJointType(ik.type);
        bone.breakable = ik.ik_flags.test(SJointIKData::flBreakable);
        bone.breakForce = ik.break_force;
        bone.breakTorque = ik.break_torque;
        bone.friction = ik.friction;

        const SBoneShape& sourceShape = boneData.shape;
        if (sourceShape.type != SBoneShape::stNone && !sourceShape.flags.test(SBoneShape::sfNoPhysics))
        {
            bone.hasShape = true;
            bone.shape.type = detail::shapeType(sourceShape);
            bone.shape.flags = sourceShape.flags.get();
            bone.shape.materialId = boneData.game_mtl_idx;
            if (sourceShape.type == SBoneShape::stBox)
            {
                bone.shape.local = detail::boxTransform(sourceShape.box);
                bone.shape.halfExtents = detail::fromFvector(sourceShape.box.m_halfsize);
            }
            else if (sourceShape.type == SBoneShape::stSphere)
            {
                bone.shape.local = physics_portable::Transform{physics_portable::Quat{}, detail::fromFvector(sourceShape.sphere.P)};
                bone.shape.radius = sourceShape.sphere.R;
            }
            else if (sourceShape.type == SBoneShape::stCylinder)
            {
                bone.shape.local = detail::cylinderTransform(sourceShape.cylinder);
                bone.shape.radius = sourceShape.cylinder.m_radius;
                bone.shape.height = sourceShape.cylinder.m_height;
            }
        }

        CPhysicsElement* element = shell.get_Element(index);
        if (element)
        {
            CPHElement* physicsElement = cast_PHElement(element);
            dMass* mass = physicsElement->getMassTensor();
            bone.mass = mass->mass;
            // dMass::c is the center of mass in the body frame. This is the
            // authoritative value and avoids mixing the ODE body position
            // (world space) with CPHElement::mXFORM (parent space).
            bone.centerOfMass = physics_portable::Vec3{
                static_cast<float>(mass->c[0]),
                static_cast<float>(mass->c[1]),
                static_cast<float>(mass->c[2])};
            // ODE dMatrix3 is a row-major 3x4 matrix with a padded fourth
            // column: element (r, c) is I[r * 4 + c]. The portable contract
            // stores the upper triangle as xx, xy, xz, yy, yz, zz.
            bone.inertia[0] = static_cast<float>(mass->I[0]);
            bone.inertia[1] = static_cast<float>(mass->I[1]);
            bone.inertia[2] = static_cast<float>(mass->I[2]);
            bone.inertia[3] = static_cast<float>(mass->I[5]);
            bone.inertia[4] = static_cast<float>(mass->I[6]);
            bone.inertia[5] = static_cast<float>(mass->I[10]);
        }

        CPhysicsJoint* joint = shell.get_Joint(index);
        if (joint)
        {
            bone.limitCount = static_cast<std::uint8_t>(std::min<u16>(joint->GetAxesNumber(), 3));
            for (u8 axis = 0; axis < bone.limitCount; ++axis)
            {
                Fvector direction;
                CPhysicsJoint::eVs space;
                joint->GetAxisDir(axis, direction, space);
                float low = 0.0f;
                float high = 0.0f;
                float spring = 1.0f;
                float damping = 1.0f;
                joint->GetLimits(low, high, axis);
                joint->GetAxisSDfactors(spring, damping, axis);
                bone.limits[axis].space = detail::axisSpace(space);
                bone.limits[axis].axis = detail::fromFvector(direction);
                bone.limits[axis].lower = low;
                bone.limits[axis].upper = high;
                bone.limits[axis].sourceLower = low;
                bone.limits[axis].sourceUpper = high;
                bone.limits[axis].spring = spring;
                bone.limits[axis].damping = damping;
            }
        }
    }

    physics_portable::RagdollBuildReport report;
    if (!buildRagdollCapture(capture, document, report, error))
        return false;

    return true;
}

struct OgsrParityReport
{
    std::size_t bodies = 0;
    std::size_t joints = 0;
    std::size_t missingElements = 0;
    std::size_t missingJoints = 0;
    double maxMassError = 0.0;
    double maxComError = 0.0;
    double maxInertiaError = 0.0;
    double maxAxisCountError = 0.0;
    double maxAxisDirectionError = 0.0;
    double maxLimitError = 0.0;
    double maxSpringError = 0.0;
    std::size_t axisSpaceMismatches = 0;
    std::size_t legacyInertAxes = 0;
    double bonePoseError = 0.0;
    std::string details;
    std::string reason;
    bool ok = true;
};

inline double transformMaxError(const physics_portable::Transform& lhs,
                                const physics_portable::Transform& rhs)
{
    const physics_portable::RowMatrix4 a = physics_portable::rowMatrixFromTransform(lhs);
    const physics_portable::RowMatrix4 b = physics_portable::rowMatrixFromTransform(rhs);
    double result = 0.0;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            result = std::max(result, std::abs(static_cast<double>(a.m[row][column]) - b.m[row][column]));
    return result;
}

// Compare the portable document with the live ODE shell. This is deliberately
// strict for mass properties and joint parameters; frame diagnostics are
// reported separately because CPHElement::mXFORM is parent-space in OGSR.
inline OgsrParityReport verifyOgsrParity(IKinematics& kinematics,
                                         CPhysicsShell& shell,
                                         const physics_portable::RagdollCaptureDocument& document)
{
    OgsrParityReport report;
    report.bodies = document.description.bodies.size();
    report.joints = document.description.joints.size();

    const physics_portable::Transform& entity = document.initial.entityWorld;
    for (const physics_portable::RagdollBodyDesc& body : document.description.bodies)
    {
        CPhysicsElement* element = shell.get_Element(body.boneId);
        if (!element)
        {
            // A shape-less root is represented as a fixed static body and may
            // not own a CPHElement of its own in OGSR.
            if (body.role == physics_portable::BodyRole::StaticRoot)
                continue;
            ++report.missingElements;
            report.details += "missing_element bone=" + std::to_string(body.boneId) + "; ";
            continue;
        }
        CPHElement* physicsElement = cast_PHElement(element);
        dMass* mass = physicsElement->getMassTensor();
        if (body.role != physics_portable::BodyRole::StaticRoot)
        {
            report.maxMassError =
                std::max(report.maxMassError, std::abs(static_cast<double>(body.mass) - mass->mass));
            report.maxComError = std::max(
                report.maxComError,
                std::max(std::abs(static_cast<double>(body.centerOfMass.x) - mass->c[0]),
                         std::max(std::abs(static_cast<double>(body.centerOfMass.y) - mass->c[1]),
                                  std::abs(static_cast<double>(body.centerOfMass.z) - mass->c[2]))));
            const double inertia[6] = {mass->I[0], mass->I[1], mass->I[2],
                                       mass->I[5], mass->I[6], mass->I[10]};
            for (int component = 0; component < 6; ++component)
                report.maxInertiaError = std::max(
                    report.maxInertiaError,
                    std::abs(static_cast<double>(body.inertia[component]) - inertia[component]));
        }

        const physics_portable::Transform boneWorld =
            entity * detail::fromFmatrix(kinematics.LL_GetTransform(body.boneId));
        report.bonePoseError =
            std::max(report.bonePoseError, transformMaxError(document.initial.bodyWorld[body.id], boneWorld));

        const physics_portable::RagdollJointDesc* portableJoint = nullptr;
        for (const physics_portable::RagdollJointDesc& candidate : document.description.joints)
        {
            if (candidate.boneId == body.boneId)
            {
                portableJoint = &candidate;
                break;
            }
        }
        if (!portableJoint)
            continue;

        CPhysicsJoint* joint = shell.get_Joint(body.boneId);
        if (!joint)
        {
            ++report.missingJoints;
            report.details += "missing_joint bone=" + std::to_string(body.boneId) + "; ";
            continue;
        }

        const int rawOdeAxes = joint->GetAxesNumber();
        const int odeAxes = std::min<int>(rawOdeAxes, 3);
        const double axisCountError =
            std::abs(static_cast<double>(portableJoint->axisCount) - static_cast<double>(odeAxes));
        if (axisCountError > report.maxAxisCountError)
        {
            report.maxAxisCountError = axisCountError;
            report.details += "joint bone=" + std::to_string(body.boneId) +
                              " axisCount doc=" + std::to_string(portableJoint->axisCount) +
                              " ode=" + std::to_string(odeAxes) + "; ";
        }
        if (rawOdeAxes > 3)
        {
            report.legacyInertAxes += static_cast<std::size_t>(rawOdeAxes - 3);
            report.details += "joint bone=" + std::to_string(body.boneId) + " raw_ode_axes=" +
                              std::to_string(rawOdeAxes) + " exported_axes=3; ";
            if (report.reason.empty())
                report.reason =
                    "legacy CPHJoint full_control constructor falls through to slider and appends "
                    "two inert SPHAxis entries (indices 3-4); the Euler motor uses only 0-2";
            if (rawOdeAxes != 5)
            {
                report.ok = false;
                report.details += "unexpected raw axis count " + std::to_string(rawOdeAxes) + "; ";
            }
        }
        const int axes = odeAxes;
        for (int axis = 0; axis < axes; ++axis)
        {
            Fvector direction;
            CPhysicsJoint::eVs space;
            joint->GetAxisDir(axis, direction, space);
            const physics_portable::Vec3 axisValue = detail::fromFvector(direction);
            const float axisLength = physics_portable::length(axisValue);
            if (axisLength > 0.0f)
            {
                const physics_portable::Vec3 normalized = axisValue * (1.0f / axisLength);
                const physics_portable::Vec3 captured = portableJoint->axes[axis].axis;
                const float capturedLength = physics_portable::length(captured);
                if (capturedLength > 0.0f)
                {
                    const physics_portable::Vec3 capturedNormalized = captured * (1.0f / capturedLength);
                    const double directionError =
                        std::abs(normalized.x - capturedNormalized.x) +
                        std::abs(normalized.y - capturedNormalized.y) +
                        std::abs(normalized.z - capturedNormalized.z);
                    if (directionError > report.maxAxisDirectionError)
                    {
                        report.maxAxisDirectionError = directionError;
                        report.details += "joint bone=" + std::to_string(body.boneId) +
                                          " axis=" + std::to_string(axis) +
                                          " doc_axis=" + std::to_string(capturedNormalized.x) + "," +
                                          std::to_string(capturedNormalized.y) + "," +
                                          std::to_string(capturedNormalized.z) +
                                          " ode_axis=" + std::to_string(normalized.x) + "," +
                                          std::to_string(normalized.y) + "," +
                                          std::to_string(normalized.z) + "; ";
                    }
                }
            }
            if (detail::axisSpace(space) != portableJoint->axes[axis].space)
            {
                ++report.axisSpaceMismatches;
                report.details += "joint bone=" + std::to_string(body.boneId) +
                                  " axis=" + std::to_string(axis) + " axis-space mismatch; ";
            }

            float low = 0.0f;
            float high = 0.0f;
            joint->GetLimits(low, high, axis);
            report.maxLimitError = std::max(
                report.maxLimitError,
                std::max(std::abs(static_cast<double>(portableJoint->axes[axis].sourceLower) - low),
                         std::abs(static_cast<double>(portableJoint->axes[axis].sourceUpper) - high)));
            float spring = 1.0f;
            float damping = 1.0f;
            joint->GetAxisSDfactors(spring, damping, axis);
            report.maxSpringError = std::max(
                report.maxSpringError,
                std::max(std::abs(static_cast<double>(portableJoint->axes[axis].spring) - spring),
                         std::abs(static_cast<double>(portableJoint->axes[axis].damping) - damping)));
        }
    }

    report.ok = report.ok && report.missingElements == 0 && report.missingJoints == 0 &&
                report.maxMassError <= 1.0e-4 && report.maxComError <= 1.0e-4 &&
                report.maxInertiaError <= 1.0e-4 && report.maxAxisCountError <= 0.0 &&
                report.maxAxisDirectionError <= 1.0e-4 && report.axisSpaceMismatches == 0 &&
                report.maxLimitError <= 1.0e-4 && report.maxSpringError <= 1.0e-4;
    return report;
}

inline bool dumpOgsrRagdoll(IKinematics& kinematics,
                            CPhysicsShell& shell,
                            const Fmatrix& entityWorld,
                            const Fvector& linearVelocity,
                            const char* outputPath,
                            std::string* error)
{
    physics_portable::RagdollCaptureDocument document;
    if (!captureFromOgsr(kinematics, shell, entityWorld, linearVelocity, document, error))
        return false;

    std::string json;
    if (!physics_portable::writeRagdollCapture(document, json, error))
        return false;

    std::ofstream output(outputPath, std::ios::binary);
    if (!output)
    {
        if (error)
            *error = std::string("cannot open capture output: ") + outputPath;
        return false;
    }
    output.write(json.data(), static_cast<std::streamsize>(json.size()));
    return output.good();
}

} // namespace physics_adapter_ogsr

#endif // OGSR_DIAGNOSTIC_CAPTURE
