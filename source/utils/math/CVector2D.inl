#pragma once

#include "CVector2D.h"

inline CVector2D CVector2D::RotatedBy(float rad) const
{
    const auto s = std::sin(rad), c = std::cos(rad);
    return {
        x * c - y * s,
        x * s - y * c,
    };
}

inline bool CVector2D::ApproxEqualTo(CVector2D o, float epsilon)
{
    return std::abs(x - o.x) <= epsilon && std::abs(y - o.y) <= epsilon;
}
