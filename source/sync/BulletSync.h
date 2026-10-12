/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include "utils/math/CVector.h"

class CPed;

namespace game::bullets
{
    struct Shot
    {
        CVector origin{};
        CVector target{};
        CVector center{};
        void* entity = nullptr;
    };

    void Fire(CPed& ped, const Shot& shot, unsigned lagCompMode);
    CVector HitOffset(void* entity, CVector point, unsigned lagCompMode);

    namespace detail
    {
        using InstantFn = bool(__thiscall*)(void*, void*, CVector*, CVector*, void*, CVector*, CVector*, bool, int);
        using SniperFn = bool(__thiscall*)(void*, void*, void*, CVector*);
        using TraceFn = bool(__cdecl*)(const CVector&, const CVector&, void*, void**, bool, bool, bool, bool, bool, bool, bool, bool);
        struct DamageCalculator
        {
            void* damager;
            float damage;
            std::uint32_t bodyPart;
            int weapon;
            bool speak;
        };
        static_assert(sizeof(DamageCalculator) == 20);
        using DamageFn = void(__thiscall*)(DamageCalculator*, void*, void*, bool);
        using AffectsPedFn = bool(__thiscall*)(void*, void*);
        extern AffectsPedFn affectsPed;
        extern DamageFn damage;
        extern InstantFn instant;
        extern SniperFn sniper;
        extern TraceFn trace;
        extern void* shooter;
        extern const Shot* remoteShot;
        extern unsigned lagCompMode;
    }
}
