/*
    Plugin-SDK file
    Authors: GTA Community. See more here
    https://github.com/DK22Pac/plugin-sdk
    Do not delete this comment block. Respect others' work!
*/
#include <stdafx.h>
#include "CMatrix.h"
#include <cmath>
#include <algorithm>
#include <numbers>
#include "CVector2D.h"
#include "CVector.h"

CVector2D::CVector2D(const CVector& v3) : CVector2D{v3.x, v3.y}
{}

void CVector2D::Normalise(float* outMag)
{
    auto mag = Magnitude();
    if (mag > 0.0f)
    {
        auto recip = 1.0F / mag;
        x *= recip;
        y *= recip;
    }
    else
    {
        x = 1.0f;
    }
    if (outMag)
    {
        *outMag = mag;
    }
}

std::uint32_t CVector2D::NodeHeading() const
{
    float angle = std::atan2(x, y);
    if (angle < 0.0f)
        angle += 2.0f * std::numbers::pi_v<float>;
    return static_cast<std::uint32_t>(std::floor((angle + std::numbers::pi_v<float> / 8.0f) / (std::numbers::pi_v<float> / 4.0f))) % 8;
}
