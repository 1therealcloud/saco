/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>
#include <cstdint>
#include "utils/math/CMatrix.h"

class CEntity
{
public:
    char pad_4[60]{};
    void* m_pGameEntity = nullptr;
    void* GetNativeHandle() const
    {
        return m_pGameEntity;
    }
    std::uint32_t m_handle = 0;

    CEntity() = default;          // 0x9C260
    virtual ~CEntity() = default; // 0x9C280
    virtual void Add();           // 0x9F300
    virtual void Remove();        // 0x9F400
    CEntity(const CEntity&) = delete;
    CEntity& operator=(const CEntity&) = delete;

    bool IsValid() const;
    bool IsVisible() const;
    bool IntersectsCameraRay(const CVector& origin, const CVector& target, float scale) const;
    void GetMatrix(CMatrix* matrix) const;                                                // 0x9EB10
    void SetMatrix(const CMatrix& matrix);                                                // 0x9EBC0
    void GetSpeed(CVector* speed) const;                                                  // 0x9ECE0
    void SetSpeed(CVector speed);                                                         // 0x9ED10
    void GetTurnSpeed(CVector* speed) const;                                              // 0x9EE30
    void SetTurnSpeed(CVector speed);                                                     // 0x9EE60
    void ApplyTurnSpeed();                                                                // 0x9EE90
    float GetDistanceFromCentreOfMassToBaseOfModel() const;                               // 0x9EEB0
    void GetBoundCentre(CVector* centre) const;                                           // 0x9EEF0
    void SetModelIndex(int model);                                                        // 0x9EF50
    int GetModelIndex() const;                                                            // 0x9F030
    void Teleport(CVector position);                                                      // 0x9F040
    float GetDistanceToLocalPlayer() const;                                               // 0x9F0C0
    float GetDistanceToLocalPlayerNoHeight() const;                                       // 0x9F1F0
    float GetDistanceToCamera() const;                                                    // 0x9F190
    float GetDistanceToPoint(CVector position) const;                                     // 0x9F2B0
    BOOL DoesExist() const;                                                               // 0x9F3D0
    BOOL EnforceWorldBoundries(float maxX, float minX, float maxY, float minY);           // 0x9F420
    BOOL HasExceededWorldBoundries(float maxX, float minX, float maxY, float minY) const; // 0x9F5C0
    void GetEulerInverted(float* x, float* y, float* z) const;                            // 0x9F8F0
    BOOL IsIgnored() const;                                                               // 0x9FCE0
    BOOL IsStationary() const;                                                            // 0x9FDE0
    BOOL GetCollisionFlag() const;                                                        // 0x9F660
    void SetCollisionFlag(BOOL enabled);                                                  // 0x9F630
    void SetCollisionProcessed(BOOL processed);                                           // 0x9F680
    RwObject* GetRwObject() const;                                                        // 0x9FA60
    void DeleteRwObject();                                                                // 0x9FBB0
    void UpdateRwFrame();                                                                 // 0x9EC80

protected:
    void UpdateMatrixInWorld(const CMatrix& matrix); // 0x9FD00
};
