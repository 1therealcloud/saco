#pragma once

#include <Windows.h>
#include <d3d9.h>
#include <cstddef>
#include <cstdint>

#include "CEntity.h"

class CVehicle : public CEntity
{
public:
    bool HasNativeVehicle() const
    {
        return m_pGameVehicle != nullptr;
    }
    void* GetNativeVehicleHandle() const
    {
        return m_pGameVehicle;
    }
    unsigned char GetPrimaryColor() const
    {
        return GetNativePrimaryColor(m_pGameVehicle);
    }
    unsigned char GetSecondaryColor() const
    {
        return GetNativeSecondaryColor(m_pGameVehicle);
    }

    static unsigned char GetNativePrimaryColor(const void* vehicle)
    {
        return vehicle ? static_cast<const unsigned char*>(vehicle)[0x434] : 0;
    }
    static unsigned char GetNativeSecondaryColor(const void* vehicle)
    {
        return vehicle ? static_cast<const unsigned char*>(vehicle)[0x435] : 0;
    }

    static constexpr std::size_t MAX_LICENSE_PLATE_TEXT = 32;

    CVehicle* m_pTrailer = nullptr;                          // 0x48
    void* m_pGameVehicle = nullptr;                          // 0x4C, GTA CVehicle*
    BOOL m_bEngineOn = -1;                                   // 0x50
    BOOL m_bIsLightsOn = -1;                                 // 0x54
    BOOL m_bIsInvulnerable = FALSE;                          // 0x58
    int pad_5c = 0;                                          // 0x5C
    BOOL m_bIsLocked = FALSE;                                // 0x60
    bool m_bIsObjective = false;                             // 0x64
    BOOL m_bObjectiveBlipCreated = FALSE;                    // 0x65
    DWORD m_timeSinceLastDriven = 0;                         // 0x69
    BOOL m_bHasBeenDriven = FALSE;                           // 0x6D
    int pad_71 = 0;                                          // 0x71
    BOOL m_bEngineState = FALSE;                             // 0x75
    unsigned char m_nPrimaryColor = 0;                       // 0x79
    unsigned char m_nSecondaryColor = 0;                     // 0x7A
    BOOL m_bNeedsToUpdateColor = FALSE;                      // 0x7B
    BOOL m_bUnoccupiedSync = FALSE;                          // 0x7F
    BOOL m_bRemoteUnocSync = FALSE;                          // 0x83
    BOOL m_bKeepModelLoaded = FALSE;                         // 0x87
    BOOL m_bHasSiren = FALSE;                                // 0x8B
    IDirect3DTexture9* m_pLicensePlate = nullptr;            // 0x8F
    char m_szLicensePlateText[MAX_LICENSE_PLATE_TEXT + 1]{}; // 0x93
    std::uint32_t m_marker = 0;                              // 0xB4

    CVehicle(int model, CVector position, float rotation, BOOL keepModelLoaded, BOOL hasSiren);
    ~CVehicle() override;

    void Add() override;
    void Remove() override;

    void ChangeInterior(int id);
    void ResetPointers();
    BOOL HasDriver();
    BOOL IsOccupied();
    void SetInvulnerable(BOOL invulnerable);
    void SetLocked(BOOL locked);
    float GetHealth();
    void SetHealth(float value);
    void SetColor(unsigned char primary, unsigned char secondary);
    void UpdateColor();
    int GetSubtype();
    BOOL IsSunk();
    BOOL IsWrecked();
    BOOL DriverIsPlayerPed();
    BOOL HasPlayerPed();
    BOOL IsTrainPart();
    BOOL HasTurret();
    void EnableSiren(bool enable);
    void SetAlarmState(bool enable);
    void SetBonnetState(bool open);
    void SetBootState(bool open);
    void SetObjective(bool enable);
    BOOL SirenEnabled();
    void SetLandingGearState(BOOL hide);
    BOOL GetLandingGearState();
    void SetHydraThrusters(int direction);
    int GetHydraThrusters();
    void ProcessMarkers();
    void Lock(BOOL lock);
    BOOL UpdateLastDrivenTime();
    float GetTrainSpeed();
    void SetTrainSpeed(float value);
    void SetTires(char state);
    char GetTires();
    void UpdateDamage(int panels, int doors, char lights);
    int GetPanelsDamage();
    int GetDoorsDamage();
    char GetLightsDamage();
    void AttachTrailer();
    void DetachTrailer();
    void SetTrailer(CVehicle* vehicle);
    CVehicle* GetTrailer();
    CVehicle* GetTractor();
    BOOL IsTrailer();
    BOOL IsTowtruck();
    BOOL IsRC();
    void EnableLights(bool enable);
    void RemovePassengers();
    BOOL AddComponent(unsigned short id);
    BOOL RemoveComponent(unsigned short id);
    void SetPaintjob(unsigned char id);
    BOOL DoesExist();
    void SetLicensePlateText(const char* text);
    void SetRotation(float value);
    void ConstructLicensePlate();
    void ShutdownLicensePlate();
    BOOL HasSiren();
    char GetMaxPassengers();
    void SetWindowOpenFlag(unsigned char doorId);
    void ClearWindowOpenFlag(unsigned char doorId);
    void EnableEngine(BOOL enable);
};
