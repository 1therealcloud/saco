/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>

#include "game/entities/CVehicle.h"
#include "utils/math/CVector.h"

class CVehiclePool
{
public:
    using ID = std::uint16_t;
    using GTAREF = int;
    using TICK = std::uint32_t;
    using NUMBER = std::uint8_t;

    enum
    {
        MAX_VEHICLES = 2000,
        WAITING_LIST_SIZE = MAX_VEHICLES
    };

    static constexpr ID INVALID_ID = 0xFFFF;
    static constexpr GTAREF INVALID_REF = 0xFFFF;

    struct VehicleInfo
    {
        ID m_nId;
        int m_nType;
        CVector m_position;
        float m_fRotation;
        NUMBER m_nPrimaryColor;
        NUMBER m_nSecondaryColor;
        float m_fHealth;
        char m_nInterior;
        int m_nDoorDamageStatus;
        int m_nPanelDamageStatus;
        char m_nLightDamageStatus;
        NUMBER m_nTireDamageStatus;
        bool m_bHasSiren;
    };

    struct WaitingList
    {
        VehicleInfo m_entry[WAITING_LIST_SIZE];
        BOOL m_bNotEmpty[WAITING_LIST_SIZE];
    };

public:
    int m_nCount;
    WaitingList m_waitingList;
    CVehicle* m_pObject[MAX_VEHICLES];
    BOOL m_bNotEmpty[MAX_VEHICLES];
    void* m_pGameObject[MAX_VEHICLES]; // GTA CVehicle*
    unsigned int pad_6ef4[MAX_VEHICLES];
    ID m_nLastUndrivenId[MAX_VEHICLES];
    TICK m_lastUndrivenProcessTick[MAX_VEHICLES];
    BOOL m_bIsActive[MAX_VEHICLES];
    BOOL m_bIsDestroyed[MAX_VEHICLES];
    TICK m_tickWhenDestroyed[MAX_VEHICLES];
    CVector m_spawnedAt[MAX_VEHICLES];
    BOOL m_bNeedsToInitializeLicensePlates;

public:
    CVehiclePool();
    ~CVehiclePool();

    CVehiclePool(const CVehiclePool&) = delete;
    CVehiclePool& operator=(const CVehiclePool&) = delete;

    void UpdateCount();
    BOOL Delete(ID id);
    void ChangeInterior(ID id, int interior);
    void SetParams(ID id, bool objective, bool doorsLocked);
    ID Find(void* gameObject);
    GTAREF GetRef(int id);
    GTAREF GetRef(void* gameObject);
    ID GetNearest();
    ID GetNearest(CVector point);
    void SetSpawnAppearance(ID id, const std::array<std::uint8_t, 14>& mods, unsigned char paintjob);
    void SetSpawnLicensePlate(ID id, const char* text);
    void SetSpawnBodyColors(ID id, std::uint32_t primary, std::uint32_t secondary);
    bool IsPending(ID id) const;
    void QueueHealth(ID id, float health);
    void QueuePosition(ID id, CVector position);
    void QueueRotation(ID id, float angle);
    void QueueInterior(ID id, int interior);
    void QueueDamage(ID id, int panels, int doors, char lights, char tires);
    void QueueTires(ID id, char tires);
    void QueueParams(ID id, const std::array<std::uint8_t, 16>& params);
    void QueueSimpleParams(ID id, bool objective, bool doorsLocked);
    void QueuePaintjob(ID id, unsigned char paintjob);
    void QueueColors(ID id, unsigned char primary, unsigned char secondary);
    void QueueComponent(ID id, std::uint16_t component, bool install);
    void QueuePlayerWarp(ID id, std::uint8_t seat);
    void ClearPlayerWarp();
    void QueueWarpVelocity(CVector velocity, bool turn);
    void QueueTrailer(ID tractor, ID trailer);
    void ClearQueuedTrailer(ID tractor);
    void AddToWaitingList(const VehicleInfo* info);
    void ConstructLicensePlates();
    void ShutdownLicensePlates();
    BOOL Create(VehicleInfo* info);
    BOOL CreateWithTrainConsist(VehicleInfo info);
    void SendDestroyNotification(ID id);
    void ProcessWaitingList();
    void Process();
    CVehicle* Get(ID id);
    BOOL DoesExist(ID id);

private:
    std::array<TICK, MAX_VEHICLES> m_lastUnoccupiedCheck{};
};
