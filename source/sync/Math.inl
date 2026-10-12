/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <DirectXMath.h>

#include <cmath>

#include "utils/math/CMatrix.h"
#include "utils/math/CVector.h"

namespace sync::math
{
    inline DirectX::XMVECTOR Load(const CVector& value) noexcept
    {
        return DirectX::XMVectorSet(value.x, value.y, value.z, 0.0f);
    }

    inline CVector Store(DirectX::FXMVECTOR value) noexcept
    {
        DirectX::XMFLOAT3 result{};
        DirectX::XMStoreFloat3(&result, value);
        return CVector{result.x, result.y, result.z};
    }

    inline DirectX::XMMATRIX ToMatrix(const CMatrix& matrix) noexcept
    {
        using namespace DirectX;
        const auto& right = matrix.GetRight();
        const auto& forward = matrix.GetForward();
        const auto& up = matrix.GetUp();
        const auto& position = matrix.GetPosition();

        // gta/renderware axes are rows, same as directxmath's row-vector matrices.
        return XMMATRIX{
            XMVectorSet(right.x, right.y, right.z, 0.0f),
            XMVectorSet(forward.x, forward.y, forward.z, 0.0f),
            XMVectorSet(up.x, up.y, up.z, 0.0f),
            XMVectorSet(position.x, position.y, position.z, 1.0f)
        };
    }

    inline DirectX::XMVECTOR NormalizeQuaternion(DirectX::FXMVECTOR quaternion) noexcept
    {
        using namespace DirectX;
        const float lengthSquared = XMVectorGetX(XMVector4LengthSq(quaternion));
        if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f)
            return XMQuaternionIdentity();
        return XMQuaternionNormalize(quaternion);
    }

    inline DirectX::XMVECTOR QuaternionFromMatrix(const CMatrix& matrix) noexcept
    {
        return NormalizeQuaternion(DirectX::XMQuaternionRotationMatrix(ToMatrix(matrix)));
    }

    inline DirectX::XMVECTOR QuaternionFromWire(const float (&wireValues)[4]) noexcept
    {
        // sa-mp stores w,x,y,z; directxmath stores x,y,z,w.
        // r5's matrix->quaternion (0x100b4d70) uses the opposite sign convention to
        // directxmath's row-vector one, so the wire quaternion is the conjugate.
        const DirectX::XMVECTOR wireQuaternion = DirectX::XMVectorSet(wireValues[1], wireValues[2], wireValues[3], wireValues[0]);
        return NormalizeQuaternion(DirectX::XMQuaternionConjugate(wireQuaternion));
    }

    inline void QuaternionToWire(DirectX::FXMVECTOR quaternion, float (&wireValues)[4]) noexcept
    {
        DirectX::XMFLOAT4 conjugatedQuaternion{};
        DirectX::XMStoreFloat4(&conjugatedQuaternion, NormalizeQuaternion(DirectX::XMQuaternionConjugate(quaternion)));
        wireValues[0] = conjugatedQuaternion.w;
        wireValues[1] = conjugatedQuaternion.x;
        wireValues[2] = conjugatedQuaternion.y;
        wireValues[3] = conjugatedQuaternion.z;
    }

    inline void SetRotation(CMatrix& matrix, DirectX::FXMVECTOR quaternion) noexcept
    {
        const DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationQuaternion(NormalizeQuaternion(quaternion));
        matrix.GetRight() = Store(rotation.r[0]);
        matrix.GetForward() = Store(rotation.r[1]);
        matrix.GetUp() = Store(rotation.r[2]);
    }

    inline DirectX::XMVECTOR Slerp(DirectX::FXMVECTOR target, DirectX::FXMVECTOR current, float factor) noexcept
    {
        return NormalizeQuaternion(DirectX::XMQuaternionSlerp(target, current, factor));
    }

    inline CVector AbsoluteDifference(const CVector& a, const CVector& b) noexcept
    {
        return Store(DirectX::XMVectorAbs(DirectX::XMVectorSubtract(Load(a), Load(b))));
    }

    inline bool ErrorExceeds(const CVector& a, const CVector& b, float xy, float z) noexcept
    {
        const CVector delta = AbsoluteDifference(a, b);
        return delta.x > xy || delta.y > xy || delta.z > z;
    }

    inline void CorrectVelocity(CVector& speed, const CVector& target, const CVector& current, float deadZone, float factor) noexcept
    {
        using namespace DirectX;
        const XMVECTOR error = XMVectorSubtract(Load(target), Load(current));
        const XMVECTOR valid = XMVectorGreater(XMVectorAbs(error), XMVectorReplicate(deadZone));
        const XMVECTOR correction = XMVectorSelect(XMVectorZero(), XMVectorScale(error, factor), valid);
        speed = Store(XMVectorAdd(Load(speed), correction));
    }

    inline bool SetUnoccupiedRotation(CMatrix& matrix, const CVector& rawRight, const CVector& rawForward) noexcept
    {
        using namespace DirectX;
        XMVECTOR right = Load(rawRight);
        XMVECTOR forward = Load(rawForward);
        if (XMVectorGetX(XMVector3LengthSq(right)) < 0.000001f || XMVectorGetX(XMVector3LengthSq(forward)) < 0.000001f)
            return false;
        right = XMVector3Normalize(right);
        forward = XMVector3Normalize(forward);
        XMVECTOR up = XMVector3Cross(right, forward);
        if (XMVectorGetX(XMVector3LengthSq(up)) < 0.000001f)
            return false;
        up = XMVector3Normalize(up);
        forward = XMVector3Normalize(XMVector3Cross(up, right));

        matrix.GetRight() = Store(right);
        matrix.GetForward() = Store(forward);
        matrix.GetUp() = Store(up);
        return true;
    }

    inline float Distance(const CVector& a, const CVector& b) noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(Load(a), Load(b))));
    }
}
