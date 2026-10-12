/*
    Plugin-SDK file
    Authors: GTA Community. See more here
    https://github.com/DK22Pac/plugin-sdk
    Do not delete this comment block. Respect others' work!
*/

#include <stdafx.h>
#include "CMatrix.h"
#include <cmath>
#include <numbers>
#include <random>

#include "CVector.h"
#include "CVector2D.h"

/*!
* @brief From a 2D vector and Z position
*/
CVector::CVector(const CVector2D& v2d, float Z) : RwV3d{v2d.x, v2d.y, Z}
{}

/*!
* @returns A vector with each of its components set to a number in the given range [min, max)
*/
namespace
{
    float RandomComponent(float min, float max)
    {
        if (min > max)
            std::swap(min, max);
        if (min == max)
            return min;
        thread_local std::mt19937 generator{std::random_device{}()};
        return std::min(std::uniform_real_distribution<float>{min, max}(generator), std::nextafter(max, min));
    }
}
CVector CVector::Random(CVector min, CVector max)
{
    return {RandomComponent(min.x, max.x), RandomComponent(min.y, max.y), RandomComponent(min.z, max.z)};
}
CVector CVector::Random(float min, float max)
{
    return Random(CVector{min, min, min}, CVector{max, max, max});
}

// Returns length of vector

// Normalises a vector

// Normalises a vector and returns length

auto CVector::Dot(const CVector& o) const -> float
{
    return DotProduct(*this, o);
}

auto CVector::Dot2D(const CVector& o) const -> float
{
    return y * o.y + x * o.x;
}

// notsa

// 0x70F890

// Adds left + right and stores result

// Subtracts left - right and stores result

// Adds value from the second vector.

// Multiplies vector by a floating point value

// Divides vector by a floating point value

void CVector::FromMultiply(const CMatrix& matrix, const CVector& vector)
{
    *this = matrix.TransformPoint(vector);
}

void CVector::FromMultiply3x3(const CMatrix& matrix, const CVector& vector)
{
    *this = matrix.TransformVector(vector);
}

float CVector::Heading(bool limitAngle) const
{
    const auto radians = std::atan2(-x, y);
    if (limitAngle)
    {
        return (radians >= std::numbers::pi_v<float> ? radians - 2.0f * std::numbers::pi_v<float> : radians);
    }
    return radians;
}

CVector Multiply3x3_MV(const CMatrix& m, const CVector& v)
{
    return m.TransformVector(v);
}
CVector Multiply3x3_VM(const CVector& v, const CMatrix& m)
{
    return m.InverseTransformVector(v);
}
CVector MultiplyMatrixWithVector(const CMatrix& mat, const CVector& vec)
{
    return mat.TransformPoint(vec);
}
