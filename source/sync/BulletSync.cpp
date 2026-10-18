/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "BulletSync.h"
#include "game/entities/CPed.h"
#include "game/PlayerSlots.h"
#include "sync/AimStuff.h"

namespace game::bullets
{

    namespace detail
    {
        void* shooter = nullptr;
        const Shot* remoteShot = nullptr;
        unsigned lagCompMode = 1;
    }

    CVector HitOffset(void* entity, CVector point, unsigned lagCompMode)
    {
        ZoneScoped;
        if (!entity)
            return CVector{};
        const auto* native = static_cast<const std::uint8_t*>(entity);
        const auto* matrix = *reinterpret_cast<const CMatrix* const*>(native + 0x14);
        if (matrix)
            return lagCompMode == 0 ? matrix->InverseTransformPoint(point) : point - matrix->GetPosition();
        return point - *reinterpret_cast<const CVector*>(native + 4);
    }

    void Fire(CPed& ped, const Shot& shot, unsigned lagCompMode)
    {
        ZoneScoped;
        if (!ped.HasNativePed() || !detail::instant || !detail::sniper)
            return;
        const auto slot = ped.m_nPlayerNumber;
        if (!slot || slot >= game::players::SLOT_COUNT)
            return;

        auto& secondary = *reinterpret_cast<std::uint16_t*>(0xB6F858);
        auto* mode = AimStuff::RefInternalCameraMode();
        const auto savedMode = *mode;
        const auto savedSecondary = secondary;
        AimStuff::UpdateAim();
        AimStuff::UpdateCameraExtZoomAndAspectRatio();
        AimStuff::UpdatePlayerStats();
        AimStuff::ApplyAim(slot);
        AimStuff::ApplyCameraExtZoomAndAspectRatio(slot);
        AimStuff::ApplyPlayerStats(slot);
        *mode = AimStuff::GetCameraMode(slot);
        secondary = *mode == 4 ? 0 : static_cast<std::uint8_t>(*mode);

        void* savedShooter = detail::shooter;
        const Shot* savedShot = detail::remoteShot;
        const auto savedLag = detail::lagCompMode;
        detail::shooter = ped.GetNativePedHandle();
        detail::remoteShot = &shot;
        detail::lagCompMode = lagCompMode;
        auto* native = static_cast<std::uint8_t*>(detail::shooter);
        auto* weapon = native + 0x5A0 + ped.GetWeaponSlot() * 0x1C;
        CVector origin = shot.origin;
        CVector target = shot.target;
        const bool fired = ped.GetCurrentWeapon() == 34 ? detail::sniper(weapon, native, nullptr, nullptr)
                                                        : detail::instant(weapon, native, &origin, &origin, nullptr, &target, nullptr, false, 1);
        // CWeapon::Fire normally reports this; synced shots call its lower-level methods.
        if (fired)
            reinterpret_cast<void(__thiscall*)(void*, int)>(0x4E69F0)(native + 0x394, 145); // AE_WEAPON_FIRE
        detail::shooter = savedShooter;
        detail::remoteShot = savedShot;
        detail::lagCompMode = savedLag;

        *mode = savedMode;
        secondary = savedSecondary;
        AimStuff::ApplyPlayerStats();
        AimStuff::ApplyCameraExtZoomAndAspectRatio();
        AimStuff::ApplyAim();
    }
}
