#pragma once

#include <Windows.h>
#include <memory>
#include <string>
#include "utils/math/CVector.h"

class CConfig;
class CChat;
class CFonts;

class CAudioStream
{
public:
    enum
    {
        AUDIOSTREAM_MAX_STRING = 256
    };
    static constexpr auto AUDIOSTREAM_USERAGENT = "SA-MP/0.3";
    struct Environment
    {
        float radioVolume = 0;
        bool muted = true;
        bool hasPlayer = false;
        CVector playerPosition{};
    };
    struct Info
    {
        std::string url, title, station, stationUrl;
    };

    explicit CAudioStream(CConfig* config, CChat* chat, CFonts* fonts);
    ~CAudioStream();
    CAudioStream(const CAudioStream&) = delete;
    CAudioStream& operator=(const CAudioStream&) = delete;

    BOOL Reset();
    BOOL Stop(bool wait);
    BOOL Play(const char* url, CVector position, float radius, bool is3d);
    void ControlGameRadio();
    void DrawInfo();
    void ConstructInfo();
    bool IsPlaying() const;
    Info GetInfo() const;
    bool m_bInitialized = false;

private:
    struct State;
    static void Process(State* state);
    static void CALLBACK SyncProc(DWORD handle, DWORD channel, DWORD data, void* user);
    std::unique_ptr<State> m_state;
    CConfig* m_config;
    CChat* m_chat;
    CFonts* m_fonts;
};
