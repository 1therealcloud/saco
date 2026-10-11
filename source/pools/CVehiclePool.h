#pragma once

#include <Windows.h>

#include <cstddef>
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
        WAITING_LIST_SIZE = 100
    };

    static constexpr ID INVALID_ID = 0xFFFF;
    static constexpr GTAREF INVALID_REF = 0xFFFF;

    struct VehicleInfo
    {
        ID m_nId;                  // 0x00
        int m_nType;               // 0x02
        CVector m_position;        // 0x06
        float m_fRotation;         // 0x12
        NUMBER m_nPrimaryColor;    // 0x16
        NUMBER m_nSecondaryColor;  // 0x17
        float m_fHealth;           // 0x18
        char m_nInterior;          // 0x1C
        int m_nDoorDamageStatus;   // 0x1D
        int m_nPanelDamageStatus;  // 0x21
        char m_nLightDamageStatus; // 0x25
        bool m_bDoorsLocked;       // 0x26
        bool m_bHasSiren;          // 0x27
    };

    struct WaitingList
    {
        VehicleInfo m_entry[WAITING_LIST_SIZE]; // 0x000
        BOOL m_bNotEmpty[WAITING_LIST_SIZE];    // 0xFA0
    };

public:
    int m_nCount;                                 // 0x00000
    WaitingList m_waitingList;                    // 0x00004
    CVehicle* m_pObject[MAX_VEHICLES];            // 0x01134
    BOOL m_bNotEmpty[MAX_VEHICLES];               // 0x03074
    void* m_pGameObject[MAX_VEHICLES];            // 0x04FB4, GTA CVehicle*
    unsigned int pad_6ef4[MAX_VEHICLES];          // 0x06EF4
    ID m_nLastUndrivenId[MAX_VEHICLES];           // 0x08E34
    TICK m_lastUndrivenProcessTick[MAX_VEHICLES]; // 0x09DD4
    BOOL m_bIsActive[MAX_VEHICLES];               // 0x0BD14
    BOOL m_bIsDestroyed[MAX_VEHICLES];            // 0x0DC54
    TICK m_tickWhenDestroyed[MAX_VEHICLES];       // 0x0FB94
    CVector m_spawnedAt[MAX_VEHICLES];            // 0x11AD4
    BOOL m_bNeedsToInitializeLicensePlates;       // 0x17894

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
    void AddToWaitingList(const VehicleInfo* info);
    void ConstructLicensePlates();
    void ShutdownLicensePlates();
    BOOL Create(VehicleInfo* info);
    void SendDestroyNotification(ID id);
    void ProcessWaitingList();
    void Process();
    CVehicle* Get(ID id);
    BOOL DoesExist(ID id);
};
