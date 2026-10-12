/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include "game/entities/CEntity.h"
#include "utils/math/CVector.h"

#include <cstddef>
#include <cstdint>

class CMatrix;
struct RwMatrixTag;

class CCamera
{
public:
    CCamera();  // 0x9FF60
    ~CCamera(); // 0x9FF70

    static CVector GetPosition();
    static bool HasLineOfSight(const CVector& position);
    static CVector TraceTarget(const CVector& origin, const CVector& target, bool vehicles, bool objects);

    void Fade(BOOL in);                                           // 0x9D440
    void GetMatrix(CMatrix* matrix) const;                        // 0x9D460
    void SetMatrix(CMatrix matrix);                               // 0x9D4E0
    void TakeControl(void* target, short mode, short switchType); // 0x9D570
    void TakeControl(CEntity* target, short mode, short switchType)
    {
        TakeControl(target ? reinterpret_cast<void*>(target->GetNativeHandle()) : nullptr, mode, switchType);
    }
    void TakeControl(const CEntity& target, short mode, short switchType)
    {
        TakeControl(reinterpret_cast<void*>(target.GetNativeHandle()), mode, switchType);
    }
    void TakeControl(std::nullptr_t, short mode, short switchType)
    {
        TakeControl(static_cast<void*>(nullptr), mode, switchType);
    }

    void SetMoveVector(const CVector* from, const CVector* to, int time, bool smooth);  // 0x9D590
    void SetTrackVector(const CVector* from, const CVector* to, int time, bool smooth); // 0x9D600
    void SetMoveVector(const CVector* from, const CVector* to, int time, std::uint8_t cutType);
    void SetTrackVector(const CVector* from, const CVector* to, int time, std::uint8_t cutType);
    void Attach(CEntity* owner);                          // 0x9D660
    void SetToOwner();                                    // 0x9D6B0
    float GetDistanceToPoint(const CVector* point) const; // 0x9D700
    void Restore();                                       // 0x9D740
    void Set(CVector position, CVector rotation);         // 0x9D780
    void PointAt(CVector point, int switchStyle);         // 0x9D7E0
    void Detach();                                        // 0x9D830

    bool IsAttachedTo(const CEntity& owner) const
    {
        return m_pAttachedTo == &owner;
    }

private:
    CEntity* m_pAttachedTo = nullptr;
    RwMatrixTag* m_pMatrix = nullptr;
};
