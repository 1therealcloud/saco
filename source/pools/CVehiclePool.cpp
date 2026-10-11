#include <stdafx.h>

#include "CVehiclePool.h"
#include "net/CNetGame.h"

#include "game/Scripting.h"
#include "game/CGame.h"
#include "game/render/CLicensePlate.h"
#include "main.h"

#include <cstring>
#include <new>

namespace
{

    constexpr int TRAIN_STREAK_LOCO = 537;
    constexpr int TRAIN_FREIGHT_LOCO = 538;
    constexpr int TRAIN_STREAK = 569;
    constexpr int TRAIN_FREIGHT = 570;

    constexpr float NEAREST_LOCAL_MAX_DISTANCE = 10000.0f;
    constexpr float NEAREST_POINT_MAX_DISTANCE = 20000.0f;
    constexpr float SUNK_NOTIFICATION_DISTANCE = 200.0f;
    constexpr DWORD DESTROYED_VEHICLE_LIFETIME = 15000;
    constexpr DWORD UNOCCUPIED_SYNC_INTERVAL = 100;
    constexpr int MAX_UNOCCUPIED_SYNCS_PER_TICK = 3;

    bool IsModelAvailable(int model)
    {
        // CGame::IsModelAvailable in R5 returns true for values outside [0, 20000]
        // and uses GTA opcode 0248 for normal model ids.
        if (model < 0 || model > 20000)
            return true;

        // Опкод 0x0248 (has_model_loaded): перевіряє, чи завантажена модель у пам'ять
        constexpr scm::OpcodeInfo opHasModelLoaded{0x0248, "i"};
        return scm::ProcessCommand(&opHasModelLoaded, model) != 0;
    }
}

CVehiclePool::CVehiclePool()
{
    std::memset(this, 0, sizeof(*this));
    // samp.dll + 0x1EA10. R5 intentionally initializes only these arrays here.
    for (int id = 0; id < MAX_VEHICLES; ++id)
    {
        m_bNotEmpty[id] = FALSE;
        m_pObject[id] = nullptr;
        m_pGameObject[id] = nullptr;
        m_lastUndrivenProcessTick[id] = ::GetTickCount();
    }

    std::memset(m_waitingList.m_bNotEmpty, 0, sizeof(m_waitingList.m_bNotEmpty));
    m_nCount = 0;
    m_bNeedsToInitializeLicensePlates = TRUE;
}

CVehiclePool::~CVehiclePool()
{
    // samp.dll + 0x1F060
    for (ID id = 0; id < MAX_VEHICLES; ++id)
        Delete(id);
}

void CVehiclePool::UpdateCount()
{
    // samp.dll + 0x1E9B0
    int last = 0;
    for (int id = 0; id < MAX_VEHICLES; ++id)
    {
        if (m_bNotEmpty[id])
            last = id;
    }
    m_nCount = last;
}

BOOL CVehiclePool::Delete(ID id)
{
    // samp.dll + 0x1EA80
    if (id >= MAX_VEHICLES || !m_bNotEmpty[id] || !m_pObject[id])
        return FALSE;

    m_pObject[id]->ShutdownLicensePlate();
    m_bNotEmpty[id] = FALSE;

    delete m_pObject[id];
    m_pObject[id] = nullptr;
    m_pGameObject[id] = nullptr;

    UpdateCount();
    return TRUE;
}

void CVehiclePool::ChangeInterior(ID id, int interior)
{
    // samp.dll + 0x1EB00
    if (id < MAX_VEHICLES && m_bNotEmpty[id] && m_pObject[id])
        m_pObject[id]->ChangeInterior(interior);
}

void CVehiclePool::SetParams(ID id, bool objective, bool doorsLocked)
{
    // samp.dll + 0x1EB30
    if (id >= MAX_VEHICLES || !m_bNotEmpty[id] || !m_pObject[id] || !m_bIsActive[id])
        return;

    CVehicle* vehicle = m_pObject[id];
    if (objective)
    {
        vehicle->m_bIsObjective = true;
        vehicle->m_bObjectiveBlipCreated = FALSE;
    }

    vehicle->Lock(doorsLocked ? TRUE : FALSE);
}

CVehiclePool::ID CVehiclePool::Find(void* gameObject)
{
    // samp.dll + 0x1EB90. Vehicle id 0 is deliberately skipped by R5 here.
    for (int id = 1; id <= m_nCount; ++id)
    {
        if (m_pGameObject[id] == gameObject)
            return static_cast<ID>(id);
    }
    return INVALID_ID;
}

CVehiclePool::GTAREF CVehiclePool::GetRef(int id)
{
    // samp.dll + 0x1EBC0
    if (id < 0 || id >= MAX_VEHICLES)
        return INVALID_REF;
    return GetRef(m_pGameObject[id]);
}

CVehiclePool::GTAREF CVehiclePool::GetRef(void* gameObject)
{
    // samp.dll + 0x1EBE0
    if (!gameObject)
        return INVALID_REF;

    auto* pool = *reinterpret_cast<void**>(0xB74494);
    return pool ? static_cast<GTAREF>(reinterpret_cast<std::uint32_t(__thiscall*)(void*, void*)>(0x424160)(pool, gameObject)) : static_cast<GTAREF>(0xFFFFFFFFu);
}

CVehiclePool::ID CVehiclePool::GetNearest()
{
    // samp.dll + 0x1EC00
    ID result = INVALID_ID;
    float nearestDistance = NEAREST_LOCAL_MAX_DISTANCE;

    for (int id = 0; id <= m_nCount && id < MAX_VEHICLES; ++id)
    {
        if (!m_bNotEmpty[id] || !m_bIsActive[id] || !m_pObject[id])
            continue;

        const float distance = m_pObject[id]->GetDistanceToLocalPlayer();
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            result = static_cast<ID>(id);
        }
    }

    return result;
}

CVehiclePool::ID CVehiclePool::GetNearest(CVector point)
{
    // samp.dll + 0x1EC70
    ID result = INVALID_ID;
    float nearestDistance = NEAREST_POINT_MAX_DISTANCE;

    for (int id = 0; id <= m_nCount && id < MAX_VEHICLES; ++id)
    {
        if (!m_bNotEmpty[id] || !m_bIsActive[id] || !m_pObject[id])
            continue;

        const float distance = m_pObject[id]->GetDistanceToPoint(point);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            result = static_cast<ID>(id);
        }
    }

    return result;
}

void CVehiclePool::AddToWaitingList(const VehicleInfo* info)
{
    // samp.dll + 0x1ED10
    if (!info)
        return;

    int slot = 0;
    while (slot < WAITING_LIST_SIZE && m_waitingList.m_bNotEmpty[slot])
        ++slot;

    if (slot == WAITING_LIST_SIZE)
    {
        Log("All vehicle waiting slots are consumed!");
        return;
    }

    m_waitingList.m_bNotEmpty[slot] = TRUE;
    std::memcpy(&m_waitingList.m_entry[slot], info, sizeof(VehicleInfo));
}

void CVehiclePool::ConstructLicensePlates()
{
    // samp.dll + 0x1ED70
    if (!m_bNeedsToInitializeLicensePlates)
        return;

    if (g_licensePlate && !g_licensePlate->m_pDefaultPlate)
        g_licensePlate->m_pDefaultPlate = g_licensePlate->Create(CLicensePlate::DEFAULT_PLATE_TEXT);

    for (int id = 0; id <= m_nCount && id < MAX_VEHICLES; ++id)
    {
        if (m_bNotEmpty[id] && m_pObject[id])
            m_pObject[id]->ConstructLicensePlate();
    }
}

void CVehiclePool::ShutdownLicensePlates()
{
    // samp.dll + 0x1EDE0
    for (int id = 0; id <= m_nCount && id < MAX_VEHICLES; ++id)
    {
        if (m_bNotEmpty[id] && m_pObject[id])
            m_pObject[id]->ShutdownLicensePlate();
    }
}

BOOL CVehiclePool::Create(VehicleInfo* info)
{
    // samp.dll + 0x1F080
    if (!info || info->m_nId >= MAX_VEHICLES)
        return FALSE;

    const ID id = info->m_nId;
    if (m_pObject[id])
    {
        Log("Warning: vehicle %u was not deleted", static_cast<unsigned int>(id));
        Delete(id);
    }

    auto* game = g_game.get();
    const BOOL keepModelLoaded = game && info->m_nType >= 400 && info->m_nType < 612 && game->m_aKeepLoadedVehicleModels[info->m_nType - 400];

    CVehicle* vehicle = new (std::nothrow) CVehicle(info->m_nType, info->m_position, info->m_fRotation, keepModelLoaded, info->m_bHasSiren ? TRUE : FALSE);

    if (!vehicle)
    {
        m_pObject[id] = nullptr;
        return FALSE;
    }

    if (!vehicle->GetNativeVehicleHandle())
    {
        // CGame::CreateVehicle in R5 returns nullptr here without exposing the
        // allocated wrapper to the pool. Keep that pool-visible behavior.
        m_pObject[id] = nullptr;
        delete vehicle;
        return FALSE;
    }

    m_pObject[id] = vehicle;
    vehicle->SetLicensePlateText(CLicensePlate::DEFAULT_PLATE_TEXT);

    if (info->m_nPrimaryColor != 0xFF || info->m_nSecondaryColor != 0xFF)
        vehicle->SetColor(info->m_nPrimaryColor, info->m_nSecondaryColor);

    vehicle->SetHealth(info->m_fHealth);

    m_pGameObject[id] = vehicle->GetNativeVehicleHandle();
    m_bNotEmpty[id] = TRUE;
    m_spawnedAt[id] = info->m_position;

    if (static_cast<unsigned char>(info->m_nInterior) > 0)
        ChangeInterior(id, static_cast<unsigned char>(info->m_nInterior));

    if (info->m_nPanelDamageStatus || info->m_nDoorDamageStatus || info->m_nLightDamageStatus)
    {
        vehicle->UpdateDamage(info->m_nPanelDamageStatus, info->m_nDoorDamageStatus, info->m_nLightDamageStatus);
    }

    vehicle->Lock(info->m_bDoorsLocked ? TRUE : FALSE);

    m_bIsActive[id] = TRUE;
    m_bIsDestroyed[id] = FALSE;
    m_tickWhenDestroyed[id] = 0;

    UpdateCount();
    return TRUE;
}

void CVehiclePool::SendDestroyNotification(ID id)
{
    // samp.dll + 0x1F230 is RakNet/local-player code. The project currently has
    // no completed CNetGame/CLocalPlayer path, so only this dependency is bridged.
    // network destroy notification is sent by CNetGame when this path is wired.
}

void CVehiclePool::ProcessWaitingList()
{
    // samp.dll + 0x1F300
    for (int slot = 0; slot < WAITING_LIST_SIZE; ++slot)
    {
        if (!m_waitingList.m_bNotEmpty[slot])
            continue;

        VehicleInfo& info = m_waitingList.m_entry[slot];
        if (!IsModelAvailable(info.m_nType))
            continue;

        Create(&info);

        if (info.m_nType == TRAIN_STREAK_LOCO)
        {
            ++info.m_nId;
            info.m_nType = TRAIN_STREAK;
            Create(&info);
            ++info.m_nId;
            Create(&info);
            ++info.m_nId;
            Create(&info);
        }

        if (info.m_nType == TRAIN_FREIGHT_LOCO)
        {
            ++info.m_nId;
            info.m_nType = TRAIN_FREIGHT;
            Create(&info);
            ++info.m_nId;
            Create(&info);
            ++info.m_nId;
            Create(&info);
        }

        m_waitingList.m_bNotEmpty[slot] = FALSE;
    }
}

void CVehiclePool::Process()
{
    // samp.dll + 0x1F3C0
    const DWORD now = ::GetTickCount();
    int unoccupiedSyncsSent = 0;
    auto* netGame = g_netGame.get();
    auto* playerPool = netGame ? netGame->GetPlayerPool() : nullptr;
    auto* localPlayer = playerPool ? playerPool->GetLocalPlayer() : nullptr;

    for (int id = 0; id <= m_nCount && id < MAX_VEHICLES; ++id)
    {
        if (!m_bNotEmpty[id] || !m_bIsActive[id] || !m_pObject[id])
            continue;

        CVehicle* vehicle = m_pObject[id];

        if (!vehicle->DoesExist())
        {
            Delete(static_cast<ID>(id));
            continue;
        }

        if (m_bIsDestroyed[id] && now - m_tickWhenDestroyed[id] > DESTROYED_VEHICLE_LIFETIME)
        {
            Delete(static_cast<ID>(id));
            continue;
        }

        vehicle->SetInvulnerable(vehicle->DriverIsPlayerPed() ? FALSE : TRUE);

        if (vehicle->GetHealth() == 0.0f)
        {
            SendDestroyNotification(static_cast<ID>(id));
            if (!m_bIsDestroyed[id])
            {
                m_bIsDestroyed[id] = TRUE;
                m_tickWhenDestroyed[id] = ::GetTickCount();
            }
            continue;
        }

        const float distance = vehicle->GetDistanceToLocalPlayer();
        if (vehicle->GetSubtype() != 4 && distance < SUNK_NOTIFICATION_DISTANCE && vehicle->IsSunk())
        {
            SendDestroyNotification(static_cast<ID>(id));
            continue;
        }

        if (!vehicle->IsTrainPart() && (!vehicle->IsTrailer() || !vehicle->GetTractor()) && unoccupiedSyncsSent < MAX_UNOCCUPIED_SYNCS_PER_TICK &&
            now - m_lastUndrivenProcessTick[id] > UNOCCUPIED_SYNC_INTERVAL && localPlayer && localPlayer->ProcessUnoccupiedSync(static_cast<ID>(id), vehicle))
        {
            m_lastUndrivenProcessTick[id] = ::GetTickCount();
            ++unoccupiedSyncsSent;
        }

        if (!netGame || !netGame->m_pSettings || !netGame->m_pSettings->m_bManualVehicleEngineAndLight)
        {
            if (vehicle->m_bEngineOn == -1)
                vehicle->EnableEngine(vehicle->HasDriver() ? TRUE : FALSE);
            else if (vehicle->m_bEngineOn == 0)
                vehicle->EnableEngine(FALSE);
            else if (vehicle->m_bEngineOn == 1)
                vehicle->EnableEngine(TRUE);

            if (vehicle->m_bIsLightsOn == 0)
                vehicle->EnableLights(false);
            else if (vehicle->m_bIsLightsOn == 1)
                vehicle->EnableLights(true);
        }
        else
        {
            vehicle->EnableEngine(vehicle->m_bEngineOn > 0 ? TRUE : FALSE);
            vehicle->EnableLights(vehicle->m_bIsLightsOn > 0);
        }

        if (vehicle->GetNativeVehicleHandle() != m_pGameObject[id])
            m_pGameObject[id] = vehicle->GetNativeVehicleHandle();

        vehicle->UpdateColor();
        vehicle->ProcessMarkers();
    }
}

CVehicle* CVehiclePool::Get(ID id)
{
    // samp.dll + 0x1120
    return id < MAX_VEHICLES && m_bNotEmpty[id] ? m_pObject[id] : nullptr;
}

BOOL CVehiclePool::DoesExist(ID id)
{
    // samp.dll + 0x1150
    return id < MAX_VEHICLES ? m_bNotEmpty[id] : FALSE;
}
