#pragma once

#include <stdafx.h>

namespace hook
{
    // original gets the trampoline when requested
    template <typename T>
    bool install(uintptr_t address, T detour, T* original = nullptr)
    {
        auto* target = reinterpret_cast<LPVOID>(address);

        const MH_STATUS createStatus = MH_CreateHook(target, reinterpret_cast<LPVOID>(detour), reinterpret_cast<LPVOID*>(original));
        if (createStatus != MH_OK)
        {
#if defined(_DEBUG)
            Log("[hook::install] address=0x%08X, detour=0x%p, original=%s, create=%s", static_cast<unsigned int>(address), reinterpret_cast<void*>(detour), original ? "yes" : "no",
                MH_StatusToString(createStatus));
#endif // _DEBUG
            return false;
        }

        const MH_STATUS enableStatus = MH_EnableHook(target);

#if defined(_DEBUG)
        Log("[hook::install] address=0x%08X, detour=0x%p, original=%s, enable=%s", static_cast<unsigned int>(address), reinterpret_cast<void*>(detour), original ? "yes" : "no",
            MH_StatusToString(enableStatus));
#endif // _DEBUG

        return enableStatus == MH_OK;
    }
}
