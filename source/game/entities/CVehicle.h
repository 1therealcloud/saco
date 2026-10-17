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
#include <cstddef>
#include <cstdint>

#include "CEntity.h"

class CVehicle : public CEntity
{
public:
    bool HasNativeVehicle() const;
    void* GetNativeVehicleHandle() const;
    unsigned char GetPrimaryColor() const
    {
        return HasNativeVehicle() ? static_cast<const unsigned char*>(m_pGameVehicle)[0x434] : 0;
    }
    unsigned char GetSecondaryColor() const
    {
        return HasNativeVehicle() ? static_cast<const unsigned char*>(m_pGameVehicle)[0x435] : 0;
    }

    static unsigned char GetNativePrimaryColor(const void* vehicle)
    {
        return vehicle ? static_cast<const unsigned char*>(vehicle)[0x434] : 0;
    }
    static unsigned char GetNativeSecondaryColor(const void* vehicle)
    {
        return vehicle ? static_cast<const unsigned char*>(vehicle)[0x435] : 0;
    }
    static std::uint32_t GetNativeRef(void* vehicle);
    static CVehicle* FindNative(void* vehicle);
    int GetUnoccupiedSyncSeat(const void* localPed) const;

    static constexpr std::size_t MAX_LICENSE_PLATE_TEXT = 32;

    CVehicle* m_pTrailer = nullptr;
    void* m_pGameVehicle = nullptr; // GTA CVehicle*
    BOOL m_bEngineOn = -1;
    BOOL m_bIsLightsOn = -1;
    BOOL m_bIsInvulnerable = FALSE;
    int pad_5c = 0;
    BOOL m_bIsLocked = FALSE;
    bool m_bIsObjective = false;
    BOOL m_bObjectiveBlipCreated = FALSE;
    DWORD m_timeSinceLastDriven = 0;
    BOOL m_bHasBeenDriven = FALSE;
    int pad_71 = 0;
    BOOL m_bEngineState = FALSE;
    unsigned char m_nPrimaryColor = 0;
    unsigned char m_nSecondaryColor = 0;
    BOOL m_bNeedsToUpdateColor = FALSE;
    BOOL m_bUnoccupiedSync = FALSE;
    BOOL m_bRemoteUnocSync = FALSE;
    BOOL m_bKeepModelLoaded = FALSE;
    BOOL m_bHasSiren = FALSE;
    IDirect3DTexture9* m_pLicensePlate = nullptr;
    char m_szLicensePlateText[MAX_LICENSE_PLATE_TEXT + 1]{};
    std::uint32_t m_marker = 0;

    CVehicle(int model, CVector position, float rotation, BOOL keepModelLoaded, BOOL hasSiren); // 0xB83D0
    ~CVehicle() override;                                                                       // 0xB88F0

    void Add() override;    // 0xB8A10
    void Remove() override; // 0xB8A40

    void ChangeInterior(int id); // 0xB7090
    void ResetPointers();        // 0xB70C0
    void* GetDriver() const;
    BOOL HasDriver(); // 0xB70E0
    bool HasPlayerDriver() const;
    BOOL IsOccupied();                                             // 0xB7130
    void SetInvulnerable(BOOL invulnerable);                       // 0xB7190
    void SetLocked(BOOL locked);                                   // 0xB7230
    float GetHealth();                                             // 0xB72A0
    void SetHealth(float value);                                   // 0xB72C0
    void SetColor(unsigned char primary, unsigned char secondary); // 0xB72E0
    void UpdateColor();                                            // 0xB7330
    int GetSubtype();                                              // 0xB7390
    BOOL IsSunk();                                                 // 0xB73B0
    BOOL IsWrecked();                                              // 0xB73D0
    BOOL DriverIsPlayerPed();                                      // 0xB73F0
    BOOL HasPlayerPed();                                           // 0xB7420
    BOOL IsTrainPart();                                            // 0xB7460
    BOOL HasTurret();                                              // 0xB74A0
    void EnableSiren(bool enable);                                 // 0xB7540
    void SetAlarmTime(std::uint16_t duration);                     // 0xB7580
    void SetAlarmState(bool enable);
    void SetBonnetState(bool open);
    void SetBootState(bool open);
    void SetObjective(bool enable);
    BOOL SirenEnabled();                   // 0xB7560
    void SetLandingGearState(BOOL hide);   // 0xB75A0
    BOOL GetLandingGearState();            // 0xB7630
    void SetHydraThrusters(int direction); // 0xB76A0
    int GetHydraThrusters();               // 0xB76C0
    void ProcessMarkers(float distance);
    void ProcessMarkers();                                    // 0xB76E0
    void Lock(BOOL lock);                                     // 0xB7840
    BOOL UpdateLastDrivenTime();                              // 0xB7870
    void SetTurretRotation(float horizontal, float vertical); // 0xB78A0
    float GetTurretHorizontalRotation() const;                // 0xB78C0
    float GetTurretVerticalRotation() const;                  // 0xB78D0
    float GetTrainSpeed();                                    // 0xB78E0
    void SetTrainSpeed(float value);                          // 0xB7900
    void SetTires(char state);                                // 0xB7940
    char GetTires();                                          // 0xB7A30
    void UpdateDamage(int panels, int doors, char lights);    // 0xB7AC0
    int GetPanelsDamage();                                    // 0xB7B80
    int GetDoorsDamage();                                     // 0xB7BB0
    char GetLightsDamage();                                   // 0xB7BE0
    void AttachTrailer();                                     // 0xB7C10
    void DetachTrailer();                                     // 0xB7C30
    void SetTrailer(CVehicle* vehicle);                       // 0xB7C80
    CVehicle* GetTrailer();                                   // 0xB7C90
    CVehicle* GetTractor();                                   // 0xB7CF0
    BOOL IsTrailer();                                         // 0xB7D70
    BOOL IsTowtruck();                                        // 0xB7DD0
    BOOL IsRC();                                              // 0xB7E00
    void EnableLights(bool enable);                           // 0xB7E50
    void RemovePassengers();                                  // 0xB7EE0
    void ProcessComponents(unsigned& budget);
    BOOL AddComponent(unsigned short id);                // 0xB7FC0
    BOOL RemoveComponent(unsigned short id);             // 0xB80B0
    void SetPaintjob(unsigned char id);                  // 0xB80F0
    BOOL DoesExist();                                    // 0xB8140
    void SetLicensePlateText(const char* text);          // 0xB8150
    void SetRotation(float value);                       // 0xB8170
    void ConstructLicensePlate();                        // 0xB81A0
    void ShutdownLicensePlate();                         // 0xB81F0
    void SetEngineOn(BOOL state);                        // 0xB8210
    BOOL GetEngineOn() const;                            // 0xB8220
    void SetLightsOn(BOOL state);                        // 0xB8230
    BOOL GetLightsOn() const;                            // 0xB8240
    void OpenDoor(int component, int door, float ratio); // 0xB8250
    BOOL HasSiren();                                     // 0xB8330
    char GetMaxPassengers();                             // 0xB8340
    void SetDoorOpenState(unsigned char doorId, bool open);
    void SetWindowOpenFlag(unsigned char component);   // 0xB8370
    void ClearWindowOpenFlag(unsigned char component); // 0xB83A0
    void EnableEngine(BOOL enable);                    // 0xB8A70
};
