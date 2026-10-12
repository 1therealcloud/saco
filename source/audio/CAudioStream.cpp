/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "main.h"
#include "CAudioStream.h"
#include "game/CGame.h"
#include "game/entities/CPed.h"
#include "game/CAudio.h"
#include "gui/CChat.h"
#include "render/CFonts.h"
#include "config/CConfig.h"
#include <bass.h>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>

struct CAudioStream::State
{
    std::atomic<bool> playing{false}, stop{false};
    std::atomic<HSTREAM> stream{0};
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable wake;
    Info info;
    Environment environment;
    CVector position{};
    float radius = 0;
    bool is3d = false;
    CAudioStream* owner = nullptr;
};

namespace
{
    class ChannelLock
    {
    public:
        explicit ChannelLock(HSTREAM stream) : m_stream(stream), m_locked(BASS_ChannelLock(stream, TRUE) != FALSE)
        {}
        ~ChannelLock()
        {
            Unlock();
        }
        explicit operator bool() const
        {
            return m_locked;
        }
        void Unlock()
        {
            if (m_locked)
            {
                BASS_ChannelLock(m_stream, FALSE);
                m_locked = false;
            }
        }
        ChannelLock(const ChannelLock&) = delete;
        ChannelLock& operator=(const ChannelLock&) = delete;

    private:
        HSTREAM m_stream;
        bool m_locked;
    };

    std::string Bounded(const char* value)
    {
        return value ? std::string(value, strnlen_s(value, CAudioStream::AUDIOSTREAM_MAX_STRING)) : std::string{};
    }
    bool Prefix(const char* text, const char* prefix)
    {
        return _strnicmp(text, prefix, std::strlen(prefix)) == 0;
    }
    DWORD Volume(float volume, float scale)
    {
        return std::isfinite(volume) ? static_cast<DWORD>(std::clamp(volume * scale, 0.0f, 10000.0f)) : 0;
    }
}

CAudioStream::CAudioStream(CConfig* config, CChat* chat, CFonts* fonts) : m_state(std::make_unique<State>()), m_config(config), m_chat(chat), m_fonts(fonts)
{
    m_state->owner = this;
}

CAudioStream::~CAudioStream()
{
    Stop(true);
    if (m_bInitialized)
        BASS_Free();
}

BOOL CAudioStream::Reset()
{
    ZoneScoped;
    Stop(true);
    BASS_Free();
    m_bInitialized = false;
    if (HIWORD(BASS_GetVersion()) != BASSVERSION || !BASS_Init(-1, 44100, 0, nullptr, nullptr))
    {
        Log("[audiostream] BASS_Init failed, error=%d", BASS_ErrorGetCode());
        return FALSE;
    }
    bool proxyOff = false;
    if (m_config)
        m_config->GetParam(CConfig::PARAM_AUDIO_PROXY_OFF, proxyOff);
    BASS_SetConfigPtr(BASS_CONFIG_NET_AGENT, AUDIOSTREAM_USERAGENT);
    // bass uses the system proxy for an empty string; nullptr disables it
    BASS_SetConfigPtr(BASS_CONFIG_NET_PROXY, proxyOff ? nullptr : "");
    BASS_SetConfig(BASS_CONFIG_NET_PLAYLIST, 1);
    BASS_SetConfig(BASS_CONFIG_NET_TIMEOUT, 10000);
    // the current SDK omits EAX from its import library, but the DLL retains the R5 export
    const auto setEax = reinterpret_cast<BOOL(WINAPI*)(int, float, float, float)>(GetProcAddress(GetModuleHandleW(L"bass.dll"), "BASS_SetEAXParameters"));
    if (setEax)
        setEax(-1, 0.0f, -1.0f, -1.0f);
    m_bInitialized = true;
    ControlGameRadio();
    Environment environment;
    {
        std::scoped_lock lock(m_state->mutex);
        environment = m_state->environment;
    }
    BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, Volume(environment.radioVolume, 7000.0f));
    return TRUE;
}

BOOL CAudioStream::Stop(bool wait)
{
    ZoneScoped;
    auto& state = *m_state;
    const bool playing = state.playing.load();
    state.stop = true;
    state.wake.notify_all();
    BASS_StreamCancel(&state);
    if (wait && state.worker.joinable())
    {
        // cancellation can race the worker entering StreamCreateURL
        while (state.playing.load())
        {
            BASS_StreamCancel(&state);
            std::unique_lock lock(state.mutex);
            state.wake.wait_for(lock, std::chrono::milliseconds(2), [&] {
                return !state.playing.load();
            });
        }
        state.worker.join();
    }
    return m_bInitialized && playing ? TRUE : FALSE;
}

bool CAudioStream::IsPlaying() const
{
    return m_state->playing.load();
}

CAudioStream::Info CAudioStream::GetInfo() const
{
    std::scoped_lock lock(m_state->mutex);
    return m_state->info;
}

BOOL CAudioStream::Play(const char* url, CVector position, float radius, bool is3d)
{
    ZoneScoped;
    if (!m_bInitialized || !url || !*url)
        return FALSE;
    Stop(true);
    ControlGameRadio();
    auto& state = *m_state;
    std::string address = Bounded(url);
    {
        std::scoped_lock lock(state.mutex);
        if (state.environment.radioVolume <= 0 || !std::isfinite(state.environment.radioVolume))
            return FALSE;
        state.info = {};
        state.info.url = address;
        state.position = position;
        state.radius = radius;
        state.is3d = is3d;
    }
    bool messagesOff = false;
    if (m_config)
        m_config->GetParam(CConfig::PARAM_AUDIO_MESSAGES_OFF, messagesOff);
    if (!messagesOff && m_chat)
        m_chat->PrintInfo("Audio stream: %s", address.c_str());
    state.stop = false;
    state.playing = true;
    try
    {
        state.worker = std::thread(&CAudioStream::Process, &state);
    }
    catch (...)
    {
        state.playing = false;
        return FALSE;
    }
    return TRUE;
}

void CAudioStream::ConstructInfo()
{
    ZoneScoped;
    auto& state = *m_state;
    const HSTREAM stream = state.stream.load();
    if (!stream)
        return;
    // lock the BASS channel so AUTOFREE cannot invalidate borrowed tag strings
    ChannelLock channelLock(stream);
    if (!channelLock)
        return;
    std::string info;
    if (const char* meta = BASS_ChannelGetTags(stream, BASS_TAG_META))
    {
        if (const char* start = std::strstr(meta, "StreamTitle='"))
        {
            start += 13;
            if (const char* end = std::strchr(start, ';'); end && end > start)
            {
                info.assign(start, std::min<std::size_t>(end - start - 1, AUDIOSTREAM_MAX_STRING));
            }
        }
    }
    else if (const char* tags = BASS_ChannelGetTags(stream, BASS_TAG_OGG))
    {
        ZoneScoped;
        const char* title = nullptr;
        const char* artist = nullptr;
        for (const char* entry = tags; *entry; entry += std::strlen(entry) + 1)
        {
            if (Prefix(entry, "title="))
                title = entry + 6;
            if (Prefix(entry, "artist="))
                artist = entry + 7;
        }
        if (artist)
            info = Bounded(artist) + " - " + (title ? Bounded(title) : "(null)");
        else if (title)
            info = Bounded(title);
        if (info.size() > AUDIOSTREAM_MAX_STRING)
            info.resize(AUDIOSTREAM_MAX_STRING);
    }
    channelLock.Unlock();
    std::scoped_lock lock(state.mutex);
    state.info.title = std::move(info);
}

void CALLBACK CAudioStream::SyncProc(DWORD, DWORD, DWORD, void* user)
{
    ZoneScoped;
    auto* state = static_cast<State*>(user);
    try
    {
        state->owner->ConstructInfo();
    }
    catch (...)
    { /* exceptions must not escape a BASS callback */
    }
}

void CAudioStream::Process(State* state)
{
    ZoneScoped;
    HSTREAM stream = 0;
    try
    {
        const std::string url = state->owner->GetInfo().url;
        if (!state->stop.load())
            stream = BASS_StreamCreateURL(url.c_str(), 0, 0x940000, nullptr, state);
        state->stream = stream;
        if (stream && !state->stop.load())
        {
            ChannelLock channelLock(stream);
            if (!channelLock)
                throw std::runtime_error("invalid stream");
            const char* headers = BASS_ChannelGetTags(stream, BASS_TAG_ICY);
            if (!headers)
                headers = BASS_ChannelGetTags(stream, BASS_TAG_HTTP);
            Info info = state->owner->GetInfo();
            if (headers)
                for (const char* entry = headers; *entry; entry += std::strlen(entry) + 1)
                {
                    if (Prefix(entry, "icy-name:"))
                        info.station = Bounded(entry + 9);
                    if (Prefix(entry, "icy-url:"))
                        info.stationUrl = Bounded(entry + 8);
                }
            {
                std::scoped_lock lock(state->mutex);
                state->info = std::move(info);
            }
            BASS_ChannelSetSync(stream, BASS_SYNC_META, 0, &CAudioStream::SyncProc, state);
            BASS_ChannelSetSync(stream, BASS_SYNC_OGG_CHANGE, 0, &CAudioStream::SyncProc, state);
            channelLock.Unlock();
            state->owner->ConstructInfo();
            BASS_ChannelPlay(stream, FALSE);
            while (!state->stop.load())
            {
                BASS_StreamGetFilePosition(stream, BASS_FILEPOS_CONNECTED);
                DWORD volume = 0;
                {
                    std::unique_lock lock(state->mutex);
                    const auto& environment = state->environment;
                    if (!environment.muted)
                    {
                        if (!state->is3d)
                            volume = Volume(environment.radioVolume, 7000.0f);
                        else if (environment.hasPlayer && state->radius > 0)
                        {
                            const float distance = (environment.playerPosition - state->position).Magnitude();
                            if (distance <= state->radius)
                            {
                                const DWORD maximum = Volume(environment.radioVolume, 8000.0f);
                                volume = static_cast<DWORD>(maximum * (1.0f - distance / state->radius));
                            }
                        }
                    }
                }
                BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, volume);
                std::unique_lock lock(state->mutex);
                state->wake.wait_for(lock, std::chrono::milliseconds(20), [&] {
                    return state->stop.load();
                });
            }
        }
        else if (!stream && !state->stop.load())
            Log("[audiostream] unable to open %s, error=%d", url.c_str(), BASS_ErrorGetCode());
    }
    catch (...)
    {
        Log("[audiostream] stream worker failed");
    }
    if (stream)
    {
        BASS_ChannelStop(stream);
        BASS_StreamFree(stream);
    }
    state->stream = 0;
    state->playing = false;
    state->wake.notify_all();
}

void CAudioStream::ControlGameRadio()
{
    ZoneScoped;
    Environment environment;
    if (auto* game = g_game.get())
    {
        if (auto* audio = game->GetAudio())
            environment.radioVolume = audio->GetRadioVolume();
        const bool playing = CGame::GetState() == 9;
        environment.muted = !playing || CGame::IsPaused() || GetForegroundWindow() != static_cast<HWND>(game->GetWindowHandle());
        if (playing)
            environment.hasPlayer = CPed::GetLocalPlayerPosition(environment.playerPosition);
    }
    {
        std::scoped_lock lock(m_state->mutex);
        m_state->environment = environment;
    }
    if (m_bInitialized && IsPlaying() && CGame::GetState() == 9)
    {
        if (auto* game = g_game.get())
        {
            if (auto* audio = game->GetAudio())
            {
                audio->StartRadio(-1);
                audio->StopRadio();
            }
        }
    }
}

void CAudioStream::DrawInfo()
{
    ZoneScoped;
    if (!m_bInitialized || !IsPlaying() || !m_fonts)
        return;
    const auto info = GetInfo();
    if (info.title.empty() || info.station.empty())
        return;
    const std::string text = info.title + " - " + info.station;
    const int width = CGame::ScreenWidth();
    const int height = CGame::ScreenHeight();
    m_fonts->DrawLittleText(Bounded(text.c_str()).c_str(), RECT{15, height - 20, width, height + 10}, DT_NOCLIP, 0x99FFFFFF, false);
}
