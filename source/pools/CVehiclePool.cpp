#include <stdafx.h>
#include "game/Profiler.h"

#include "CVehiclePool.h"
#include "net/CNetGame.h"

#include "game/Scripting.h"
#include "game/CGame.h"
#include "game/render/CLicensePlate.h"
#include "main.h"

#include <new>
#include <optional>
#include <cmath>

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

    struct SpawnAppearance
    {
        std::uint8_t mods[14]{};
        unsigned char paintjob = 0; // rpc 164: 0 = none, 1..4 = gta paintjobs 0..3
        std::optional<unsigned char> pendingPaintjob;
        std::optional<std::pair<std::uint32_t, std::uint32_t>> bodyColors;
        char plate[CVehicle::MAX_LICENSE_PLATE_TEXT + 1]{};
        bool hasPlate = false;
        std::optional<float> health;
        std::optional<CVector> position;
        std::optional<float> heading;
        std::optional<int> interior;
        std::optional<int> panels;
        std::optional<int> doors;
        std::optional<char> lights;
        std::optional<char> pendingTires;
        std::optional<std::array<std::uint8_t, 16>> params;
        std::optional<bool> effectiveSiren;
        std::optional<std::pair<bool, bool>> simpleParams;
        std::optional<std::pair<unsigned char, unsigned char>> colors;
        std::vector<std::pair<unsigned short, bool>> componentUpdates;
    };
    std::unordered_map<CVehiclePool*, std::unordered_map<CVehiclePool::ID, SpawnAppearance>> g_spawnAppearances;
    std::unordered_map<CVehiclePool*, std::unordered_map<CVehiclePool::ID, CVehiclePool::ID>> g_trailerQueue;
    std::unordered_map<CVehiclePool*, std::unordered_map<CVehiclePool::ID, std::array<CVehiclePool::ID, 3>>> g_trainConsists;
    std::unordered_map<CVehiclePool*, std::array<DWORD, CVehiclePool::WAITING_LIST_SIZE>> g_failedSpawnAttempts;

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
    : m_nCount(0), m_waitingList{}, m_pObject{}, m_bNotEmpty{}, m_pGameObject{}, pad_6ef4{}, m_nLastUndrivenId{}, m_lastUndrivenProcessTick{}, m_bIsActive{}, m_bIsDestroyed{},
      m_tickWhenDestroyed{}, m_spawnedAt{}, m_bNeedsToInitializeLicensePlates(TRUE)
{
    const DWORD now = ::GetTickCount();
    for (std::size_t i = 0; i < MAX_VEHICLES; ++i)
    {
        m_nLastUndrivenId[i] = INVALID_ID;
        m_lastUndrivenProcessTick[i] = now;
    }
}

CVehiclePool::~CVehiclePool()
{
    // samp.dll + 0x1F060
    for (ID id = 0; id < MAX_VEHICLES; ++id)
        Delete(id);
    g_spawnAppearances.erase(this);
    g_trailerQueue.erase(this);
    g_trainConsists.erase(this);
    g_failedSpawnAttempts.erase(this);
}

void CVehiclePool::UpdateCount()
{
    // samp.dll + 0x1E9B0
    int last = 0;
    for (int id = MAX_VEHICLES - 1; id > 0; --id)
    {
        if (m_bNotEmpty[id])
        {
            last = id;
            break;
        }
    }
    m_nCount = last;
}

BOOL CVehiclePool::Delete(ID id)
{
    // samp.dll + 0x1EA80
    if (id >= MAX_VEHICLES)
        return FALSE;

    m_lastUnoccupiedCheck[id] = 0;
    std::array<ID, 3> trainWagons{INVALID_ID, INVALID_ID, INVALID_ID};
    bool hadConsist = false;
    if (auto trains = g_trainConsists.find(this); trains != g_trainConsists.end())
    {
        if (auto consist = trains->second.find(id); consist != trains->second.end())
        {
            trainWagons = consist->second;
            hadConsist = true;
            trains->second.erase(consist);
        }
        else
        {
            for (auto& [locomotive, wagons] : trains->second)
                for (ID& wagon : wagons)
                    if (wagon == id)
                        wagon = INVALID_ID;
        }
    }
    const auto deleteTrainWagons = [&]() {
        for (ID wagon : trainWagons)
            if (wagon != INVALID_ID)
                Delete(wagon);
    };

    if (const auto appearance = g_spawnAppearances.find(this); appearance != g_spawnAppearances.end())
        appearance->second.erase(id);
    if (auto queued = g_trailerQueue.find(this); queued != g_trailerQueue.end())
    {
        queued->second.erase(id);
        for (auto it = queued->second.begin(); it != queued->second.end();)
        {
            if (it->second == id)
                it = queued->second.erase(it);
            else
                ++it;
        }
    }

    bool pending = false;
    for (int slot = 0; slot < WAITING_LIST_SIZE; ++slot)
    {
        if (m_waitingList.m_bNotEmpty[slot] && m_waitingList.m_entry[slot].m_nId == id)
        {
            m_waitingList.m_bNotEmpty[slot] = FALSE;
            if (auto attempts = g_failedSpawnAttempts.find(this); attempts != g_failedSpawnAttempts.end())
                attempts->second[slot] = 0;
            pending = true;
        }
    }
    if (!m_pObject[id])
    {
        if (m_bNotEmpty[id])
        {
            m_bNotEmpty[id] = FALSE;
            m_pGameObject[id] = nullptr;
            m_bIsActive[id] = FALSE;
            m_bIsDestroyed[id] = FALSE;
            m_tickWhenDestroyed[id] = 0;
            m_nLastUndrivenId[id] = INVALID_ID;
            m_lastUndrivenProcessTick[id] = ::GetTickCount();
            UpdateCount();
        }
        deleteTrainWagons();
        return pending || hadConsist ? TRUE : FALSE;
    }

    CVehicle* vehicle = m_pObject[id];

    // detach before deleting the trailer so tractors cannot retain stale wrappers
    const bool vehicleExists = vehicle->DoesExist() != FALSE;
    for (int otherId = 0; otherId <= m_nCount && otherId < MAX_VEHICLES; ++otherId)
    {
        CVehicle* tractor = m_pObject[otherId];
        if (tractor && tractor != vehicle && tractor->m_pTrailer == vehicle)
        {
            if (vehicleExists && tractor->DoesExist())
                tractor->DetachTrailer();
            tractor->SetTrailer(nullptr);
        }
    }

    // deleting the locomotive destroys native wagons too; move their occupants first
    if (hadConsist)
        for (const ID wagon : trainWagons)
            if (wagon < MAX_VEHICLES && m_pObject[wagon] && m_pObject[wagon]->DoesExist())
                m_pObject[wagon]->RemovePassengers();

    vehicle->ShutdownLicensePlate();
    m_bNotEmpty[id] = FALSE;

    delete vehicle;
    m_pObject[id] = nullptr;
    m_pGameObject[id] = nullptr;
    m_bIsActive[id] = FALSE;
    m_bIsDestroyed[id] = FALSE;
    m_tickWhenDestroyed[id] = 0;
    m_nLastUndrivenId[id] = INVALID_ID;
    m_lastUndrivenProcessTick[id] = ::GetTickCount();

    UpdateCount();
    deleteTrainWagons();
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
    if (!gameObject)
        return INVALID_ID;
    for (int id = 1; id <= m_nCount && id < MAX_VEHICLES; ++id)
    {
        if (m_bNotEmpty[id] && m_pObject[id] && m_pGameObject[id] == gameObject && m_pObject[id]->GetNativeVehicleHandle() == gameObject)
            return static_cast<ID>(id);
    }
    return INVALID_ID;
}

CVehiclePool::GTAREF CVehiclePool::GetRef(int id)
{
    // samp.dll + 0x1EBC0
    if (id < 0 || id >= MAX_VEHICLES || !m_bNotEmpty[id] || !m_pObject[id])
        return INVALID_REF;
    void* native = m_pObject[id]->GetNativeVehicleHandle();
    return native && native == m_pGameObject[id] ? GetRef(native) : INVALID_REF;
}

CVehiclePool::GTAREF CVehiclePool::GetRef(void* gameObject)
{
    // samp.dll + 0x1EBE0
    if (!gameObject)
        return INVALID_REF;

    return CVehicle::GetNativeRef(gameObject);
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

void CVehiclePool::SetSpawnAppearance(ID id, const std::array<std::uint8_t, 14>& mods, unsigned char paintjob)
{
    if (id >= MAX_VEHICLES)
        return;
    auto& state = g_spawnAppearances[this][id];
    std::memcpy(state.mods, mods.data(), sizeof(state.mods));
    state.paintjob = paintjob;
}

void CVehiclePool::SetSpawnBodyColors(ID id, std::uint32_t primary, std::uint32_t secondary)
{
    if (id >= MAX_VEHICLES)
        return;
    g_spawnAppearances[this][id].bodyColors = std::make_pair(primary, secondary);
}

void CVehiclePool::SetSpawnLicensePlate(ID id, const char* text)
{
    if (!text || !IsPending(id))
        return;
    auto& state = g_spawnAppearances[this][id];
    std::strncpy(state.plate, text, CVehicle::MAX_LICENSE_PLATE_TEXT);
    state.plate[CVehicle::MAX_LICENSE_PLATE_TEXT] = '\0';
    state.hasPlate = true;
}

bool CVehiclePool::IsPending(ID id) const
{
    if (id >= MAX_VEHICLES || m_bNotEmpty[id])
        return false;
    for (int slot = 0; slot < WAITING_LIST_SIZE; ++slot)
        if (m_waitingList.m_bNotEmpty[slot] && m_waitingList.m_entry[slot].m_nId == id)
            return true;
    return false;
}

void CVehiclePool::QueueHealth(ID id, float health)
{
    if (IsPending(id) && std::isfinite(health))
        g_spawnAppearances[this][id].health = health;
}

void CVehiclePool::QueuePosition(ID id, CVector position)
{
    if (IsPending(id) && std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z))
        g_spawnAppearances[this][id].position = position;
}

void CVehiclePool::QueueRotation(ID id, float angle)
{
    if (IsPending(id) && std::isfinite(angle))
        g_spawnAppearances[this][id].heading = angle;
}

void CVehiclePool::QueueInterior(ID id, int interior)
{
    if (IsPending(id))
        g_spawnAppearances[this][id].interior = interior;
}

void CVehiclePool::QueueDamage(ID id, int panels, int doors, char lights, char tires)
{
    if (!IsPending(id))
        return;
    auto& state = g_spawnAppearances[this][id];
    state.panels = panels;
    state.doors = doors;
    state.lights = lights;
    state.pendingTires = tires;
}

void CVehiclePool::QueueTires(ID id, char tires)
{
    if (IsPending(id))
        g_spawnAppearances[this][id].pendingTires = tires;
}

void CVehiclePool::QueueParams(ID id, const std::array<std::uint8_t, 16>& params)
{
    if (!IsPending(id))
        return;
    auto& state = g_spawnAppearances[this][id];
    if (!state.params)
    {
        std::array<std::uint8_t, 16> unset{};
        unset.fill(0xFF);
        state.params = unset;
    }
    for (std::size_t i = 0; i < params.size(); ++i)
        if (params[i] <= 1)
            state.params->at(i) = params[i];

    // alarm and siren update the same flag in rpc order
    if (params[2] <= 1)
        state.effectiveSiren = params[2] != 0;
    if (params[7] <= 1)
        state.effectiveSiren = params[7] != 0;

    // latest rpc wins for shared fields
    if (state.simpleParams)
    {
        if (params[3] <= 1)
            state.simpleParams->second = params[3] != 0;
        if (params[6] <= 1)
            state.simpleParams->first = params[6] != 0;
    }
}

void CVehiclePool::QueueSimpleParams(ID id, bool objective, bool doorsLocked)
{
    if (!IsPending(id))
        return;

    auto& state = g_spawnAppearances[this][id];
    // rpc 161 only sets objective when true; false does not clear a previous rpc 24 value
    const bool hadObjective = state.simpleParams ? state.simpleParams->first : (state.params && state.params->at(6) == 1);
    state.simpleParams = std::make_pair(objective || hadObjective, doorsLocked);
    if (state.params)
    {
        state.params->at(3) = 0xFF;
        state.params->at(6) = 0xFF;
    }
}

void CVehiclePool::QueuePaintjob(ID id, unsigned char paintjob)
{
    if (IsPending(id) && paintjob <= 3)
        g_spawnAppearances[this][id].pendingPaintjob = paintjob;
}

void CVehiclePool::QueueColors(ID id, unsigned char primary, unsigned char secondary)
{
    if (IsPending(id))
        g_spawnAppearances[this][id].colors = std::make_pair(primary, secondary);
}

void CVehiclePool::QueueComponent(ID id, std::uint16_t component, bool install)
{
    if (!IsPending(id) || component < 1000 || component > 1193)
        return;
    auto& updates = g_spawnAppearances[this][id].componentUpdates;
    // preserve order when components share a tuning slot
    if (updates.size() == 256)
        updates.erase(updates.begin());
    updates.emplace_back(component, install);
}

void CVehiclePool::QueueTrailer(ID tractor, ID trailer)
{
    if (tractor >= MAX_VEHICLES || trailer >= MAX_VEHICLES || tractor == trailer)
        return;
    if ((Get(tractor) || IsPending(tractor)) && (Get(trailer) || IsPending(trailer)))
        g_trailerQueue[this][tractor] = trailer;
}

void CVehiclePool::ClearQueuedTrailer(ID tractor)
{
    if (auto it = g_trailerQueue.find(this); it != g_trailerQueue.end())
        it->second.erase(tractor);
}

void CVehiclePool::AddToWaitingList(const VehicleInfo* info)
{
    // samp.dll + 0x1ED10
    if (!info)
        return;

    int slot = 0;
    for (; slot < WAITING_LIST_SIZE; ++slot)
    {
        if (m_waitingList.m_bNotEmpty[slot] && m_waitingList.m_entry[slot].m_nId == info->m_nId)
            break;
    }
    if (slot == WAITING_LIST_SIZE)
    {
        for (slot = 0; slot < WAITING_LIST_SIZE && m_waitingList.m_bNotEmpty[slot]; ++slot)
        {
        }
    }

    if (slot == WAITING_LIST_SIZE)
    {
        Log("All vehicle waiting slots are consumed!");
        if (auto it = g_spawnAppearances.find(this); it != g_spawnAppearances.end())
            it->second.erase(info->m_nId);
        return;
    }

    m_waitingList.m_bNotEmpty[slot] = TRUE;
    m_waitingList.m_entry[slot] = *info;
    g_failedSpawnAttempts[this][slot] = 0;

    if (info->m_nType >= 400 && info->m_nType <= 611 && !IsModelAvailable(info->m_nType))
        if (g_game)
            g_game->RequestModel(info->m_nType, 2);
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
        std::optional<SpawnAppearance> pendingAppearance;
        if (const auto appearances = g_spawnAppearances.find(this); appearances != g_spawnAppearances.end())
            if (const auto entry = appearances->second.find(id); entry != appearances->second.end())
                pendingAppearance = entry->second;

        Log("Warning: vehicle %u was not deleted", static_cast<unsigned int>(id));
        Delete(id);
        if (pendingAppearance)
            g_spawnAppearances[this][id] = std::move(*pendingAppearance);
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
        // avoid exposing an invalid gta handle to the pool
        m_pObject[id] = nullptr;
        delete vehicle;
        return FALSE;
    }

    m_pObject[id] = vehicle;

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

    vehicle->SetTires(static_cast<char>(info->m_nTireDamageStatus));

    m_bIsActive[id] = TRUE;
    m_bIsDestroyed[id] = FALSE;
    m_tickWhenDestroyed[id] = 0;
    m_nLastUndrivenId[id] = INVALID_ID;
    m_lastUndrivenProcessTick[id] = ::GetTickCount();

    if (const auto appearance = g_spawnAppearances.find(this); appearance != g_spawnAppearances.end())
    {
        if (const auto entry = appearance->second.find(id); entry != appearance->second.end())
        {
            for (const auto mod : entry->second.mods)
                if (mod && mod <= 194)
                    vehicle->AddComponent(static_cast<std::uint16_t>(999 + mod));
            if (entry->second.paintjob >= 1 && entry->second.paintjob <= 4)
                vehicle->SetPaintjob(entry->second.paintjob - 1);
            if (entry->second.bodyColors && (entry->second.bodyColors->first != 0xFFFFFFFFu || entry->second.bodyColors->second != 0xFFFFFFFFu))
            {
                vehicle->SetColor(static_cast<unsigned char>(entry->second.bodyColors->first), static_cast<unsigned char>(entry->second.bodyColors->second));
            }
            if (entry->second.pendingPaintjob)
                vehicle->SetPaintjob(*entry->second.pendingPaintjob);
            if (entry->second.colors)
                vehicle->SetColor(entry->second.colors->first, entry->second.colors->second);
            for (const auto& [component, install] : entry->second.componentUpdates)
            {
                if (install)
                    vehicle->AddComponent(component);
                else
                    vehicle->RemoveComponent(component);
            }
            if (entry->second.hasPlate)
                vehicle->SetLicensePlateText(entry->second.plate);

            const auto& state = entry->second;
            if (state.position)
                vehicle->Teleport(*state.position);
            if (state.heading)
                vehicle->SetRotation(*state.heading);
            if (state.health)
                vehicle->SetHealth(*state.health);
            if (state.interior)
                vehicle->ChangeInterior(*state.interior);
            if (state.panels || state.doors || state.lights)
                vehicle->UpdateDamage(state.panels.value_or(info->m_nPanelDamageStatus), state.doors.value_or(info->m_nDoorDamageStatus),
                                      state.lights.value_or(info->m_nLightDamageStatus));
            if (state.pendingTires)
                vehicle->SetTires(*state.pendingTires);
            if (state.simpleParams)
            {
                vehicle->SetObjective(state.simpleParams->first);
                vehicle->Lock(state.simpleParams->second ? TRUE : FALSE);
            }
            if (state.params)
            {
                const auto& p = *state.params;
                if (p[0] <= 1)
                    vehicle->SetEngineOn(p[0]);
                if (p[1] <= 1)
                    vehicle->SetLightsOn(p[1]);
                if (p[2] <= 1)
                    vehicle->SetAlarmState(p[2] != 0);
                if (p[3] <= 1)
                    vehicle->Lock(p[3] != 0);
                if (p[4] <= 1)
                    vehicle->SetBonnetState(p[4] != 0);
                if (p[5] <= 1)
                    vehicle->SetBootState(p[5] != 0);
                if (p[6] <= 1)
                    vehicle->SetObjective(p[6] != 0);
                if (state.effectiveSiren)
                    vehicle->EnableSiren(*state.effectiveSiren);
                constexpr std::uint8_t windowComponents[]{10, 8, 11, 9};
                for (std::uint8_t door = 0; door < 4; ++door)
                {
                    if (p[8 + door] <= 1)
                        vehicle->SetDoorOpenState(door, p[8 + door] != 0);
                    if (p[12 + door] == 0)
                        vehicle->SetWindowOpenFlag(windowComponents[door]);
                    else if (p[12 + door] == 1)
                        vehicle->ClearWindowOpenFlag(windowComponents[door]);
                }
            }
            appearance->second.erase(entry);
        }
    }

    UpdateCount();
    return TRUE;
}

BOOL CVehiclePool::CreateWithTrainConsist(VehicleInfo info)
{
    if (info.m_nType != TRAIN_STREAK_LOCO && info.m_nType != TRAIN_FREIGHT_LOCO)
        return Create(&info);

    if (info.m_nId >= MAX_VEHICLES - 3)
        return FALSE;

    const ID locomotiveId = info.m_nId;
    std::optional<std::array<ID, 3>> oldConsist;
    if (const auto trains = g_trainConsists.find(this); trains != g_trainConsists.end())
        if (const auto entry = trains->second.find(locomotiveId); entry != trains->second.end())
            oldConsist = entry->second;

    // an existing consist may be replaced, but never overwrite another vehicle's ids
    for (int wagon = 1; wagon <= 3; ++wagon)
    {
        const ID wagonId = static_cast<ID>(locomotiveId + wagon);
        if (IsPending(wagonId))
            return FALSE;
        if (m_pObject[wagonId] || m_bNotEmpty[wagonId])
        {
            if (!oldConsist || (*oldConsist)[wagon - 1] != wagonId)
                return FALSE;
        }
    }

    std::optional<SpawnAppearance> savedAppearance;
    if (auto pool = g_spawnAppearances.find(this); pool != g_spawnAppearances.end())
        if (auto entry = pool->second.find(locomotiveId); entry != pool->second.end())
            savedAppearance = entry->second;

    if (oldConsist)
    {
        Delete(locomotiveId);
        if (savedAppearance)
            g_spawnAppearances[this][locomotiveId] = *savedAppearance;
    }

    if (!Create(&info))
        return FALSE;

    info.m_nType = info.m_nType == TRAIN_STREAK_LOCO ? TRAIN_STREAK : TRAIN_FREIGHT;
    for (int wagon = 1; wagon <= 3; ++wagon)
    {
        info.m_nId = static_cast<ID>(locomotiveId + wagon);
        if (Create(&info))
            continue;

        // remove the native train through its locomotive before releasing wagon wrappers
        Delete(locomotiveId);
        for (int created = 1; created < wagon; ++created)
            Delete(static_cast<ID>(locomotiveId + created));
        if (savedAppearance)
            g_spawnAppearances[this][locomotiveId] = std::move(*savedAppearance);
        return FALSE;
    }

    g_trainConsists[this][locomotiveId] = {static_cast<ID>(locomotiveId + 1), static_cast<ID>(locomotiveId + 2), static_cast<ID>(locomotiveId + 3)};
    return TRUE;
}

void CVehiclePool::SendDestroyNotification(ID id)
{
    if (auto* netGame = g_netGame.get())
        netGame->VehicleDestroyed(id);
}

void CVehiclePool::ProcessWaitingList()
{
    PROFILE_SCOPE(VehicleWaiting);
    // samp.dll + 0x1F300
    const DWORD now = ::GetTickCount();
    auto& attempts = g_failedSpawnAttempts[this];
    for (int slot = 0; slot < WAITING_LIST_SIZE; ++slot)
    {
        if (!m_waitingList.m_bNotEmpty[slot])
            continue;
        if (attempts[slot] && now - attempts[slot] < 250)
            continue;

        const VehicleInfo info = m_waitingList.m_entry[slot];
        if (!IsModelAvailable(info.m_nType))
        {
            if (info.m_nType >= 400 && info.m_nType <= 611 && (!attempts[slot] || now - attempts[slot] >= 1000))
            {
                if (g_game)
                    g_game->RequestModel(info.m_nType, 2);
                attempts[slot] = now;
            }
            continue;
        }

        // hide this entry while Create may call Delete, then restore on failure
        m_waitingList.m_bNotEmpty[slot] = FALSE;
        if (CreateWithTrainConsist(info))
        {
            attempts[slot] = 0;
        }
        else
        {
            m_waitingList.m_bNotEmpty[slot] = TRUE;
            attempts[slot] = now;
        }
    }
}

void CVehiclePool::Process()
{
    PROFILE_SCOPE(Vehicles);
    // samp.dll + 0x1F3C0
    const DWORD now = ::GetTickCount();
    int unoccupiedSyncsSent = 0;
    auto* netGame = g_netGame.get();
    auto* playerPool = netGame ? netGame->GetPlayerPool() : nullptr;
    auto* localPlayer = playerPool ? playerPool->GetLocalPlayer() : nullptr;

    if (auto queued = g_trailerQueue.find(this); queued != g_trailerQueue.end())
    {
        for (auto it = queued->second.begin(); it != queued->second.end();)
        {
            auto* tractor = Get(it->first);
            auto* trailer = Get(it->second);
            if (tractor && trailer && tractor != trailer && tractor->DoesExist() && trailer->DoesExist())
            {
                if (auto* attached = tractor->GetTrailer(); attached && attached != trailer)
                {
                    tractor->SetTrailer(attached);
                    tractor->DetachTrailer();
                }
                tractor->SetTrailer(trailer);
                tractor->AttachTrailer();
                it = queued->second.erase(it);
            }
            else
                ++it;
        }
    }

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
            if (!m_bIsDestroyed[id])
            {
                SendDestroyNotification(static_cast<ID>(id));
                m_bIsDestroyed[id] = TRUE;
                m_tickWhenDestroyed[id] = now;
            }
            continue;
        }

        const float distance = vehicle->GetDistanceToLocalPlayer();
        if (vehicle->GetSubtype() != 4 && distance < SUNK_NOTIFICATION_DISTANCE && vehicle->IsSunk())
        {
            if (!m_bIsDestroyed[id])
            {
                SendDestroyNotification(static_cast<ID>(id));
                m_bIsDestroyed[id] = TRUE;
                m_tickWhenDestroyed[id] = now;
            }
            continue;
        }

        if (!vehicle->IsTrainPart() && (!vehicle->IsTrailer() || !vehicle->GetTractor()) && unoccupiedSyncsSent < MAX_UNOCCUPIED_SYNCS_PER_TICK &&
            now - m_lastUndrivenProcessTick[id] > UNOCCUPIED_SYNC_INTERVAL && now - m_lastUnoccupiedCheck[id] > UNOCCUPIED_SYNC_INTERVAL && localPlayer)
        {
            // Throttle eligibility separately from sends: a rejection must not rescan all players every frame.
            m_lastUnoccupiedCheck[id] = now;
            PROFILE_SCOPE(VehicleUnoccupied);
            if (localPlayer->ProcessUnoccupiedSync(static_cast<ID>(id), vehicle, distance))
            {
                m_lastUndrivenProcessTick[id] = now;
                ++unoccupiedSyncsSent;
            }
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

        const auto nativeVehicle = vehicle->GetNativeVehicleHandle();
        if (nativeVehicle != m_pGameObject[id])
            m_pGameObject[id] = nativeVehicle;

        vehicle->UpdateColor();
        vehicle->ProcessMarkers(distance);
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
