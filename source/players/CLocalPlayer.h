/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "sync/Animation.h"
#include "sync/Synchronization.h"
#include "utils/math/CVector.h"

class CPed;
class CVehicle;
class CObject;
class CPad;

enum SpectatingMode : std::uint8_t
{
    SPECTATING_MODE_VEHICLE = 3,
    SPECTATING_MODE_PLAYER = 4,
    SPECTATING_MODE_SIDE = 14,
    SPECTATING_MODE_FIXED = 15
};

enum SpectatingType : std::uint8_t
{
    SPEC_TYPE_NONE,
    SPEC_TYPE_PLAYER,
    SPEC_TYPE_VEHICLE
};

enum SurfingMode
{
    SURFING_MODE_NONE,
    SURFING_MODE_UNFIXED,
    SURFING_MODE_FIXED
};

class CLocalPlayer
{
public:
    using ID = std::uint16_t;
    using NUMBER = std::uint8_t;
    using TICK = std::uint32_t;

    static constexpr ID INVALID_ID = 0xFFFF;

    struct CameraTarget
    {
        ID m_nObject;
        ID m_nVehicle;
        ID m_nPlayer;
        ID m_nActor;
    };

    struct Head
    {
        CVector m_direction;
        TICK m_lastUpdate;
        TICK m_lastLook;
    };

    struct SpawnInfo
    {
        NUMBER m_nTeam;                // 0x00
        int m_nSkin;                   // 0x01
        char field_5;                  // 0x05
        CVector m_position;            // 0x06
        float m_fRotation;             // 0x12
        std::uint32_t m_nCustomSkin{}; // 0x16, DL runtime data
        std::array<std::int32_t, 3> m_spawnWeapons{-1, -1, -1};
        std::array<std::int32_t, 3> m_spawnAmmo{};
    };

    struct OpaqueCombatState
    {
        std::uint8_t data[0x46];
    };

    struct Surfing
    {
        ID m_nEntityId; // vehicle: [0, 2000), object: 2000 + object id
        TICK m_lastUpdate;
        union
        {
            CVehicle* m_pVehicle;
            CObject* m_pObject;
        };
        BOOL m_bStuck;
        BOOL m_bIsActive;
        CVector m_position;
        int field_1e;
        int m_nMode;
    };

    struct ClassSelection
    {
        BOOL m_bEnableAfterDeath;
        int m_nSelected;
        BOOL m_bWaitingForSpawnRequestReply;
        BOOL m_bIsActive;
    };

    struct Spectating
    {
        char m_nMode;
        char m_nType;
        int m_nObject;
        BOOL m_bProcessed;
    };

    struct Damage
    {
        ID m_nVehicleUpdating;
        int m_nBumper;
        int m_nDoor;
        std::uint8_t m_bLight;
        std::uint8_t m_bWheel;
    };

    Synchronization::IncarData m_incarData;         // 0x000
    Synchronization::AimData m_aimData;             // 0x03F
    Synchronization::TrailerData m_trailerData;     // 0x05E
    Synchronization::OnfootData m_onfootData;       // 0x094
    Synchronization::PassengerData m_passengerData; // 0x0D8

    BOOL m_bIsActive;                // 0x0F0
    BOOL m_bIsWasted;                // 0x0F4
    ID m_nCurrentVehicle;            // 0x0F8
    ID m_nLastVehicle;               // 0x0FA
    Animation m_animation;           // 0x0FC
    int field_100;                   // 0x100
    CPed* m_pPed;                    // 0x104
    BOOL m_bDoesSpectating;          // 0x108
    NUMBER m_nTeam;                  // 0x10C
    short field_10d;                 // 0x10D
    TICK m_lastUpdate;               // 0x10F
    TICK m_lastSpecUpdate;           // 0x113
    TICK m_lastAimUpdate;            // 0x117
    TICK m_lastStatsUpdate;          // 0x11B
    CameraTarget m_cameraTarget;     // 0x11F
    TICK m_lastCameraTargetUpdate;   // 0x127
    Head m_head;                     // 0x12B
    TICK m_lastAnyUpdate;            // 0x13F
    BOOL m_bClearedToSpawn;          // 0x143
    TICK m_lastSelectionTick;        // 0x147
    TICK m_initialSelectionTick;     // 0x14B
    SpawnInfo m_spawnInfo;           // 0x14F
    BOOL m_bHasSpawnInfo;            // 0x17D
    TICK m_lastWeaponsUpdate;        // 0x181
    OpaqueCombatState m_weaponsData; // 0x185
    BOOL m_bPassengerDriveBy;        // 0x1CB
    char m_nCurrentInterior;         // 0x1CF
    BOOL m_bInRCMode;                // 0x1D0
    char m_szName[256];              // 0x1D4
    Surfing m_surfing;               // 0x2D4
    ClassSelection m_classSelection; // 0x2FA
    TICK m_zoneDisplayingEnd;        // 0x30A
    Spectating m_spectating;         // 0x30E
    Damage m_damage;                 // 0x318

    CLocalPlayer(); // 0x4C50

    CLocalPlayer(const CLocalPlayer&) = delete;
    CLocalPlayer& operator=(const CLocalPlayer&) = delete;
    static int& RefIncarSendrate();
    static int& RefOnfootSendrate();
    static int& RefFiringSendrate();
    static int& RefSendMultiplier();

    CPed* GetPed();
    void ResetData();
    void ProcessHead();
    void SetSpecialAction(char id);
    char GetSpecialAction();
    void UpdateSurfing();
    void SetSurfing(CVehicle* vehicle, BOOL stuck);
    void ProcessSurfing();
    BOOL NeedsToUpdate(const void* oldData, const void* newData, unsigned int len);
    int GetIncarSendRate();
    int GetOnfootSendRate();
    int GetUnoccupiedSendRate();
    void SetSpawnInfo(const SpawnInfo* info);
    BOOL Spawn();
    void SetColor(D3DCOLOR color);
    D3DCOLOR GetColorAsRGBA();
    D3DCOLOR GetColorAsARGB();
    void ProcessOnfootWorldBounds();
    void ProcessIncarWorldBounds();
    void RequestSpawn();
    void PrepareForClassSelection();
    void PrepareForClassSelection_Outcome(BOOL outcome);
    void EnableSpectating(BOOL enable);
    void SpectateForVehicle(ID id);
    void SpectateForPlayer(ID id);
    BOOL NeedsToSendOnfootData(short controllerState, short leftStickY, short leftStickX);
    BOOL NeedsToSendIncarData(short controllerState, short leftStickX, short leftStickY);
    bool DefineCameraTarget(CameraTarget* info);
    void UpdateCameraTarget();
    void DrawCameraTargetLabel();
    void SendUnoccupiedData(ID vehicle, char seat); // 0x4D30
    void SendOnfootData();
    void SendAimData();
    void SendTrailerData(ID trailer);
    void SendPassengerData();
    void WastedNotification();
    void RequestClass(int id);
    void ChangeInterior(char id);
    void Chat(const char* text);
    void EnterVehicle(int vehicle, BOOL passenger); // 0x5AD0
    void ExitVehicle(int vehicle);                  // 0x5BF0
    void OnServerVehicleWarp(ID vehicle);
    void SendStats();
    void UpdateVehicleDamage(ID vehicle); // 0x5DE0
    void NextClass();
    void PrevClass();
    void ProcessClassSelection();

    void UpdateWeapons();
    void SendBulletData(CVector origin, CVector target, void* entity);
    void SendTakeDamage(int id, float damage, int type, int bodyPart);
    void SendGiveDamage(int id, float damage, int type, int bodyPart);

    void ProcessSpectating();
    bool ProcessUnoccupiedSync(ID vehicle, CVehicle* object, float distance = -1.0f); // 0x6E00
    void EnterVehicleAsPassenger();
    void ProcessPassengerEntry(const CPad& keys);
    void SendIncarData();
    void Process(); // 0x74C0

private:
    bool m_passengerKeyDown = false;
    bool m_driveByKeyDown = false;
    bool m_vehicleEntryPending = false;
    bool m_vehicleExitPending = false;
    std::uint32_t m_vehicleTransitionStarted = 0;
};
