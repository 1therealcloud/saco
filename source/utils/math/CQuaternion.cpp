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

#include "CQuaternion.h"

// Quat to matrix
void CQuaternion::Get(RwMatrix* out) const
{
    auto vecImag2 = imag + imag;
    auto x2x = vecImag2.x * imag.x;
    auto y2x = vecImag2.y * imag.x;
    auto z2x = vecImag2.z * imag.x;

    auto y2y = vecImag2.y * imag.y;
    auto z2y = vecImag2.z * imag.y;
    auto z2z = vecImag2.z * imag.z;

    auto x2r = vecImag2.x * real;
    auto y2r = vecImag2.y * real;
    auto z2r = vecImag2.z * real;

    CVector right{1.0F - (z2z + y2y), z2r + y2x, z2x - y2r}, up{y2x - z2r, 1.0F - (z2z + x2x), x2r + z2y}, at{y2r + z2x, z2y - x2r, 1.0F - (y2y + x2x)};
    RwV3dAssign(RwMatrixGetRight(out), &right);
    RwV3dAssign(RwMatrixGetUp(out), &up);
    RwV3dAssign(RwMatrixGetAt(out), &at);
}

// Quat to euler angles
void CQuaternion::Get(float* x, float* y, float* z)
{
    CQuaternion q = *this;
    q.Normalise();
    RwMatrix m{};
    q.Get(&m);
    *x = std::asin(std::clamp(-m.right.z, -1.0f, 1.0f));
    if (std::abs(std::cos(*x)) > 0.000001f)
    {
        *y = std::atan2(m.right.y, m.right.x);
        *z = std::atan2(m.up.z, m.at.z);
    }
    else
    {
        *y = std::atan2(-m.up.x, m.up.y);
        *z = 0.0f;
    }
    for (float* angle : {x, y, z})
        if (*angle < 0.0f)
            *angle += 2.0f * std::numbers::pi_v<float>;
}

// Quat to axis & angle
void CQuaternion::Get(RwV3d* axis, float* angle)
{
    CQuaternion q = *this;
    q.Normalise();
    if (q.w < 0.0f)
        q = -q;
    const float length = q.imag.Magnitude();
    *angle = 2.0f * std::atan2(length, q.w);
    *axis = length > 0.000001f ? RwV3d{q.x / length, q.y / length, q.z / length} : RwV3d{1, 0, 0};
}

// Stores result of quat multiplication
void CQuaternion::Multiply(const CQuaternion& a, const CQuaternion& b)
{
    const CVector v = a.imag.Cross(b.imag) + a.imag * b.w + b.imag * a.w;
    const float scalar = a.w * b.w - a.imag.Dot(b.imag);
    *this = CQuaternion{v.x, v.y, v.z, scalar};
}

// Spherical linear interpolation
void CQuaternion::Slerp(const CQuaternion& from, const CQuaternion& to, float halftheta, float sintheta_inv, float t)
{
    const float a = std::sin((1.0f - t) * halftheta) * sintheta_inv;
    const float b = std::sin(t * halftheta) * sintheta_inv;
    if (!std::isfinite(sintheta_inv) || std::abs(std::sin(halftheta)) < 0.000001f)
    {
        Slerp(from, to, t);
        return;
    }
    *this = from * a + to * b;
}

// Quat from matrix
void CQuaternion::Set(const RwMatrix& m)
{
    const float trace = m.right.x + m.up.y + m.at.z;
    if (trace > 0.0f)
    {
        const float s = 2.0f * std::sqrt(trace + 1.0f);
        *this = {(m.up.z - m.at.y) / s, (m.at.x - m.right.z) / s, (m.right.y - m.up.x) / s, s * 0.25f};
    }
    else if (m.right.x >= m.up.y && m.right.x >= m.at.z)
    {
        const float s = 2.0f * std::sqrt(std::max(0.0f, 1.0f + m.right.x - m.up.y - m.at.z));
        *this = {s * 0.25f, (m.up.x + m.right.y) / s, (m.at.x + m.right.z) / s, (m.up.z - m.at.y) / s};
    }
    else if (m.up.y >= m.at.z)
    {
        const float s = 2.0f * std::sqrt(std::max(0.0f, 1.0f + m.up.y - m.right.x - m.at.z));
        *this = {(m.up.x + m.right.y) / s, s * 0.25f, (m.at.y + m.up.z) / s, (m.at.x - m.right.z) / s};
    }
    else
    {
        const float s = 2.0f * std::sqrt(std::max(0.0f, 1.0f + m.at.z - m.right.x - m.up.y));
        *this = {(m.at.x + m.right.z) / s, (m.at.y + m.up.z) / s, s * 0.25f, (m.right.y - m.up.x) / s};
    }
    Normalise();
}

// Quat from euler angles
void CQuaternion::Set(float x, float y, float z)
{
    // gta uses pitch (Y), yaw (Z), roll (X), composed as Rz * Ry * Rx
    const float sx = std::sin(x * 0.5f), cx = std::cos(x * 0.5f);
    const float sy = std::sin(y * 0.5f), cy = std::cos(y * 0.5f);
    const float sz = std::sin(z * 0.5f), cz = std::cos(z * 0.5f);
    *this = {cx * cy * sz - sx * sy * cz, sx * cy * cz + cx * sy * sz, cx * sy * cz - sx * cy * sz, cx * cy * cz + sx * sy * sz};
}

// Quat from axis & angle
void CQuaternion::Set(RwV3d* axis, float angle)
{
    const CVector direction{*axis};
    const float length = direction.Magnitude();
    if (length == 0.0f)
    {
        *this = {0, 0, 0, 1};
        return;
    }
    const float scale = std::sin(angle * 0.5f) / length;
    *this = {axis->x * scale, axis->y * scale, axis->z * scale, std::cos(angle * 0.5f)};
}

// Spherical linear interpolation
void CQuaternion::Slerp(const CQuaternion& from, const CQuaternion& to, float t)
{
    CQuaternion a = from, b = to;
    a.Normalise();
    b.Normalise();
    float dot = DotProduct(a, b);
    if (dot < 0.0f)
    {
        b = -b;
        dot = -dot;
    }
    dot = std::clamp(dot, 0.0f, 1.0f);
    if (dot > 0.9995f)
    {
        *this = a * (1.0f - t) + b * t;
        Normalise();
        return;
    }
    const float theta = std::acos(dot);
    Slerp(a, b, theta, 1.0f / std::sin(theta), t);
    Normalise();
}

// Conjugate of a quat
void CQuaternion::Conjugate()
{
    imag = -imag;
}

// Squared length of a quat
float CQuaternion::GetLengthSquared() const
{
    // Originally NOP.
    return (x * x) + (y * y) + (z * z) + (w * w);
}

// Multiplies quat by a floating point value
void CQuaternion::Scale(float multiplier)
{
    *this *= multiplier;
}

// Copies value from other quat
void CQuaternion::Copy(const CQuaternion& from)
{
    *this = from;
}

// Gets a dot product for quats
float CQuaternion::Dot(const CQuaternion& rhs)
{
    return this->w * rhs.w + this->z * rhs.z + this->y * rhs.y + this->x * rhs.x;
}

// Normalises a quat
void CQuaternion::Normalise()
{
    const auto sqMag = GetLengthSquared();
    if (sqMag == 0.f)
    {
        w = 1.0;
    }
    else
    {
        *this = *this / std::sqrt(sqMag);
    }
}
