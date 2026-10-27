#pragma once

// Tested bridge between OGSR's row-vector Fmatrix convention and the
// column-vector contract.
//
// OGSR's Fmatrix stores m[row][column], translation in row 3, and applies
// points as p' = p * M. Fmatrix::mul_43(A, B) means "B followed by A" and its
// exact storage formula is reproduced here. The contract uses
// p' = R * p + t. Therefore:
//
//   transformFromRowMatrix(M) == [transpose(M.rotation) | M.translation]
//   transformFromRowMatrix(mul43(A, B)) == transformFromRowMatrix(A)
//                                          * transformFromRowMatrix(B)
//
// The last identity is tested with non-identity rotations and translations.

#include "physics_portable/ragdoll.h"

namespace physics_portable
{

struct RowMatrix4
{
    float m[4][4]{};
};

RowMatrix4 rowMatrixIdentity();
RowMatrix4 rowMatrixFromTransform(const Transform& transform);
Transform transformFromRowMatrix(const RowMatrix4& matrix);

// Exact replica of OGSR Fmatrix::mul_43(A, B): B is applied before A.
RowMatrix4 rowMatrixMul43(const RowMatrix4& a, const RowMatrix4& b);

// Exact replica of OGSR Fmatrix::transform_tiny.
Vec3 rowMatrixTransformPoint(const RowMatrix4& matrix, const Vec3& point);

} // namespace physics_portable
