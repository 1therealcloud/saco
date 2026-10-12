#include <stdafx.h>

#include "BulletSync.h"
#include "game/entities/CPed.h"
#include "game/patch/hooks.h"
#include "game/PlayerSlots.h"
#include "sync/AimStuff.h"

namespace game::bullets
{
    namespace detail
    {
        DamageFn damage = nullptr;
        AffectsPedFn affectsPed = nullptr;
        InstantFn instant = nullptr;
        SniperFn sniper = nullptr;
        TraceFn trace = nullptr;
        void* shooter = nullptr;
        const Shot* remoteShot = nullptr;
        unsigned lagCompMode = 1;
    }

    bool Install()
    {
        return ::patch::call(0x73AC4B, reinterpret_cast<const void*>(&SniperBulletHook)) &&
               hook::install(0x73FB10, reinterpret_cast<detail::InstantFn>(&FireInstantHitHook), &detail::instant) &&
               hook::install(0x73AAC0, reinterpret_cast<detail::SniperFn>(&FireSniperHook), &detail::sniper) && hook::install(0x56BA00, &BulletTraceHook, &detail::trace) &&
               hook::install(0x4B5AC0, reinterpret_cast<detail::DamageFn>(&ComputeDamageResponseHook), &detail::damage) &&
               hook::install(0x4B35A0, reinterpret_cast<detail::AffectsPedFn>(&DamageAffectsPedHook), &detail::affectsPed);
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
        if (ped.GetCurrentWeapon() == 34)
            detail::sniper(weapon, native, nullptr, nullptr);
        else
            detail::instant(weapon, native, &origin, &origin, nullptr, &target, nullptr, false, 1);
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
