#include <stdafx.h>

#include "main.h"

#include "PlayerSlots.h"
#include "patch/hooks.h"
#include "CGame.h"
#include "CPad.h"
#include "entities/CPed.h"
#include "sync/KeyStuff.h"
#include "sync/AimStuff.h"

namespace game::players::detail
{
    constexpr std::uintptr_t kPlayerInfo = 0xB7CD98;
    constexpr std::size_t kPlayerInfoSize = 0x190;
    std::array<bool, game::players::SLOT_COUNT> g_used{};
    std::array<void*, game::players::SLOT_COUNT> g_peds{};
    std::array<std::uint8_t, 8 * 20000> g_scanList{};
    bool g_installed = false;
    bool g_useCJWalk = false;

    constexpr std::uintptr_t kOriginalProcessControl = 0x60EA90;
    constexpr std::uintptr_t kProcessControlVtable = 0x86D190;
    constexpr std::uintptr_t kUpdatePositionCall = 0x5E92F4;
    constexpr std::uintptr_t kCameraRotationA = 0x6884C4;
    constexpr std::uintptr_t kCameraRotationB = 0x688200;
    constexpr std::array<std::uint8_t, 5> kUpdatePositionOriginal{0xE8, 0x17, 0x88, 0xFF, 0xFF};
    constexpr std::array<std::uint8_t, 6> kCameraRotationOriginal{0xD9, 0x96, 0x5C, 0x05, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 5> kNop5{0x90, 0x90, 0x90, 0x90, 0x90};
    constexpr std::array<std::uint8_t, 6> kNop6{0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    ControlFn g_control = reinterpret_cast<ControlFn>(kOriginalProcessControl);

    constexpr std::uintptr_t kScanBase[] = {0x5dc7aa, 0x41a85d, 0x41a864, 0x408259, 0x711b32, 0x699cf8, 0x4092ec,
                                            0x409151, 0x408702, 0x564220, 0x564172, 0x563845, 0x84e9c2, 0x85652d};
    constexpr std::uintptr_t kScanIndexed[] = {0x40d68f, 0x5664da, 0x566589, 0x408709, 0x56b3b4, 0x56ad94, 0x56a862, 0x5675fd, 0x56cd87, 0x56cc7c, 0x56cb54, 0x56ca4d,
                                               0x56c667, 0x56c56c, 0x56c448, 0x56c344, 0x56bd49, 0x56bc56, 0x56be59, 0x56a943, 0x567738, 0x54673b, 0x54bb26, 0x6e31ad,
                                               0x40dc2c, 0x534a0c, 0x534d6e, 0x564b5c, 0x564dac, 0x67ff60, 0x568cbc, 0x568efe, 0x569f5a, 0x56953a, 0x56912a, 0x56b4b8,
                                               0x56b597, 0x56b2c6, 0x56af77, 0x56ae98, 0x56bf52, 0x56aca6, 0x56a769, 0x56a688, 0x70b9bd, 0x5647a0, 0x70acb5, 0x6063ca,
                                               0x699d01, 0x41a864, 0x40e064, 0x40df61, 0x40ddd1, 0x40db11, 0x40d98f, 0x1566858};
    constexpr std::uintptr_t kScanSecond[] = {0x4091c8, 0x40936a, 0x40d9c8, 0x40db4a, 0x40dc64, 0x40de0a, 0x40df9a, 0x40e09d, 0x534a9b, 0x534dfd, 0x71cdb3};
    constexpr std::uintptr_t kScanEnd[] = {0x5634a6, 0x5638df, 0x56420f, 0x564283};

    bool MakeWritable(std::uintptr_t address, std::size_t size)
    {
        DWORD oldProtect{};
        return VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect) != FALSE;
    }

    template <std::size_t N>
    void WriteCode(std::uintptr_t address, const std::array<std::uint8_t, N>& bytes)
    {
        std::memcpy(reinterpret_cast<void*>(address), bytes.data(), bytes.size());
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(address), bytes.size());
    }

    void EnableLocalCameraRotation()
    {
        WriteCode(kCameraRotationA, kCameraRotationOriginal);
        WriteCode(kCameraRotationB, kCameraRotationOriginal);
    }

    void DisableLocalCameraRotation()
    {
        WriteCode(kCameraRotationA, kNop6);
        WriteCode(kCameraRotationB, kNop6);
    }

    int FindSlot(void* ped)
    {
        for (int slot = 2; slot < game::players::SLOT_COUNT; ++slot)
            if (g_used[slot] && g_peds[slot] == ped)
                return slot;
        return 0;
    }

    void ApplyLocalControlSuppressions()
    {
        ZoneScoped;
        auto* gameInstance = g_game.get();
        auto* player = gameInstance ? gameInstance->GetPlayerPed() : nullptr;
        if (!player)
            return;

        CPad pad{};
        const auto* internalKeys = KeyStuff::GetInternalKeys();
        if (!internalKeys)
            return;
        std::memcpy(&pad, internalKeys, sizeof(pad));

        bool changed = false;
        if (player->SuppressesShockButtonLInput())
        {
            pad.NewState.ShockButtonL = 0;
            pad.OldState.ShockButtonL = 0;
            changed = true;
        }
        if (player->SuppressesJumpInput())
        {
            pad.NewState.ButtonSquare = 0;
            pad.OldState.ButtonSquare = 0;
            changed = true;
        }
        if (player->SuppressesCrossInput())
        {
            pad.NewState.ButtonCross = 0;
            pad.OldState.ButtonCross = 0;
            changed = true;
        }
        if (changed)
            std::memcpy(KeyStuff::GetInternalKeys(), &pad, sizeof(pad));
    }

    void SetPositionUpdate(bool enabled)
    {
        ZoneScoped;
        WriteCode(kUpdatePositionCall, enabled ? kUpdatePositionOriginal : kNop5);
    }

    template <std::size_t N>
    bool Relocate(const std::uintptr_t (&addresses)[N], std::uintptr_t value)
    {
        for (auto address : addresses)
            if (!patch::write(address, value))
                return false;
        return true;
    }
}

namespace game::players
{
    using namespace detail;
    std::uint8_t GetActiveSlotCount()
    {
        std::uint8_t count = 0;
        for (int slot = 2; slot < SLOT_COUNT; ++slot)
            if (g_used[slot])
                ++count;
        return count;
    }

    void SetPed(std::uint8_t slot, void* ped)
    {
        if (slot >= 2 && slot < SLOT_COUNT)
            g_peds[slot] = ped;
    }

    bool ReplacePedIfMatches(std::uint8_t slot, void* previous, void* replacement)
    {
        if (slot < 2 || slot >= SLOT_COUNT || !g_used[slot] || g_peds[slot] != previous)
            return false;
        g_peds[slot] = replacement;
        return true;
    }

    bool ReleaseSlotIfPed(std::uint8_t slot, const void* expected)
    {
        if (slot < 2 || slot >= SLOT_COUNT || !g_used[slot] || g_peds[slot] != expected)
            return false;
        ReleaseSlot(slot);
        return true;
    }

    bool Install()
    {
        ZoneScoped;
        if (g_installed)
            return true;
        // r5 skips native group creation for standalone player peds (jnz -> jz)
        // gta has only eight groups; streaming more remote players exhausts them
        if (!patch::write<std::uint8_t>(0x60D64E, 0x84))
            return false;
        const auto scan = reinterpret_cast<std::uintptr_t>(g_scanList.data());
        if (!Relocate(kScanBase, scan) || !Relocate(kScanIndexed, scan) || !Relocate(kScanSecond, scan + 4) || !Relocate(kScanEnd, scan + g_scanList.size()))
            return false;
        // the old scan list becomes storage for native CPlayerInfo slots 2..209
        std::memset(reinterpret_cast<void*>(kPlayerInfo + 2 * kPlayerInfoSize), 0, (SLOT_COUNT - 2) * kPlayerInfoSize);
        const std::uint32_t expectedControl = static_cast<std::uint32_t>(kOriginalProcessControl);
        if (!patch::expect(kProcessControlVtable, expectedControl) || !patch::expect(kUpdatePositionCall, kUpdatePositionOriginal.data(), kUpdatePositionOriginal.size()) ||
            !patch::expect(kCameraRotationA, kCameraRotationOriginal.data(), kCameraRotationOriginal.size()) ||
            !patch::expect(kCameraRotationB, kCameraRotationOriginal.data(), kCameraRotationOriginal.size()))
            return false;
        if (!MakeWritable(kUpdatePositionCall, kUpdatePositionOriginal.size()) || !MakeWritable(kCameraRotationA, kCameraRotationOriginal.size()) ||
            !MakeWritable(kCameraRotationB, kCameraRotationOriginal.size()))
            return false;
        if (!game::hooks::InstallPlayerHooks())
            return false;
        DisableLocalCameraRotation();
        g_installed = true;
        return true;
    }

    int AcquireSlot()
    {
        ZoneScoped;
        if (!g_installed)
            return -1;
        for (int slot = 2; slot < SLOT_COUNT; ++slot)
            if (!g_used[slot])
            {
                g_used[slot] = true;
                KeyStuff::ResetKeys(slot);
                AimStuff::SetAim(slot, AimStuff::GetAim());
                AimStuff::SetCameraMode(*AimStuff::RefInternalCameraMode(), static_cast<std::uint8_t>(slot));
                AimStuff::SetCameraExtZoomAndAspectRatio(static_cast<std::uint8_t>(slot), AimStuff::GetCameraExtZoom(), AimStuff::GetAspectRatio());
                return slot;
            }
        return -1;
    }

    void SetUseCJWalk(bool enabled)
    {
        g_useCJWalk = enabled;
    }

    void ReleaseSlot(std::uint8_t slot)
    {
        if (slot < 2 || slot >= SLOT_COUNT || !g_used[slot])
            return;
        g_peds[slot] = nullptr;
        g_used[slot] = false;
        std::memset(reinterpret_cast<void*>(kPlayerInfo + slot * kPlayerInfoSize), 0, kPlayerInfoSize);
        KeyStuff::ResetKeys(slot);
    }
}
