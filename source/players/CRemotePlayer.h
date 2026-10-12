#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>

#include "sync/Animation.h"
#include "sync/Synchronization.h"
#include "utils/math/CVector.h"

class CMatrix;
class CPed;
class CVehicle;

class CRemotePlayer
{
public:
    using ID = std::uint16_t;
    using TICK = std::uint32_t;
    using GTAREF = int;

    static constexpr ID INVALID_ID = 0xFFFF;

    enum PlayerState
    {
        PLAYER_STATE_NONE,
        PLAYER_STATE_ONFOOT = 17,
        PLAYER_STATE_PASSENGER = 18,
        PLAYER_STATE_DRIVER = 19,
        PLAYER_STATE_WASTED = 32,
        PLAYER_STATE_SPAWNED = 33
    };

    enum UpdateType
    {
        UPDATE_TYPE_NONE,
        UPDATE_TYPE_ONFOOT = 16,
        UPDATE_TYPE_INCAR = 17,
        UPDATE_TYPE_PASSENGER = 18
    };

    enum PlayerStatus
    {
        PLAYER_STATUS_TIMEOUT = 2
    };

    struct IncarTargetRotation
    {
        float real;
        CVector imag;
    };

    struct Head
    {
        CVector m_direction;
        TICK m_lastUpdate;
        TICK m_lastLook;
    };

    struct MarkerPosition
    {
        int x;
        int y;
        int z;
    };

    int field_1;                                    // 0x000
    BOOL m_bDrawLabels;                             // 0x004
    BOOL m_bHasJetpack;                             // 0x008
    unsigned char m_nSpecialAction;                 // 0x00C
    char pad_2[12];                                 // 0x00D
    Synchronization::IncarData m_incarData;         // 0x019
    Synchronization::TrailerData m_trailerData;     // 0x058
    Synchronization::AimData m_aimData;             // 0x08E
    Synchronization::PassengerData m_passengerData; // 0x0AD
    Synchronization::OnfootData m_onfootData;       // 0x0C5
    unsigned char m_nTeam;                          // 0x109
    unsigned char m_nState;                         // 0x10A
    unsigned char m_nSeatId;                        // 0x10B
    int field_3;                                    // 0x10C
    BOOL m_bPassengerDriveBy;                       // 0x110
    char pad_1[76];                                 // 0x114
    CVector m_positionDifference;                   // 0x160
    IncarTargetRotation m_incarTargetRotation;      // 0x16C
    CVector m_onfootTargetPosition;                 // 0x17C
    CVector m_onfootTargetSpeed;                    // 0x188
    CVector m_incarTargetPosition;                  // 0x194
    CVector m_incarTargetSpeed;                     // 0x1A0
    float m_fReportedArmour;                        // 0x1AC
    float m_fReportedHealth;                        // 0x1B0
    Animation m_animation;                          // 0x1B4
    unsigned char m_nUpdateType;                    // 0x1B8
    TICK m_lastUpdate;                              // 0x1B9
    TICK m_lastTimestamp;                           // 0x1BD
    BOOL m_bPerformingCustomAnimation;              // 0x1C1
    int m_nStatus;                                  // 0x1C5
    Head m_head;                                    // 0x1C9
    CPed* m_pPed;                                   // 0x1DD
    CVehicle* m_pVehicle;                           // 0x1E1
    ID m_nId;                                       // 0x1E5
    ID m_nVehicleId;                                // 0x1E7
    BOOL m_bMarkerState;                            // 0x1E9
    MarkerPosition m_markerPosition;                // 0x1ED
    GTAREF m_marker;                                // 0x1F9

    CRemotePlayer();
    ~CRemotePlayer();

    CRemotePlayer(const CRemotePlayer&) = delete;
    CRemotePlayer& operator=(const CRemotePlayer&) = delete;

    void ProcessHead();
    void SetMarkerState(BOOL state);
    void SetMarkerPosition(int x, int y, int z);
    BOOL SurfingOnVehicle();
    BOOL SurfingOnObject();
    void ProcessSurfing();
    void OnEnterVehicle();
    void OnExitVehicle();
    void ProcessSpecialAction();
    void UpdateOnfootSpeedAndPosition();
    void UpdateOnfootRotation();
    void SetOnfootTargetSpeedAndPosition(CVector* position, CVector* speed);
    void UpdateIncarSpeedAndPosition();
    void UpdateIncarRotation();
    void SetIncarTargetSpeedAndPosition(CMatrix* matrix, CVector* position, CVector* speed);
    void UpdateTrain(CMatrix* matrix, CVector* speed, float trainSpeed);
    void Update(Synchronization::AimData* data);
    void Update(Synchronization::UnoccupiedData* data);
    void Update(Synchronization::TrailerData* data);
    void ResetData();
    float GetDistanceToPlayer(CRemotePlayer* player);
    float GetDistanceToLocalPlayer();
    void SetColor(D3DCOLOR color);
    D3DCOLOR GetColorAsRGBA();
    D3DCOLOR GetColorAsARGB();
    void EnterVehicle(ID id, BOOL passenger);
    void ExitVehicle();
    void ChangeState(char oldState, char newState);
    int GetStatus();
    void Update(Synchronization::BulletData* data);
    void Process();
    BOOL Spawn(int a2, int model, int a4, CVector* position, float rotation, D3DCOLOR color, char fightingStyle);
    void Update(Synchronization::OnfootData* data, TICK timestamp);
    void Update(Synchronization::IncarData* data, TICK timestamp);
    void Update(Synchronization::PassengerData* data, TICK timestamp = 0);
    void Remove();
    void Kill();
    void Chat(const char* text);
    BOOL DoesExist();
};
