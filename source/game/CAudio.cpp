#include <stdafx.h>

#include "CAudio.h"
#include "Scripting.h"

namespace
{
    constexpr scm::OpcodeInfo clearMissionAudio{0x040D, "i"};
    constexpr scm::OpcodeInfo loadMissionAudio{0x03CF, "ii"};
    constexpr scm::OpcodeInfo missionAudioLoaded{0x03D0, "i"};
    constexpr scm::OpcodeInfo playMissionAudio{0x03D1, "i"};
    constexpr scm::OpcodeInfo positionMissionAudio{0x03D7, "ifff"};
    constexpr scm::OpcodeInfo playSound{0x018C, "fffi"};
}

CAudio::~CAudio()
{
    Play(SOUND_OFF);
}

int CAudio::GetRadioStation() const
{
    // AERadioTrackManager
    const int station = *reinterpret_cast<const std::uint8_t*>(0x8CB6F8 + 0xE9);
    return station <= 12 ? station : -1;
}

void CAudio::StartRadio(int station)
{
    // CAudioEngine::StartRadio
    reinterpret_cast<void(__thiscall*)(void*, int, int)>(0x507DC0)(reinterpret_cast<void*>(0xB6BC90), station, 0);
}

void CAudio::StopRadio()
{
    // CAudioEngine::StopRadio
    reinterpret_cast<void(__thiscall*)(void*, int, int)>(0x506F70)(reinterpret_cast<void*>(0xB6BC90), 0, 0);
}

float CAudio::GetRadioVolume() const
{
    return *reinterpret_cast<const float*>(0xB5FCC8);
}

void CAudio::StopOutdoorAmbienceTrack()
{
    // CAEAmbienceTrackManager::StopSpecialMissionAmbienceTrack
    reinterpret_cast<void(__thiscall*)(void*)>(0x4D6D60)(reinterpret_cast<void*>(0x8AC15C));
}

void CAudio::SetOutdoorAmbienceTrack(int sound)
{
    // CAEAmbienceTrackManager::PlaySpecialMissionAmbienceTrack
    reinterpret_cast<void(__thiscall*)(void*, int)>(0x4D6D50)(reinterpret_cast<void*>(0x8AC15C), sound);
}

void CAudio::Play(int sound, CVector location)
{
    if (sound == SOUND_OFF)
    {
        if (m_nSoundState)
        {
            scm::ProcessCommand(&clearMissionAudio, 1);
            m_nSoundState = 0;
        }
        StopOutdoorAmbienceTrack();
        m_bOutdoorAmbienceTrackDisabled = false;
    }
    else if (sound == SOUND_DISABLE_OUTDOOR_AMBIENCE_TRACK)
        m_bOutdoorAmbienceTrackDisabled = true;
    else if (sound < 1000)
        SetOutdoorAmbienceTrack(sound);
    else if (sound < 2000)
        scm::ProcessCommand(&playSound, location.x, location.y, location.z, sound);
    else
    {
        scm::ProcessCommand(&clearMissionAudio, 1);
        scm::ProcessCommand(&loadMissionAudio, 1, sound);
        scm::ProcessCommand(&positionMissionAudio, 1, location.x, location.y, location.z);
        m_nSoundState = 1;
    }
}

void CAudio::Process()
{
    // R5 +0xA21D0: slot 1 is shared by all server-triggered mission audio
    if (m_nSoundState == 1 && scm::ProcessCommand(&missionAudioLoaded, 1))
    {
        scm::ProcessCommand(&playMissionAudio, 1);
        m_nSoundState = 2;
    }
}

bool CAudio::IsOutdoorAmbienceTrackDisabled() const
{
    return m_bOutdoorAmbienceTrackDisabled;
}
