#pragma once

#include <cstdint>

namespace game::players
{
    constexpr int SLOT_COUNT = 210;
    bool Install();
    int AcquireSlot();
    void ReleaseSlot(std::uint8_t slot);
    std::uint8_t GetActiveSlotCount(); // saco wrapper for r5 0xa00f0
    void SetPed(std::uint8_t slot, void* ped);
    bool ReplacePedIfMatches(std::uint8_t slot, void* previous, void* replacement);
    bool ReleaseSlotIfPed(std::uint8_t slot, const void* expected);
    void SetUseCJWalk(bool enabled);
}

namespace game::players::detail
{
    using ControlFn = void(__thiscall*)(void*);
    using InfoFn = void*(__thiscall*)(void*);
    using ShadowFn = void*(__thiscall*)(void*, void*);
    extern ControlFn g_control;
    extern InfoFn g_info;
    extern ShadowFn g_shadow;
    extern bool g_useCJWalk;
    int FindSlot(void* ped);
    void ApplyLocalControlSuppressions();
    void EnableLocalCameraRotation();
    void DisableLocalCameraRotation();
    void SetPositionUpdate(bool enabled);
}
