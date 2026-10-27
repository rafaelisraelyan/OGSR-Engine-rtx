#pragma once

// Portable ragdoll data contract.
//
// This header intentionally contains no OGSR, ODE, DirectX or PhysX types.
// Units are meters, kilograms, seconds and radians.
// Transforms use column-vector convention: p' = rotation * p + translation.
// Quaternions are stored as (x, y, z, w).

#include <cstdint>
#include <string>
#include <vector>

namespace physics_portable
{

using BoneId = std::uint16_t;
using BodyId = std::uint16_t;
using JointId = std::uint16_t;
using ShapeId = std::uint16_t;

constexpr BoneId kInvalidBone = 0xFFFF;
constexpr BodyId kInvalidBody = 0xFFFF;
constexpr JointId kInvalidJoint = 0xFFFF;

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Quat
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Transform
{
    Quat rotation{};
    Vec3 translation{};
};

inline Vec3 makeVec3(float x, float y, float z) { return Vec3{x, y, z}; }
inline Quat makeQuat(float x, float y, float z, float w) { return Quat{x, y, z, w}; }
inline Transform makeTransform(const Quat& rotation, const Vec3& translation) { return Transform{rotation, translation}; }

Quat fromAxisAngle(const Vec3& axis, float radians);
Quat normalized(const Quat& value);
Quat conjugate(const Quat& value);
Quat operator*(const Quat& a, const Quat& b);
Vec3 normalized(const Vec3& value);
Vec3 rotate(const Quat& rotation, const Vec3& vector);

Vec3 operator+(const Vec3& a, const Vec3& b);
Vec3 operator-(const Vec3& a, const Vec3& b);
Vec3 operator*(const Vec3& value, float scale);
Vec3 operator*(float scale, const Vec3& value);
float dot(const Vec3& a, const Vec3& b);
Vec3 cross(const Vec3& a, const Vec3& b);
float lengthSquared(const Vec3& value);
float length(const Vec3& value);

// Column-vector transform composition: a * b applies b first, then a.
Transform operator*(const Transform& a, const Transform& b);
Transform inverse(const Transform& value);
Vec3 transformPoint(const Transform& transform, const Vec3& point);
Vec3 transformVector(const Transform& transform, const Vec3& vector);

bool isFinite(float value);
bool isFinite(const Vec3& value);
bool isFinite(const Quat& value);
bool isFinite(const Transform& value);
bool approxEqual(const Vec3& a, const Vec3& b, float epsilon = 1.0e-5f);
bool approxEqual(const Quat& a, const Quat& b, float epsilon = 1.0e-5f);
bool approxEqual(const Transform& a, const Transform& b, float epsilon = 1.0e-5f);

enum class BoneDisposition : std::uint8_t
{
    Body,             // owns a rigid body
    MergedIntoParent, // jtRigid or visible bone without a physics shape
    SkippedInvisible, // invisible bone; descendants attach to the nearest visible ancestor
    FreeBody          // jtNone with a shape, no joint
};

enum class BodyRole : std::uint8_t
{
    Dynamic,
    StaticRoot,
    FreeBody
};

enum class JointKind : std::uint8_t
{
    Ball,
    Hinge,
    Hinge2,
    Slider,
    Euler3,
    Fixed
};

enum class AxisSpace : std::uint8_t
{
    Parent,
    Child,
    World
};

enum class SwingLimitMode : std::uint8_t
{
    Auto,   // pyramid when both swing axes are limited, otherwise cone
    Cone,   // symmetric cone; asymmetric source limits are approximated
    Pyramid // asymmetric pyramid; requires both swing axes limited or one limited + one locked
};

enum class ShapeType : std::uint8_t
{
    Box,
    Sphere,
    Cylinder
};

enum class ShapeFlag : std::uint32_t
{
    None = 0,
    NoPickable = 1u << 0,
    RemoveAfterBreak = 1u << 1,
    NoPhysics = 1u << 2,
    NoFogCollider = 1u << 3
};

inline std::uint32_t shapeFlag(ShapeFlag flag) { return static_cast<std::uint32_t>(flag); }

struct RagdollBoneDesc
{
    BoneId id = kInvalidBone;
    BoneId parent = kInvalidBone;
    std::string name;
    Transform bindLocal{}; // relative to the parent bone
    bool visible = true;
    BoneDisposition disposition = BoneDisposition::Body;
    BodyId body = kInvalidBody; // valid for Body and FreeBody
    std::uint32_t sourceShapeFlags = 0;
};

struct RagdollShapeDesc
{
    ShapeType type = ShapeType::Box;
    Transform local{}; // body frame -> shape frame
    Vec3 halfExtents = makeVec3(0.05f, 0.05f, 0.05f); // Box
    float radius = 0.05f;                             // Sphere / Cylinder
    float height = 0.1f;                              // Cylinder, full height
    float margin = 0.0f;                              // geometric core margin; 0 for OGSR parity
    std::uint16_t materialId = 0;
    BoneId sourceBoneId = kInvalidBone;
    std::uint32_t flags = 0;
};

struct RagdollBodyDesc
{
    BodyId id = kInvalidBody;
    BoneId boneId = kInvalidBone;
    BodyId parentBody = kInvalidBody;
    BodyRole role = BodyRole::Dynamic;
    bool isFixed = false;
    float mass = 0.0f;
    Vec3 centerOfMass{};
    // Symmetric 3x3 inertia about the center of mass, expressed in the body frame:
    // xx, xy, xz, yy, yz, zz.
    float inertia[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    Transform bindPose{}; // relative to the parent body; root is relative to the entity
    std::vector<RagdollShapeDesc> shapes;
    std::vector<BoneId> mergedBones;
    std::uint32_t collisionGroup = 0;
};

struct RagdollAxisLimit
{
    AxisSpace space = AxisSpace::Child;
    Vec3 axis = makeVec3(0.0f, 0.0f, 1.0f);
    float lower = 0.0f; // effective value after OGSR CalcAxis clamping
    float upper = 0.0f;
    float sourceLower = 0.0f; // raw value before clamping, diagnostics only
    float sourceUpper = 0.0f;
    float spring = 1.0f;
    float damping = 1.0f;
    float zero = 0.0f;
};

struct RagdollJointDesc
{
    JointId id = kInvalidJoint;
    BoneId boneId = kInvalidBone;
    BodyId parentBody = kInvalidBody;
    BodyId childBody = kInvalidBody;
    JointKind kind = JointKind::Ball;
    Transform parentFrame{}; // relative to the parent body
    Transform childFrame{};  // relative to the child body
    RagdollAxisLimit axes[3]{};
    std::uint8_t axisCount = 0;
    float erp = 0.0f;
    float cfm = 0.0f;
    float friction = 0.0f;
    float maxForce = 0.0f;
    float targetVelocity = 0.0f;
    bool breakable = false;
    float breakForce = 0.0f;
    float breakTorque = 0.0f;
    SwingLimitMode swingLimitMode = SwingLimitMode::Auto;
    std::uint32_t sourceFlags = 0;
};

struct RagdollMaterialDesc
{
    std::uint16_t id = 0;
    std::string name;
    float friction = 0.8f;
    float restitution = 0.0f;
    float spring = 1.0f;
    float damping = 1.0f;
};

struct RagdollDescription
{
    std::uint32_t schemaVersion = 1;
    std::string skeletonName;
    std::uint32_t sourceHash = 0;
    BodyId rootBody = 0;
    std::vector<RagdollBoneDesc> bones;
    std::vector<RagdollBodyDesc> bodies;
    std::vector<RagdollJointDesc> joints;
    std::vector<RagdollMaterialDesc> materials;
};

struct RagdollInitialState
{
    Transform entityWorld{};
    Transform rootToEntity{}; // inverse(rootBodyWorld) * entityWorld
    std::vector<Transform> bodyWorld;
    std::vector<Vec3> linearVelocity;
    std::vector<Vec3> angularVelocity;
    std::vector<std::uint8_t> enabled;
};

struct RagdollPose
{
    Transform entityWorld{};
    std::vector<Transform> bodyWorld;
    std::uint64_t stepIndex = 0;
};

struct RagdollSimulationConfig
{
    Vec3 gravity = makeVec3(0.0f, -19.62f, 0.0f); // OGSR effective gravity
    float fixedTimestep = 0.01f;                  // OGSR fixed_step
    int maxSubsteps = 4;
    float contactOffset = 0.02f; // PhysX default at unit scale; not a geometric margin
    float restOffset = 0.0f;
    bool enableStabilization = true;
    std::uint32_t solverPositionIterations = 8;
    std::uint32_t solverVelocityIterations = 2;

    // A synthetic OGSR root with shapes but zero mass is a static reference in
    // the capture contract. For a free PhysX ragdoll it can be promoted to a
    // dynamic body whose mass and inertia are derived from its actual collision
    // shapes at this density (kg/m^3), instead of an arbitrary constant.
    bool promoteStaticRoot = false;
    float staticRootDensity = 300.0f;
    float linearDamping = 0.05f;
    float angularDamping = 0.2f;
    bool disableSelfCollision = false;
    bool collectDiagnostics = false;
};

struct RagdollBuildReport
{
    std::vector<BoneId> bodyBones;
    std::vector<BoneId> mergedBones;
    std::vector<BoneId> skippedInvisibleBones;
    std::vector<std::string> warnings;
};

bool validateDescription(const RagdollDescription& description, std::string* error);
bool validateInitialState(const RagdollDescription& description, const RagdollInitialState& initial, std::string* error);

// Eigen-decomposition of the symmetric 3x3 inertia tensor about the center of mass.
// eigenvalues are the principal moments; axes maps the body frame to the principal-axis frame.
bool diagonalizeInertia(const float inertia[6], Vec3& eigenvalues, Quat& axes, std::string* error);

// pose.bodyWorld stores the body bone-frame transform, not the center of mass.
Transform bodyToBoneModel(const RagdollPose& pose, BodyId body);
Transform entityFromRoot(const RagdollPose& pose, BodyId rootBody, const Transform& rootToEntity);

// Bone-space hit mapping shared by the game adapter and its unit tests.
// Body: the bone owns a rigid body. Merged: the bone is part of a parent
// body, so the capture stores its body-local transform. Frozen: the bone is
// not simulated and follows the animated model transform.
enum class BoneWorldKind : std::uint8_t
{
    Body,
    Merged,
    Frozen
};

Transform resolveBoneWorld(BoneWorldKind kind,
                           const Transform& bodyWorld,
                           const Transform& mergedLocal,
                           const Transform& entityWorld,
                           const Transform& animationBoneModel);

// The bone-space position is always explicit and is interpreted in the hit
// bone frame. A zero vector is a valid point; a central impulse uses the
// separate PhysX COM path, not a zero sentinel.
Vec3 resolveWorldHitPoint(const Transform& boneWorld, const Vec3& boneLocalPosition);

} // namespace physics_portable
