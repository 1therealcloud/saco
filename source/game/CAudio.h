/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <cstdint>
#include "utils/math/CVector.h"

class CAudio
{
public:
    enum SoundId
    {
        SOUND_OFF = 0,
        SOUND_DISABLE_OUTDOOR_AMBIENCE_TRACK = 1
    };

    CAudio() = default;
    ~CAudio();
    CAudio(const CAudio&) = delete;
    CAudio& operator=(const CAudio&) = delete;

    int GetRadioStation() const;
    void StartRadio(int station);
    void StopRadio();
    float GetRadioVolume() const;
    void StopOutdoorAmbienceTrack();
    void SetOutdoorAmbienceTrack(int sound);
    void Play(int sound, CVector location = {});
    void Process(); // start mission audio once the game has loaded it
    bool IsOutdoorAmbienceTrackDisabled() const;

private:
    std::int32_t m_nSoundState = 0; // 0: idle, 1: loading, 2: playing
    bool m_bOutdoorAmbienceTrackDisabled = false;
};
