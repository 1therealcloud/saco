/*
    Plugin-SDK file
    Authors: GTA Community. See more here
    https://github.com/DK22Pac/plugin-sdk
    Do not delete this comment block. Respect others' work!
*/
#pragma once

#include "ControllerState.h"

#include <array>
#include <type_traits>

// native GTA data only; value-initialize with CPad{} to get a zeroed snapshot
class CPad
{
public:
    CControllerState NewState;                            // 0x000
    CControllerState OldState;                            // 0x030
    std::array<std::int16_t, 10> SteeringLeftRightBuffer; // 0x060
    std::int32_t DrunkDrivingBufferUsed;                  // 0x074
    CControllerState PCTempKeyState;                      // 0x078
    CControllerState PCTempJoyState;                      // 0x0A8
    CControllerState PCTempMouseState;                    // 0x0D8
    char Phase;                                           // 0x108
    std::uint8_t pad109;
    std::int16_t Mode;     // 0x10A
    std::int16_t ShakeDur; // 0x10C
    union
    {
        std::uint16_t DisablePlayerControls; // 0x10E
        struct
        {
            std::uint16_t bCamera : 1;
            std::uint16_t unk2 : 1;
            std::uint16_t bPlayerAwaitsInGarage : 1;
            std::uint16_t bPlayerOnInteriorTransition : 1;
            std::uint16_t unk3 : 1;
            std::uint16_t bPlayerSafe : 1;
            std::uint16_t bPlayerTalksOnPhone : 1;
            std::uint16_t bPlayerSafeForCutscene : 1;
            std::uint16_t bPlayerSkipsToDestination : 1;
        };
    };
    char ShakeFreq;                       // 0x110
    std::array<char, 5> bHornHistory;     // 0x111
    char iCurrHornHistory;                // 0x116
    char JustOutOfFrontEnd;               // 0x117
    bool bApplyBrakes;                    // 0x118
    bool bDisablePlayerEnterCar;          // 0x119
    bool bDisablePlayerDuck;              // 0x11A
    bool bDisablePlayerFireWeapon;        // 0x11B
    bool bDisablePlayerFireWeaponWithL1;  // 0x11C
    bool bDisablePlayerCycleWeapon;       // 0x11D
    bool bDisablePlayerJump;              // 0x11E
    bool bDisablePlayerDisplayVitalStats; // 0x11F
    std::uint32_t LastTimeTouched;        // 0x120
    std::int32_t AverageWeapon;           // 0x124
    std::int32_t AverageEntries;          // 0x128
    float NoShakeBeforeThis;              // 0x12C
    char NoShakeFreq;                     // 0x130
    std::array<std::uint8_t, 3> pad131;
};
