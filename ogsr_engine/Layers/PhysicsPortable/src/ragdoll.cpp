#include "physics_portable/ragdoll.h"

#include "physics_portable/row_matrix.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace physics_portable
{
namespace
{
constexpr float kEpsilon = 1.0e-6f;
constexpr float kPi = 3.14159265358979323846f;

bool fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}

bool isUnitQuat(const Quat& value);

Vec3 vadd(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 vsub(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 vscale(const Vec3& a, float s) { return Vec3{a.x * s, a.y * s, a.z * s}; }
float vdot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 vcross(const Vec3& a, const Vec3& b)
{
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float vlengthSquared(const Vec3& v) { return vdot(v, v); }
float vlength(const Vec3& v) { return std::sqrt(vlengthSquared(v)); }

Quat quatFromMatrix(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22)
{
    const float trace = m00 + m11 + m22;
    Quat q{};
    if (trace > 0.0f)
    {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    }
    else if (m00 > m11 && m00 > m22)
    {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    }
    else if (m11 > m22)
    {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    }
    else
    {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }

    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > kEpsilon))
        return Quat{};
    return Quat{q.x / n, q.y / n, q.z / n, q.w / n};
}

Quat multiplyQuat(const Quat& a, const Quat& b)
{
    return Quat{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat conjugateQuat(const Quat& q) { return Quat{-q.x, -q.y, -q.z, q.w}; }

Vec3 rotateVector(const Quat& q, const Vec3& v)
{
    const Vec3 qv{q.x, q.y, q.z};
    const Vec3 t = vscale(vcross(qv, v), 2.0f);
    return vadd(v, vadd(vscale(t, q.w), vcross(qv, t)));
}

float clampFinite(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

bool validateBodyIds(const RagdollDescription& description, std::string* error)
{
    for (std::size_t i = 0; i < description.bodies.size(); ++i)
    {
        const RagdollBodyDesc& body = description.bodies[i];
        if (body.id != static_cast<BodyId>(i))
            return fail(error, "body id must equal its storage index");
        if (body.boneId == kInvalidBone)
            return fail(error, "body has no source bone");
        if (body.boneId >= description.bones.size())
            return fail(error, "body bone reference is out of range");
        if (description.bones[body.boneId].body != body.id)
            return fail(error, "body bone does not reference this body");
        if (body.parentBody != kInvalidBody && body.parentBody >= description.bodies.size())
            return fail(error, "body parent is out of range");
        if ((body.role == BodyRole::Dynamic || body.role == BodyRole::FreeBody) && !(body.mass > 0.0f))
            return fail(error, "dynamic body must have positive mass");
        if (!isFinite(body.mass) || !isFinite(body.centerOfMass))
            return fail(error, "body mass properties are not finite");
        for (float value : body.inertia)
        {
            if (!isFinite(value))
                return fail(error, "body inertia is not finite");
        }
        if (!isFinite(body.bindPose) || !isUnitQuat(body.bindPose.rotation))
            return fail(error, "body bind pose is not finite or unit");

        for (const RagdollShapeDesc& shape : body.shapes)
        {
            if (!isFinite(shape.local) || !isUnitQuat(shape.local.rotation))
                return fail(error, "shape local pose is not finite or unit");
            if (!isFinite(shape.margin) || shape.margin < 0.0f)
                return fail(error, "shape margin must be finite and non-negative");
            if (shape.type != ShapeType::Cylinder && shape.margin != 0.0f)
                return fail(error, "only cylinder shapes support a geometric margin");
            if (shape.type == ShapeType::Box)
            {
                if (!isFinite(shape.halfExtents) || shape.halfExtents.x <= 0.0f || shape.halfExtents.y <= 0.0f ||
                    shape.halfExtents.z <= 0.0f)
                    return fail(error, "box half extents must be positive");
            }
            else if (shape.type == ShapeType::Sphere)
            {
                if (!isFinite(shape.radius) || shape.radius <= 0.0f)
                    return fail(error, "sphere radius must be positive");
            }
            else if (shape.type == ShapeType::Cylinder)
            {
                if (!isFinite(shape.radius) || !isFinite(shape.height) || shape.radius <= 0.0f || shape.height <= 0.0f)
                    return fail(error, "cylinder radius and height must be positive");
            }
        }
    }
    return true;
}

bool validateBones(const RagdollDescription& description, std::string* error)
{
    std::vector<bool> seen(description.bones.size(), false);
    for (std::size_t index = 0; index < description.bones.size(); ++index)
    {
        const RagdollBoneDesc& bone = description.bones[index];
        if (bone.id != static_cast<BoneId>(index))
            return fail(error, "bone id must equal its storage index");
        if (seen[bone.id])
            return fail(error, "duplicate bone id");
        seen[bone.id] = true;
        if (bone.parent != kInvalidBone && bone.parent >= description.bones.size())
            return fail(error, "bone parent is out of range");
        if (!isFinite(bone.bindLocal) || !isUnitQuat(bone.bindLocal.rotation))
            return fail(error, "bone bind transform is not finite or unit");
        if ((bone.disposition == BoneDisposition::Body || bone.disposition == BoneDisposition::FreeBody) &&
            bone.body == kInvalidBody)
            return fail(error, "body bone has no body reference");
        if (bone.disposition == BoneDisposition::MergedIntoParent && bone.body == kInvalidBody)
            return fail(error, "merged bone has no parent body reference");
        if (bone.body != kInvalidBody && bone.body >= description.bodies.size())
            return fail(error, "bone body reference is out of range");
        if ((bone.disposition == BoneDisposition::Body || bone.disposition == BoneDisposition::FreeBody) &&
            description.bodies[bone.body].boneId != bone.id)
            return fail(error, "body bone/body back-reference mismatch");

        BoneId parent = bone.parent;
        std::size_t depth = 0;
        while (parent != kInvalidBone)
        {
            if (parent >= description.bones.size() || ++depth > description.bones.size())
                return fail(error, "bone hierarchy is cyclic or out of range");
            parent = description.bones[parent].parent;
        }
    }
    return true;
}

bool validateJoints(const RagdollDescription& description, std::string* error)
{
    for (std::size_t i = 0; i < description.joints.size(); ++i)
    {
        const RagdollJointDesc& joint = description.joints[i];
        if (joint.id != static_cast<JointId>(i))
            return fail(error, "joint id must equal its storage index");
        if (joint.parentBody >= description.bodies.size() || joint.childBody >= description.bodies.size())
            return fail(error, "joint body reference is out of range");
        if (joint.parentBody == joint.childBody)
            return fail(error, "joint connects a body to itself");
        if (!isFinite(joint.parentFrame) || !isFinite(joint.childFrame) ||
            !isUnitQuat(joint.parentFrame.rotation) || !isUnitQuat(joint.childFrame.rotation))
            return fail(error, "joint frame is not finite or unit");
        if (joint.axisCount > 3)
            return fail(error, "joint has more than three axes");
        for (std::uint8_t axis = 0; axis < joint.axisCount; ++axis)
        {
            const RagdollAxisLimit& limit = joint.axes[axis];
            if (!isFinite(limit.axis) || !isFinite(limit.lower) || !isFinite(limit.upper) ||
                !isFinite(limit.sourceLower) || !isFinite(limit.sourceUpper) || !isFinite(limit.spring) ||
                !isFinite(limit.damping) || !isFinite(limit.zero))
                return fail(error, "joint limit is not finite");
            if (limit.lower > limit.upper)
                return fail(error, "joint limit lower is greater than upper");
            if (vlengthSquared(limit.axis) < kEpsilon)
                return fail(error, "joint limit axis is zero");
        }
        if (!isFinite(joint.breakForce) || !isFinite(joint.breakTorque) || joint.breakForce < 0.0f ||
            joint.breakTorque < 0.0f || !isFinite(joint.erp) || !isFinite(joint.cfm) ||
            !isFinite(joint.friction) || !isFinite(joint.maxForce) || !isFinite(joint.targetVelocity) ||
            joint.friction < 0.0f || joint.maxForce < 0.0f)
            return fail(error, "joint solver/break parameters are invalid");
    }
    return true;
}

bool diagonalizeInertiaImpl(const float inertia[6], Vec3& eigenvalues, Quat& axes, std::string* error)
{
    double a[3][3] = {
        {inertia[0], inertia[1], inertia[2]},
        {inertia[1], inertia[3], inertia[4]},
        {inertia[2], inertia[4], inertia[5]},
    };
    double v[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};

    for (int iteration = 0; iteration < 32; ++iteration)
    {
        int p = 0;
        int q = 1;
        double largest = std::abs(a[0][1]);
        if (std::abs(a[0][2]) > largest)
        {
            p = 0;
            q = 2;
            largest = std::abs(a[0][2]);
        }
        if (std::abs(a[1][2]) > largest)
        {
            p = 1;
            q = 2;
            largest = std::abs(a[1][2]);
        }
        if (largest < 1.0e-12)
            break;

        const double theta = 0.5 * std::atan2(2.0 * a[p][q], a[q][q] - a[p][p]);
        const double c = std::cos(theta);
        const double s = std::sin(theta);

        for (int k = 0; k < 3; ++k)
        {
            const double akp = a[k][p];
            const double akq = a[k][q];
            a[k][p] = c * akp - s * akq;
            a[k][q] = s * akp + c * akq;
        }
        for (int k = 0; k < 3; ++k)
        {
            const double apk = a[p][k];
            const double aqk = a[q][k];
            a[p][k] = c * apk - s * aqk;
            a[q][k] = s * apk + c * aqk;
        }
        for (int k = 0; k < 3; ++k)
        {
            const double vkp = v[k][p];
            const double vkq = v[k][q];
            v[k][p] = c * vkp - s * vkq;
            v[k][q] = s * vkp + c * vkq;
        }
    }

    int order[3] = {0, 1, 2};
    std::sort(order, order + 3, [&](int lhs, int rhs) { return a[lhs][lhs] < a[rhs][rhs]; });

    eigenvalues = Vec3{
        static_cast<float>(a[order[0]][order[0]]),
        static_cast<float>(a[order[1]][order[1]]),
        static_cast<float>(a[order[2]][order[2]])};

    if (!isFinite(eigenvalues) || eigenvalues.x <= 0.0f || eigenvalues.y <= 0.0f || eigenvalues.z <= 0.0f)
        return fail(error, "inertia tensor is not positive definite");

    const double det =
        v[0][order[0]] * (v[1][order[1]] * v[2][order[2]] - v[1][order[2]] * v[2][order[1]]) -
        v[0][order[1]] * (v[1][order[0]] * v[2][order[2]] - v[1][order[2]] * v[2][order[0]]) +
        v[0][order[2]] * (v[1][order[0]] * v[2][order[1]] - v[1][order[1]] * v[2][order[0]]);

    const int third = det < 0.0 ? -1 : 1;
    axes = quatFromMatrix(
        static_cast<float>(v[0][order[0]]), static_cast<float>(v[0][order[1]]),
        static_cast<float>(third * v[0][order[2]]),
        static_cast<float>(v[1][order[0]]), static_cast<float>(v[1][order[1]]),
        static_cast<float>(third * v[1][order[2]]),
        static_cast<float>(v[2][order[0]]), static_cast<float>(v[2][order[1]]),
        static_cast<float>(third * v[2][order[2]]));
    return isFinite(axes);
}

} // namespace

Quat fromAxisAngle(const Vec3& axis, float radians)
{
    const Vec3 unit = normalized(axis);
    if (vlengthSquared(unit) < kEpsilon)
        return Quat{};
    const float half = radians * 0.5f;
    const float sine = std::sin(half);
    return Quat{unit.x * sine, unit.y * sine, unit.z * sine, std::cos(half)};
}

Vec3 operator+(const Vec3& a, const Vec3& b) { return vadd(a, b); }
Vec3 operator-(const Vec3& a, const Vec3& b) { return vsub(a, b); }
Vec3 operator*(const Vec3& value, float scale) { return vscale(value, scale); }
Vec3 operator*(float scale, const Vec3& value) { return vscale(value, scale); }

float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float lengthSquared(const Vec3& value) { return dot(value, value); }
float length(const Vec3& value) { return std::sqrt(lengthSquared(value)); }

Quat operator*(const Quat& a, const Quat& b) { return multiplyQuat(a, b); }
Quat conjugate(const Quat& value) { return conjugateQuat(value); }

Vec3 normalized(const Vec3& value)
{
    const float n = vlength(value);
    if (!(n > kEpsilon))
        return Vec3{0.0f, 0.0f, 0.0f};
    return vscale(value, 1.0f / n);
}

Quat normalized(const Quat& value)
{
    const float n = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
    if (!(n > kEpsilon))
        return Quat{};
    return Quat{value.x / n, value.y / n, value.z / n, value.w / n};
}

Vec3 rotate(const Quat& rotation, const Vec3& vector) { return rotateVector(normalized(rotation), vector); }

Transform operator*(const Transform& a, const Transform& b)
{
    return Transform{a.rotation * b.rotation, transformPoint(a, b.translation)};
}

Transform inverse(const Transform& value)
{
    const Quat inverseRotation = conjugate(normalized(value.rotation));
    const Vec3 inverseTranslation = rotate(inverseRotation, Vec3{-value.translation.x, -value.translation.y, -value.translation.z});
    return Transform{inverseRotation, inverseTranslation};
}

Vec3 transformPoint(const Transform& transform, const Vec3& point)
{
    return vadd(rotate(transform.rotation, point), transform.translation);
}

Vec3 transformVector(const Transform& transform, const Vec3& vector) { return rotate(transform.rotation, vector); }

bool isFinite(float value) { return std::isfinite(value); }
bool isFinite(const Vec3& value) { return isFinite(value.x) && isFinite(value.y) && isFinite(value.z); }
bool isFinite(const Quat& value) { return isFinite(value.x) && isFinite(value.y) && isFinite(value.z) && isFinite(value.w); }
bool isFinite(const Transform& value) { return isFinite(value.rotation) && isFinite(value.translation); }

namespace
{
bool isUnitQuat(const Quat& value)
{
    const float normSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    return std::abs(normSquared - 1.0f) <= 1.0e-3f;
}
} // namespace

bool approxEqual(const Vec3& a, const Vec3& b, float epsilon)
{
    return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon && std::abs(a.z - b.z) <= epsilon;
}

bool approxEqual(const Quat& a, const Quat& b, float epsilon)
{
    const Quat qa = normalized(a);
    const Quat qb = normalized(b);
    const float direct = std::abs(qa.x - qb.x) + std::abs(qa.y - qb.y) + std::abs(qa.z - qb.z) + std::abs(qa.w - qb.w);
    const float negated = std::abs(qa.x + qb.x) + std::abs(qa.y + qb.y) + std::abs(qa.z + qb.z) + std::abs(qa.w + qb.w);
    return std::min(direct, negated) <= epsilon;
}

bool approxEqual(const Transform& a, const Transform& b, float epsilon)
{
    return approxEqual(a.rotation, b.rotation, epsilon) && approxEqual(a.translation, b.translation, epsilon);
}

RowMatrix4 rowMatrixIdentity()
{
    RowMatrix4 result{};
    result.m[0][0] = 1.0f;
    result.m[1][1] = 1.0f;
    result.m[2][2] = 1.0f;
    result.m[3][3] = 1.0f;
    return result;
}

RowMatrix4 rowMatrixFromTransform(const Transform& transform)
{
    const Quat q = normalized(transform.rotation);
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;

    // Column-vector rotation R.
    const float r00 = 1.0f - 2.0f * (yy + zz);
    const float r01 = 2.0f * (xy - wz);
    const float r02 = 2.0f * (xz + wy);
    const float r10 = 2.0f * (xy + wz);
    const float r11 = 1.0f - 2.0f * (xx + zz);
    const float r12 = 2.0f * (yz - wx);
    const float r20 = 2.0f * (xz - wy);
    const float r21 = 2.0f * (yz + wx);
    const float r22 = 1.0f - 2.0f * (xx + yy);

    RowMatrix4 result{};
    // Row-vector storage is the transpose of the column-vector rotation.
    result.m[0][0] = r00;
    result.m[0][1] = r10;
    result.m[0][2] = r20;
    result.m[0][3] = 0.0f;
    result.m[1][0] = r01;
    result.m[1][1] = r11;
    result.m[1][2] = r21;
    result.m[1][3] = 0.0f;
    result.m[2][0] = r02;
    result.m[2][1] = r12;
    result.m[2][2] = r22;
    result.m[2][3] = 0.0f;
    result.m[3][0] = transform.translation.x;
    result.m[3][1] = transform.translation.y;
    result.m[3][2] = transform.translation.z;
    result.m[3][3] = 1.0f;
    return result;
}

Transform transformFromRowMatrix(const RowMatrix4& matrix)
{
    const Quat rotation = quatFromMatrix(
        matrix.m[0][0], matrix.m[1][0], matrix.m[2][0],
        matrix.m[0][1], matrix.m[1][1], matrix.m[2][1],
        matrix.m[0][2], matrix.m[1][2], matrix.m[2][2]);
    return Transform{rotation, Vec3{matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]}};
}

RowMatrix4 rowMatrixMul43(const RowMatrix4& a, const RowMatrix4& b)
{
    // Copied operation-for-operation from xrCore/_matrix.h::mul_43.
    // Fmatrix::mul_43(A, B) applies B first and then A.
    RowMatrix4 r{};
    r.m[0][0] = a.m[0][0] * b.m[0][0] + a.m[1][0] * b.m[0][1] + a.m[2][0] * b.m[0][2];
    r.m[0][1] = a.m[0][1] * b.m[0][0] + a.m[1][1] * b.m[0][1] + a.m[2][1] * b.m[0][2];
    r.m[0][2] = a.m[0][2] * b.m[0][0] + a.m[1][2] * b.m[0][1] + a.m[2][2] * b.m[0][2];
    r.m[0][3] = 0.0f;

    r.m[1][0] = a.m[0][0] * b.m[1][0] + a.m[1][0] * b.m[1][1] + a.m[2][0] * b.m[1][2];
    r.m[1][1] = a.m[0][1] * b.m[1][0] + a.m[1][1] * b.m[1][1] + a.m[2][1] * b.m[1][2];
    r.m[1][2] = a.m[0][2] * b.m[1][0] + a.m[1][2] * b.m[1][1] + a.m[2][2] * b.m[1][2];
    r.m[1][3] = 0.0f;

    r.m[2][0] = a.m[0][0] * b.m[2][0] + a.m[1][0] * b.m[2][1] + a.m[2][0] * b.m[2][2];
    r.m[2][1] = a.m[0][1] * b.m[2][0] + a.m[1][1] * b.m[2][1] + a.m[2][1] * b.m[2][2];
    r.m[2][2] = a.m[0][2] * b.m[2][0] + a.m[1][2] * b.m[2][1] + a.m[2][2] * b.m[2][2];
    r.m[2][3] = 0.0f;

    r.m[3][0] = a.m[0][0] * b.m[3][0] + a.m[1][0] * b.m[3][1] + a.m[2][0] * b.m[3][2] + a.m[3][0];
    r.m[3][1] = a.m[0][1] * b.m[3][0] + a.m[1][1] * b.m[3][1] + a.m[2][1] * b.m[3][2] + a.m[3][1];
    r.m[3][2] = a.m[0][2] * b.m[3][0] + a.m[1][2] * b.m[3][1] + a.m[2][2] * b.m[3][2] + a.m[3][2];
    r.m[3][3] = 1.0f;
    return r;
}

Vec3 rowMatrixTransformPoint(const RowMatrix4& matrix, const Vec3& point)
{
    return Vec3{
        point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0],
        point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1],
        point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2]};
}

bool validateDescription(const RagdollDescription& description, std::string* error)
{
    if (description.schemaVersion != 1)
        return fail(error, "unsupported ragdoll schema version");
    if (description.bodies.empty())
        return fail(error, "ragdoll has no bodies");
    if (description.rootBody >= description.bodies.size())
        return fail(error, "root body is out of range");
    if (!validateBones(description, error))
        return false;
    if (!validateBodyIds(description, error))
        return false;
    if (!validateJoints(description, error))
        return false;

    for (const RagdollMaterialDesc& material : description.materials)
    {
        if (!isFinite(material.friction) || !isFinite(material.restitution) || material.friction < 0.0f ||
            material.restitution < 0.0f || !isFinite(material.spring) || !isFinite(material.damping) ||
            material.spring < 0.0f || material.damping < 0.0f)
            return fail(error, "material coefficients are invalid");
    }

    RagdollPose probe;
    probe.bodyWorld.resize(description.bodies.size());
    for (BodyId body = 0; body < description.bodies.size(); ++body)
    {
        Transform world = description.bodies[body].bindPose;
        BodyId parent = description.bodies[body].parentBody;
        std::size_t depth = 0;
        while (parent != kInvalidBody)
        {
            if (parent >= description.bodies.size() || ++depth > description.bodies.size())
                return fail(error, "body bind hierarchy is cyclic or out of range");
            world = description.bodies[parent].bindPose * world;
            parent = description.bodies[parent].parentBody;
        }
        if (!isFinite(world))
            return fail(error, "body bind hierarchy produces a non-finite transform");
        probe.bodyWorld[body] = world;
    }

    for (const RagdollBodyDesc& body : description.bodies)
    {
        if (body.role == BodyRole::Dynamic || body.role == BodyRole::FreeBody)
        {
            Vec3 eigenvalues{};
            Quat axes{};
            if (!diagonalizeInertiaImpl(body.inertia, eigenvalues, axes, error))
                return false;
        }
    }
    return true;
}

bool validateInitialState(const RagdollDescription& description, const RagdollInitialState& initial, std::string* error)
{
    if (initial.bodyWorld.size() != description.bodies.size())
        return fail(error, "initial body pose count does not match body count");
    if (initial.linearVelocity.size() != description.bodies.size())
        return fail(error, "initial linear velocity count does not match body count");
    if (initial.angularVelocity.size() != description.bodies.size())
        return fail(error, "initial angular velocity count does not match body count");
    if (!initial.enabled.empty() && initial.enabled.size() != description.bodies.size())
        return fail(error, "initial enabled count does not match body count");
    if (!isFinite(initial.entityWorld) || !isFinite(initial.rootToEntity) ||
        !isUnitQuat(initial.entityWorld.rotation) || !isUnitQuat(initial.rootToEntity.rotation))
        return fail(error, "initial entity transform is not finite or unit");
    for (BodyId body = 0; body < description.bodies.size(); ++body)
    {
        if (!isFinite(initial.bodyWorld[body]) || !isFinite(initial.linearVelocity[body]) ||
            !isFinite(initial.angularVelocity[body]) || !isUnitQuat(initial.bodyWorld[body].rotation))
            return fail(error, "initial body state is not finite or unit");
    }
    if (description.rootBody < initial.bodyWorld.size())
    {
        const Transform expected = inverse(initial.bodyWorld[description.rootBody]) * initial.entityWorld;
        if (!approxEqual(expected, initial.rootToEntity, 1.0e-3f))
            return fail(error, "initial rootToEntity is inconsistent with root body and entity");
    }
    return true;
}

Transform bodyToBoneModel(const RagdollPose& pose, BodyId body)
{
    if (body >= pose.bodyWorld.size())
        return Transform{};
    return inverse(pose.entityWorld) * pose.bodyWorld[body];
}

Transform entityFromRoot(const RagdollPose& pose, BodyId rootBody, const Transform& rootToEntity)
{
    if (rootBody >= pose.bodyWorld.size())
        return Transform{};
    return pose.bodyWorld[rootBody] * rootToEntity;
}

Transform resolveBoneWorld(BoneWorldKind kind,
                           const Transform& bodyWorld,
                           const Transform& mergedLocal,
                           const Transform& entityWorld,
                           const Transform& animationBoneModel)
{
    switch (kind)
    {
    case BoneWorldKind::Merged:
        return bodyWorld * mergedLocal;
    case BoneWorldKind::Frozen:
        return entityWorld * animationBoneModel;
    case BoneWorldKind::Body:
    default:
        return bodyWorld;
    }
}

Vec3 resolveWorldHitPoint(const Transform& boneWorld, const Vec3& boneLocalPosition)
{
    return transformPoint(boneWorld, boneLocalPosition);
}

bool diagonalizeInertia(const float inertia[6], Vec3& eigenvalues, Quat& axes, std::string* error)
{
    return diagonalizeInertiaImpl(inertia, eigenvalues, axes, error);
}

} // namespace physics_portable
