#include <stdafx.h>

#include "CNetStats.h"

#include "CNetGame.h"
#include "game/CGame.h"
#include "main.h"
#include "players/CLocalPlayer.h"
#include "pools/CObjectPool.h"
#include "pools/CPlayerPool.h"
#include "pools/CVehiclePool.h"
#include "render/CFonts.h"

#include <RakClientInterface.h>
#include <RakNetStatistics.h>

#include <Psapi.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "Psapi.lib")

namespace
{
    constexpr D3DCOLOR kTitleColor = 0xFF8888EEu;
    constexpr D3DCOLOR kTextColor = 0xFFFFFFFFu;
    constexpr float kBytesToKilobytes = 1.0f / 1024.0f;
    constexpr int kFirstVehicleModel = 400;
    constexpr int kVehicleModelCount = 212;

    struct VehicleStats
    {
        unsigned int vehicles{};
        unsigned int models{};
    };

    VehicleStats GetVehicleStats(CNetGame* netGame)
    {
        VehicleStats result{};
        if (!netGame || !netGame->m_pPools)
            return result;

        auto* vehiclePool = netGame->m_pPools->m_pVehicle;
        if (!vehiclePool)
            return result;

        std::array<bool, kVehicleModelCount> usedModels{};
        for (int id = 0; id < CVehiclePool::MAX_VEHICLES; ++id)
        {
            if (!vehiclePool->m_bNotEmpty[id])
                continue;

            auto* vehicle = vehiclePool->m_pObject[id];
            if (!vehicle)
                continue;

            ++result.vehicles;

            const int model = vehicle->GetModelIndex();
            if (model >= kFirstVehicleModel && model < kFirstVehicleModel + kVehicleModelCount)
                usedModels[static_cast<std::size_t>(model - kFirstVehicleModel)] = true;
        }

        for (const bool used : usedModels)
        {
            if (used)
                ++result.models;
        }

        return result;
    }

    unsigned int GetLoadedVehicleModelCount()
    {
        unsigned int count = 0;
        for (std::uintptr_t entry = 0x8E6C10; entry != 0x8E7C8C; entry += 0x14)
        {
            if (*reinterpret_cast<void**>(entry))
                ++count;
        }
        return count;
    }

    unsigned int GetRwObjectCount()
    {
        return *reinterpret_cast<const unsigned int*>(0xB71804);
    }

    unsigned int GetFreeIdeCount()
    {
        unsigned int count = 0;
        for (std::uintptr_t entry = 0xA9C068; entry != 0xAAE948; entry += sizeof(void*))
        {
            if (!*reinterpret_cast<void**>(entry))
                ++count;
        }
        return count;
    }

    unsigned int GetSystemMemoryMb()
    {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (!::GlobalMemoryStatusEx(&status))
            return 0;
        return static_cast<unsigned int>(status.ullTotalPhys >> 20);
    }

    unsigned int GetPedSlotsUsed(CNetGame* netGame)
    {
        if (!netGame || !netGame->m_pPools || !netGame->m_pPools->m_pPlayer)
            return 0;

        auto* playerPool = netGame->m_pPools->m_pPlayer;
        unsigned int count = static_cast<unsigned int>(std::max(playerPool->GetCount(TRUE), 0));
        if (auto* localPlayer = playerPool->GetLocalPlayer(); localPlayer && localPlayer->m_pPed)
            ++count;
        return count;
    }

    unsigned int GetObjectSlotsUsed(CNetGame* netGame)
    {
        if (!netGame || !netGame->m_pPools || !netGame->m_pPools->m_pObject)
            return 0;

        return static_cast<unsigned int>(std::max(netGame->m_pPools->m_pObject->GetCount(), 0));
    }

    unsigned int GetCurrentSendRate(CNetGame* netGame, bool& inVehicle)
    {
        inVehicle = false;
        if (!netGame || !netGame->m_pPools || !netGame->m_pPools->m_pPlayer)
            return 0;

        auto* localPlayer = netGame->m_pPools->m_pPlayer->GetLocalPlayer();
        if (!localPlayer)
            return 0;

        inVehicle = localPlayer->m_nCurrentVehicle != CLocalPlayer::INVALID_ID;
        return static_cast<unsigned int>(std::max(inVehicle ? localPlayer->GetIncarSendRate() : localPlayer->GetOnfootSendRate(), 0));
    }

    void Append(char* destination, std::size_t capacity, const char* source)
    {
        if (!destination || capacity == 0 || !source)
            return;

        const std::size_t length = std::strlen(destination);
        if (length >= capacity - 1)
            return;

        strncat_s(destination, capacity, source, _TRUNCATE);
    }
}

CNetStats::CNetStats(IDirect3DDevice9* device)
    : m_dwLastTotalBytesSent(0), m_dwLastTotalBytesRecv(0), m_dwLastUpdateTick(::GetTickCount()), m_dwBPSUpload(0), m_dwBPSDownload(0), m_pDevice(device)
{}

void CNetStats::Draw()
{
    auto* netGame = g_netGame.get();
    if (!netGame || !netGame->m_pRakClient || !netGame->m_pRakClient->IsConnected() || !g_fonts || !m_pDevice)
        return;

    auto* statistics = netGame->m_pRakClient->GetStatistics();
    if (!statistics)
        return;

    const std::uint32_t now = ::GetTickCount();
    if (now - m_dwLastUpdateTick > 1000)
    {
        m_dwLastUpdateTick = now;

        const auto totalBytesRecv = static_cast<std::uint32_t>(statistics->bitsReceived >> 3);
        m_dwBPSDownload = totalBytesRecv - m_dwLastTotalBytesRecv;
        m_dwLastTotalBytesRecv = totalBytesRecv;

        const auto totalBytesSent = static_cast<std::uint32_t>(statistics->totalBitsSent >> 3);
        m_dwBPSUpload = totalBytesSent - m_dwLastTotalBytesSent;
        m_dwLastTotalBytesSent = totalBytesSent;
    }

    const float downloadRate = static_cast<float>(m_dwBPSDownload) * kBytesToKilobytes;
    const float uploadRate = static_cast<float>(m_dwBPSUpload) * kBytesToKilobytes;
    const VehicleStats vehicleStats = GetVehicleStats(netGame);
    const unsigned int vehicleModelsLoaded = GetLoadedVehicleModelCount();
    const unsigned int rwObjects = GetRwObjectCount();
    const unsigned int pedSlotsUsed = GetPedSlotsUsed(netGame);
    const unsigned int objectSlotsUsed = GetObjectSlotsUsed(netGame);
    const unsigned int shotCompMode = CNetGame::RefLagCompMode();
    const unsigned int freeIdes = GetFreeIdeCount();

    bool inVehicle = false;
    const unsigned int sendRate = GetCurrentSendRate(netGame, inVehicle);

    char text[8192]{};
    std::snprintf(text, sizeof(text),
                  "Download Rate: %.2f KB/s\n"
                  "Upload Rate: %.2f KB/s\n"
                  "Vehicles: %u\n"
                  "Vehicle Models: %u\n"
                  "Vehicle Models Loaded: %u\n"
                  "RWObjects: %u\n"
                  "PedSlotsUsed: %u\n"
                  "ObjectSlotsUsed: %u\n"
                  "ShotCompMode: %u\n"
                  "FreeIDEs: %u\n"
                  "%s Send Rate: %u\n",
                  downloadRate, uploadRate, vehicleStats.vehicles, vehicleStats.models, vehicleModelsLoaded, rwObjects, pedSlotsUsed, objectSlotsUsed, shotCompMode, freeIdes,
                  inVehicle ? "InCar" : "OnFoot", sendRate);

    if (auto* game = g_game.get())
    {
        char memory[192]{};
        const unsigned int used = static_cast<unsigned int>(std::max(game->GetUsedMemory(), 0)) >> 20;
        const unsigned int streaming = static_cast<unsigned int>(std::max(game->GetStreamingMemory(), 0)) >> 20;
        const unsigned int system = GetSystemMemoryMb();
        std::snprintf(memory, sizeof(memory), "Streaming Mem: %uMB Total: %uMB System: %uMB\n", used, streaming, system);
        Append(text, sizeof(text), memory);
    }

    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), &counters, sizeof(counters)))
    {
        char processMemory[160]{};
        std::snprintf(processMemory, sizeof(processMemory), "Process Mem: %uKB Working Set: %uKB\n", static_cast<unsigned int>(counters.PagefileUsage >> 10),
                      static_cast<unsigned int>(counters.WorkingSetSize >> 10));
        Append(text, sizeof(text), processMemory);
    }

    char rakNetText[4096]{};
    StatisticsToString(statistics, rakNetText, 1);
    Append(text, sizeof(text), rakNetText);

    D3DVIEWPORT9 viewport{};
    if (FAILED(m_pDevice->GetViewport(&viewport)))
        return;

    const LONG left = static_cast<LONG>(viewport.X) + 10;
    const LONG top = static_cast<LONG>(viewport.Y) + 10;
    const LONG right = static_cast<LONG>(viewport.X + viewport.Width) - 10;
    const LONG bottom = static_cast<LONG>(viewport.Y + viewport.Height) - 10;

    RECT titleRect{left, top, right, bottom};
    g_fonts->DrawText("Client Network Stats", titleRect, kTitleColor, true);

    RECT textRect{left, top + g_fonts->GetCharHeight(), right, bottom};
    g_fonts->DrawText(text, textRect, kTextColor, true);
}
