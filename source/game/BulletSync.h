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

    bool Install();
    void Fire(CPed& ped, const Shot& shot, unsigned lagCompMode);
    CVector HitOffset(void* entity, CVector point, unsigned lagCompMode);

    namespace detail
    {
        using InstantFn = bool(__thiscall*)(void*, void*, CVector*, CVector*, void*, CVector*, CVector*, bool, int);
        using SniperFn = bool(__thiscall*)(void*, void*, void*, CVector*);
        using TraceFn = bool(__cdecl*)(const CVector&, const CVector&, void*, void**, bool, bool, bool, bool, bool, bool, bool, bool);
        extern InstantFn instant;
        extern SniperFn sniper;
        extern TraceFn trace;
        extern void* shooter;
        extern const Shot* remoteShot;
        extern unsigned lagCompMode;
    }
}
