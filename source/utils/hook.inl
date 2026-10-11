#pragma once

#include <stdafx.h>

namespace hook
{
    // original gets the trampoline when requested
    template <typename T>
    bool install(uintptr_t address, T detour, T* original = nullptr)
    {
        Log("[hook::install] address=0x%08X, detour=0x%p, original=%s", address, reinterpret_cast<void*>(detour), original ? "yes" : "no");

        auto* target = reinterpret_cast<LPVOID>(address);

        if (MH_CreateHook(target, reinterpret_cast<LPVOID>(detour), reinterpret_cast<LPVOID*>(original)) != MH_OK)
        {
            Log("[hook::install] MH_CreateHook failed");
            return false;
        }

        Log("[hook::install] MH_CreateHook succeeded");

        const bool result = MH_EnableHook(target) == MH_OK;

        Log("[hook::install] MH_EnableHook %s", result ? "success" : "failed");

        return result;
    }
}
