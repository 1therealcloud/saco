/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "utils/math/CVector.h"

namespace AimStuff
{
    constexpr std::size_t PLAYER_COUNT = 210;
    constexpr std::size_t PLAYER_STAT_COUNT = 11;

    struct Aim
    {
        CVector front;
        CVector source;
        CVector sourceBeforeLookBehind;
        CVector up;
    };

    static_assert(sizeof(Aim) == 0x30);

    float& RefLocalPlayerCameraExtZoom(); // 0x143FD0
    float& RefLocalPlayerAspectRatio();   // 0x146B88
    float*& RefInternalCameraExtZoom();   // 0x1039D4
    float*& RefInternalAspectRatio();     // 0x1039D0
    float* ArrayCameraExtZoom();          // 0x1440B0
    float* ArrayAspectRatio();            // 0x146BB8
    char* ArrayCameraMode();              // 0x143FD8
    char*& RefInternalCameraMode();       // 0x113974
    Aim& RefLocalPlayerAim();             // 0x1443F8
    Aim* ArrayPlayerAim();                // 0x144428
    Aim*& RefInternalAim();               // 0x1039C8

    void UpdateCameraExtZoomAndAspectRatio();                                                         // 0x9C7C0
    void ApplyCameraExtZoomAndAspectRatio();                                                          // 0x9C7E0
    void SetCameraExtZoomAndAspectRatio(std::uint8_t player, float cameraExtZoom, float aspectRatio); // 0x9C800
    float GetAspectRatio();                                                                           // 0x9C820
    float GetCameraExtZoom();                                                                         // 0x9C830
    void ApplyCameraExtZoomAndAspectRatio(std::uint8_t player);                                       // 0x9C850
    void SetCameraMode(char mode, std::uint8_t player);                                               // 0x9C890
    char GetCameraMode(std::uint8_t player);                                                          // 0x9C8B0
    char GetCameraMode();                                                                             // 0x9C8C0
    void Initialize();                                                                                // 0x9C8D0
    void UpdateAim();                                                                                 // 0x9C940
    void ApplyAim();                                                                                  // 0x9C960
    Aim* GetAim();                                                                                    // 0x9C980
    void SetAim(int player, const Aim* aim);                                                          // 0x9C990
    void ApplyAim(int player);                                                                        // 0x9C9C0
    Aim* GetAim(int player);                                                                          // 0x9C9F0

    // names reconstructed from r5 disassembly
    void SetRemotePlayerStat(int player, int index, std::uint16_t value); // 0x9CA10
    void SetLocalPlayerStat(int index, std::uint16_t value);              // 0x9CA40
    void UpdatePlayerStats();                                             // 0x9CB30
    void ApplyPlayerStats();                                              // 0x9CBC0
    void ResetPlayerStats();                                              // 0x9CC90
    void ApplyPlayerStats(std::uint8_t player);                           // 0x9CD20
}
