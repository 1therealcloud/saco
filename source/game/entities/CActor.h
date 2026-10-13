/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "CEntity.h"

class CActor : public CEntity
{
public:
    bool HasNativePed() const
    {
        return m_pGamePed != nullptr;
    }

    void GetBonePosition(CVector& position, unsigned int bone, bool update) const;

    void* GetNativePedHandle() const
    {
        return m_pGamePed;
    }

    void* m_pGamePed = nullptr;          // 0x48, GTA CPed*
    std::uint32_t m_marker = 0;          // 0x4C
    std::uint32_t m_arrow = 0;           // 0x50
    bool m_bNeedsToCreateMarker = false; // 0x54
    bool m_bInvulnerable = false;        // 0x55

    CActor(int model, CVector position, float rotation); // 0x9C2B0
    ~CActor() override;                                  // 0x9C750

    void Destroy();                                                                                                                         // 0x9C400
    void PerformAnimation(const char* animation, const char* file, float frameDelta, int lockA, int lockX, int lockY, int lockF, int time); // 0x9C460
    static void ProcessAnimations();
    void ClearAnimations();        // 0x9C550
    void SetRotation(float angle); // 0x9C570

    float GetHealth() const;           // 0x9C5B0
    void SetHealth(float value);       // 0x9C5D0
    float GetArmour() const;           // 0x9C610
    void SetArmour(float value);       // 0x9C630
    int GetFlags() const;              // 0x9C650
    void SetFlags(int value);          // 0x9C660
    BOOL IsDead() const;               // 0x9C680
    char GetState() const;             // 0x9C6B0
    void SetState(char value);         // 0x9C6C0
    BOOL IsOnGround() const;           // 0x9C6E0
    void SetInvulnerable(bool enable); // 0x9C700
private:
    int m_pinnedCustomModel{};
};
