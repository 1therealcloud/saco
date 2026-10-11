#pragma once

#include <cstdint>

namespace Commands
{
    struct PlayerSnapshot
    {
        bool inVehicle;
        unsigned int model;
        float x;
        float y;
        float z;
        float rotation;
        unsigned int color1;
        unsigned int color2;

        PlayerSnapshot() : inVehicle(false), model(0), x(0.0f), y(0.0f), z(0.0f), rotation(0.0f), color1(0), color2(0)
        {}
    };

    void Default(const char* params);                   // forward chat text or unknown commands to the server
    void SetChatPageSize(const char* params);           // /pagesize: set the number of visible chat lines
    void SetChatFontSize(const char* params);           // /fontsize: change chat font size
    void DrawNameTagStatus(const char* params);         // /nametagstatus: toggle player status icons on nametags
    void DrawChatTimestamps(const char* params);        // /timestamp: toggle chat timestamps
    void ToggleAudioStreamMessages(const char* params); // /audiomsg: toggle audio stream messages
    void ToggleURLMessages(const char* params);         // /logurls: toggle url messages
    void ToggleHUDScaleFix(const char* params);         // /hudscalefix: toggle the hud scaling fix
    void PrintMemory(const char* params);               // /mem: print game memory usage
    void SetFrameLimiter(const char* params);           // /fpslimit: set the frame rate limit
    void ToggleHeadMoves(const char* params);           // /headmove: toggle player head movement
    void Quit(const char* params);                      // /quit, /q: close the game
    // void CmpStat(const char* params); // /cmpstat: reserved, currently does nothing
    void SavePosition(const char* params);         // /save: save a player class or vehicle spawn with an optional comment
    void SavePositionOnly(const char* params);     // /rs: save raw position data with an optional comment
    void PrintCurrentInterior(const char* params); // /interior: print the current interior id
    void ToggleObjectsLight(const char* params);   // /togobjlight: toggle object lighting
    void ToggleDebugLabels(const char* params);    // /dl: toggle debug labels
    void SendRconCommand(const char* params);      // /rcon: send an rcon command to the server

#if defined(_DEBUG)
    namespace Debug
    {
        void TestDeathWindow(const char* params);          // /testdw: add test entries to the kill feed
        void ToggleCameraTargetLabels(const char* params); // /ctd: toggle camera target labels
        void SetPlayerSkin(const char* params);            // /player_skin: set the player skin in debug mode
        void SetWorldWeather(const char* params);          // /set_weather: change weather in debug mode
        void SetWorldTime(const char* params);             // /set_time: change world time in debug mode
    }
#endif            // _DEBUG
    void Setup(); // register client commands in CInput
}
