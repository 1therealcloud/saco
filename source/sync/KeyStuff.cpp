#include <stdafx.h>

#include "KeyStuff.h"

#include "game/CPad.h"


namespace KeyStuff
{
    namespace
    {
        constexpr std::size_t kPlayerCount = PLAYER_COUNT;
        constexpr std::size_t kPadSize = 0x134;
        constexpr std::size_t kDriveByLeftOffset = 0x0A;
        constexpr std::size_t kDriveByRightOffset = 0x0E;

        struct alignas(CPad) PadStorage
        {
            std::byte data[kPadSize];
        };

        static_assert(sizeof(CPad) == kPadSize, "GTA:SA CPad must be 0x134 bytes for SA-MP R5 KeyStuff");
        static_assert(sizeof(PadStorage) == kPadSize);
        static_assert(alignof(PadStorage) == alignof(CPad));
        static_assert(sizeof(bool) == 1, "SA-MP R5 KeyStuff requires 1-byte bool");

        // These are raw BSS blocks in samp.dll. Do not instantiate CPad here:
        // plugin-sdk's CPad constructor calls GTA at 0x541D80, while R5 merely
        // zeroes/copies these bytes itself.
        PadStorage g_localPlayerKeys{};
        std::array<PadStorage, kPlayerCount> g_playerKeys{};
        bool g_savedDriveByLeft{};
        bool g_savedDriveByRight{};
        CPad* g_internalKeys = reinterpret_cast<CPad*>(0xB73458); // 0x114AE8
        bool* g_driveByLeft = reinterpret_cast<bool*>(0xB6F1A4);  // 0x114AEC
        bool* g_driveByRight = reinterpret_cast<bool*>(0xB6F1A5); // 0x114AF0

        CPad* LocalPlayerKeys()
        {
            return reinterpret_cast<CPad*>(&g_localPlayerKeys);
        }

        CPad* PlayerKeys()
        {
            return reinterpret_cast<CPad*>(g_playerKeys.data());
        }

        bool ReadPadWordAsBool(const PadStorage& pad, std::size_t offset)
        {
            std::int16_t value{};
            std::memcpy(&value, pad.data + offset, sizeof(value));
            return value != 0;
        }
    }

    CPad& RefLocalPlayerKeys()
    {
        return *LocalPlayerKeys();
    }

    CPad* ArrayPlayerKeys()
    {
        return PlayerKeys();
    }

    CPad*& RefInternalKeys()
    {
        return g_internalKeys;
    }

    bool*& RefDriveByLeft()
    {
        return g_driveByLeft;
    }

    bool*& RefDriveByRight()
    {
        return g_driveByRight;
    }

    bool& RefSavedDriveByLeft()
    {
        return g_savedDriveByLeft;
    }

    bool& RefSavedDriveByRight()
    {
        return g_savedDriveByRight;
    }

    void Initialize()
    {
        std::memset(&g_localPlayerKeys, 0, kPadSize);
        std::memset(g_playerKeys.data(), 0, sizeof(g_playerKeys));
    }

    void UpdateKeys()
    {
        std::memcpy(&g_localPlayerKeys, RefInternalKeys(), kPadSize);
        g_savedDriveByLeft = *RefDriveByLeft();
        g_savedDriveByRight = *RefDriveByRight();
    }

    void ApplyKeys()
    {
        std::memcpy(RefInternalKeys(), &g_localPlayerKeys, kPadSize);
        *RefDriveByLeft() = g_savedDriveByLeft;
        *RefDriveByRight() = g_savedDriveByRight;
    }

    void SetKeys(int player, const CPad* keys)
    {
        if (!keys || player < 0 || static_cast<std::size_t>(player) >= kPlayerCount)
            return;
        std::memcpy(&g_playerKeys[player], keys, kPadSize);
    }

    void ApplyKeys(int player)
    {
        if (player < 0 || static_cast<std::size_t>(player) >= kPlayerCount)
            return;
        const PadStorage& keys = g_playerKeys[player];
        std::memcpy(RefInternalKeys(), &keys, kPadSize);
        *RefDriveByLeft() = ReadPadWordAsBool(keys, kDriveByLeftOffset);
        *RefDriveByRight() = ReadPadWordAsBool(keys, kDriveByRightOffset);
    }

    CPad* GetInternalKeys()
    {
        return RefInternalKeys();
    }

    CPad* GetKeys()
    {
        return LocalPlayerKeys();
    }

    CPad* GetKeys(int player)
    {
        if (player < 0 || static_cast<std::size_t>(player) >= kPlayerCount)
            return nullptr;
        return reinterpret_cast<CPad*>(&g_playerKeys[player]);
    }

    void ResetKeys(int player)
    {
        if (player < 0 || static_cast<std::size_t>(player) >= kPlayerCount)
            return;
        std::memset(&g_playerKeys[player], 0, kPadSize);
    }

    void ResetInternalKeys()
    {
        std::memset(RefInternalKeys(), 0, 0x60);
    }
}
