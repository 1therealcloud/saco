#include <stdafx.h>

#include "AimStuff.h"

#include <bit>

namespace AimStuff
{
    namespace
    {
        constexpr std::size_t kPlayerCount = PLAYER_COUNT;

        struct alignas(Aim) AimStorage
        {
            std::byte data[sizeof(Aim)];
        };

        static_assert(sizeof(AimStorage) == sizeof(Aim));
        static_assert(alignof(AimStorage) == alignof(Aim));

        float g_localPlayerCameraExtZoom{};
        float g_localPlayerAspectRatio{};
        AimStorage g_localPlayerAim{};

        std::array<float, kPlayerCount> g_cameraExtZoom{};
        std::array<float, kPlayerCount> g_aspectRatio{};
        std::array<char, kPlayerCount> g_cameraMode{};
        std::array<AimStorage, kPlayerCount> g_playerAim{};

        constexpr std::size_t kProcessControlStatCount = PLAYER_STAT_COUNT;
        constexpr std::size_t kPlayerStatOffset = 69;
        std::array<float, kProcessControlStatCount> g_localPlayerStats{};
        // 11 stats per remote player.
        std::array<float, kPlayerCount * kProcessControlStatCount> g_playerStats{};

        Aim* g_internalAim = reinterpret_cast<Aim*>(0xB6F32C);               // 0x1039C8
        float* g_internalPlayerStats = reinterpret_cast<float*>(0xB79380);   // 0x1039CC
        float* g_internalAspectRatio = reinterpret_cast<float*>(0xC3EFA4);   // 0x1039D0
        float* g_internalCameraExtZoom = reinterpret_cast<float*>(0xB6F250); // 0x1039D4
        char* g_internalCameraMode = reinterpret_cast<char*>(0xB6F1A8);      // 0x113974

        constexpr float kAspectOffset = 1.0f;
        constexpr float kCameraZoomBase = 35.0f;
        constexpr float kCameraZoomScale = std::bit_cast<float>(0x3CEA0EA1u);
        constexpr float kDefaultCameraExtZoom = 1.0f;
        constexpr float kDefaultAspectRatio = std::bit_cast<float>(0x3EAAAA9Fu);
        constexpr float kDefaultPlayerStat = std::bit_cast<float>(0x4479C000u);

        Aim* LocalPlayerAim()
        {
            return reinterpret_cast<Aim*>(&g_localPlayerAim);
        }

        Aim* PlayerAimArray()
        {
            return reinterpret_cast<Aim*>(g_playerAim.data());
        }
    }

    float& RefLocalPlayerCameraExtZoom()
    {
        return g_localPlayerCameraExtZoom;
    }

    float& RefLocalPlayerAspectRatio()
    {
        return g_localPlayerAspectRatio;
    }

    float*& RefInternalCameraExtZoom()
    {
        return g_internalCameraExtZoom;
    }

    float*& RefInternalAspectRatio()
    {
        return g_internalAspectRatio;
    }

    float* ArrayCameraExtZoom()
    {
        return g_cameraExtZoom.data();
    }

    float* ArrayAspectRatio()
    {
        return g_aspectRatio.data();
    }

    char* ArrayCameraMode()
    {
        return g_cameraMode.data();
    }

    char*& RefInternalCameraMode()
    {
        return g_internalCameraMode;
    }

    Aim& RefLocalPlayerAim()
    {
        return *LocalPlayerAim();
    }

    Aim* ArrayPlayerAim()
    {
        return PlayerAimArray();
    }

    Aim*& RefInternalAim()
    {
        return g_internalAim;
    }

    void UpdateCameraExtZoomAndAspectRatio()
    {
        ZoneScoped;
        g_localPlayerCameraExtZoom = *RefInternalCameraExtZoom();
        g_localPlayerAspectRatio = *RefInternalAspectRatio();
    }

    void ApplyCameraExtZoomAndAspectRatio()
    {
        *RefInternalCameraExtZoom() = g_localPlayerCameraExtZoom;
        *RefInternalAspectRatio() = g_localPlayerAspectRatio;
    }

    void SetCameraExtZoomAndAspectRatio(std::uint8_t player, float cameraExtZoom, float aspectRatio)
    {
        if (player >= kPlayerCount)
            return;
        g_cameraExtZoom[player] = cameraExtZoom;
        g_aspectRatio[player] = aspectRatio;
    }

    float GetAspectRatio()
    {
        return *RefInternalAspectRatio() - kAspectOffset;
    }

    float GetCameraExtZoom()
    {
        return (*RefInternalCameraExtZoom() - kCameraZoomBase) * kCameraZoomScale;
    }

    void ApplyCameraExtZoomAndAspectRatio(std::uint8_t player)
    {
        if (player >= kPlayerCount)
            return;
        *RefInternalCameraExtZoom() = g_cameraExtZoom[player] * kCameraZoomBase + kCameraZoomBase;
        *RefInternalAspectRatio() = g_aspectRatio[player] + kAspectOffset;
    }

    void SetCameraMode(char mode, std::uint8_t player)
    {
        if (player >= kPlayerCount)
            return;
        g_cameraMode[player] = mode;
    }

    char GetCameraMode(std::uint8_t player)
    {
        if (player >= kPlayerCount)
            return 4;
        return g_cameraMode[player];
    }

    char GetCameraMode()
    {
        return *RefInternalCameraMode();
    }

    void Initialize()
    {
        ZoneScoped;
        std::memset(&g_localPlayerAim, 0, sizeof(g_localPlayerAim));
        std::memset(g_playerAim.data(), 0, sizeof(g_playerAim));

        g_cameraMode.fill(4);
        g_playerStats.fill(kDefaultPlayerStat);
        g_aspectRatio.fill(kDefaultAspectRatio);
        g_cameraExtZoom.fill(kDefaultCameraExtZoom);
    }

    void UpdateAim()
    {
        ZoneScoped;
        std::memcpy(&g_localPlayerAim, RefInternalAim(), sizeof(Aim));
    }

    void ApplyAim()
    {
        std::memcpy(static_cast<void*>(RefInternalAim()), &g_localPlayerAim, sizeof(Aim));
    }

    Aim* GetAim()
    {
        return RefInternalAim();
    }

    void SetAim(int player, const Aim* aim)
    {
        if (!aim || player < 0 || static_cast<std::size_t>(player) >= kPlayerCount)
            return;
        std::memcpy(&g_playerAim[player], aim, sizeof(Aim));
    }

    void ApplyAim(int player)
    {
        if (player < 0 || static_cast<std::size_t>(player) >= kPlayerCount)
            return;
        std::memcpy(static_cast<void*>(RefInternalAim()), &g_playerAim[player], sizeof(Aim));
    }

    Aim* GetAim(int player)
    {
        if (player < 0 || static_cast<std::size_t>(player) >= kPlayerCount)
            return nullptr;
        return reinterpret_cast<Aim*>(&g_playerAim[player]);
    }

    void SetRemotePlayerStat(int player, int index, std::uint16_t value)
    {
        if (player < 0 || static_cast<std::size_t>(player) >= kPlayerCount || index < 0 || static_cast<std::size_t>(index) >= kProcessControlStatCount)
            return;
        g_playerStats[player * kProcessControlStatCount + index] = static_cast<float>(value);
    }

    void SetLocalPlayerStat(int index, std::uint16_t value)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= kProcessControlStatCount)
            return;
        const float stat = static_cast<float>(value);
        g_localPlayerStats[index] = stat;
        g_internalPlayerStats[kPlayerStatOffset + index] = stat;
    }

    void UpdatePlayerStats()
    {
        ZoneScoped;
        std::memcpy(g_localPlayerStats.data(), g_internalPlayerStats + kPlayerStatOffset, kProcessControlStatCount * sizeof(float));
    }

    void ApplyPlayerStats()
    {
        std::memcpy(g_internalPlayerStats + kPlayerStatOffset, g_localPlayerStats.data(), kProcessControlStatCount * sizeof(float));
    }

    void ResetPlayerStats()
    {
        std::fill_n(g_internalPlayerStats + kPlayerStatOffset, kProcessControlStatCount, kDefaultPlayerStat);
        UpdatePlayerStats();
    }

    void ApplyPlayerStats(std::uint8_t player)
    {
        if (player >= kPlayerCount)
            return;
        std::memcpy(g_internalPlayerStats + kPlayerStatOffset, g_playerStats.data() + player * kProcessControlStatCount, kProcessControlStatCount * sizeof(float));
    }
}
