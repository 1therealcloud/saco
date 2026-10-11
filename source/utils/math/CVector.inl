#pragma once

#include "CVector.h"

inline float CVector::Magnitude() const
{
    return sqrt(x * x + y * y + z * z);
}

inline float CVector::Magnitude2D() const
{
    return sqrt(x * x + y * y);
}

inline void CVector::Normalise()
{
    (void)NormaliseAndMag();
}

inline float CVector::NormaliseAndMag()
{
    const auto fDot = x * x + y * y + z * z;
    if (fDot <= 0.0F)
    {
        x = 1.0F;
        return 1.0F;
    }

    const auto fRecip = 1.0F / sqrt(fDot);
    x *= fRecip;
    y *= fRecip;
    z *= fRecip;

    return 1.0F / fRecip;
}

inline CVector CVector::Cross(const CVector& o) const
{
    return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
}

inline void CVector::Cross_OG(const CVector& a, const CVector& b)
{
    *this = a.Cross(b);
}

inline void CVector::Sum(const CVector& left, const CVector& right)
{
    x = left.x + right.x;
    y = left.y + right.y;
    z = left.z + right.z;
}

inline void CVector::Difference(const CVector& left, const CVector& right)
{
    x = left.x - right.x;
    y = left.y - right.y;
    z = left.z - right.z;
}

inline void CVector::operator+=(const CVector& right)
{
    x += right.x;
    y += right.y;
    z += right.z;
}

inline void CVector::operator-=(const CVector& right)
{
    x -= right.x;
    y -= right.y;
    z -= right.z;
}

inline void CVector::operator*=(const CVector& right)
{
    x *= right.x;
    y *= right.y;
    z *= right.z;
}

inline void CVector::operator*=(float multiplier)
{
    x *= multiplier;
    y *= multiplier;
    z *= multiplier;
}

inline void CVector::operator/=(float divisor)
{
    x /= divisor;
    y /= divisor;
    z /= divisor;
}

inline CVector* CrossProduct(CVector* out, CVector* a, CVector* b)
{
    *out = a->Cross(*b);
    return out;
}

inline CVector CrossProduct(const CVector& a, const CVector& b)
{
    return a.Cross(b);
}

inline float DotProduct(CVector* v1, CVector* v2)
{
    return v1->z * v2->z + v1->y * v2->y + v1->x * v2->x;
}

inline float DotProduct(const CVector& v1, const CVector& v2)
{
    return v1.z * v2.z + v1.y * v2.y + v1.x * v2.x;
}

inline float DotProduct2D(const CVector& v1, const CVector& v2)
{
    return v1.y * v2.y + v1.x * v2.x;
}
