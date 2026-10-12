/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>
#include "game/CGame.h"
#include "main.h"
#include "net/CNetGame.h"
#include "game/patch/FrameLimiter.h"
#include "game/patch/HudScaleFix.h"
#include "game/entities/CPed.h"
#include "game/entities/CVehicle.h"
#include "gui/CDeathWindow.h"

#include "Commands.h"
#include "game/ModelInfo.h"
#include "game/Debug.h"

#include "CChat.h"
#include "config/CConfig.h"
#include "config/CSettings.h"
#include "render/CFonts.h"
#include "render/render.h"
#include "CInput.h"

#include <cstdlib>

namespace Commands
{
    namespace
    {
#if defined(_DEBUG)
        bool g_cameraTargetLabels = false;
#endif // _DEBUG
        bool g_hudScaleFix = true;
        bool g_objectsLight = true;
        bool g_debugLabels = false;

        bool HasText(const char* params)
        {
            return params && params[0] != '\0';
        }

        int ToInt(const char* params)
        {
            return params ? std::atoi(params) : 0;
        }

        void PrintInfo(const char* format, ...)
        {
            ZoneScoped;
            if (!g_chat)
                return;

            char buffer[512];
            buffer[0] = '\0';

            va_list args;
            va_start(args, format);
            vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
            va_end(args);

            g_chat->PrintInfo("%s", buffer);
        }

        void PrintDebug(const char* format, ...)
        {
            ZoneScoped;
            if (!g_chat)
                return;

            char buffer[512];
            buffer[0] = '\0';

            va_list args;
            va_start(args, format);
            vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
            va_end(args);

            g_chat->PrintDebug("%s", buffer);
        }

        void WriteInt(CConfig::eParam param, int value)
        {
            g_config.SetParam(param, value);
            g_config.Save();
        }

        int ReadInt(CConfig::eParam param, int fallback)
        {
            int value = fallback;
            g_config.GetParam(param, value);
            return value;
        }

        bool ReadBool(CConfig::eParam param, bool fallback)
        {
            bool value = fallback;
            g_config.GetParam(param, value);
            return value;
        }

        std::string GetSampDirectory()
        {
            return GetSampUserDirectory();
        }

        bool QueryPlayerSnapshot(PlayerSnapshot& snapshot)
        {
            ZoneScoped;
            auto* ped = g_game ? g_game->GetPlayerPed() : nullptr;
            if (!ped || !ped->IsValid())
                return false;
            CEntity vehicle;
            vehicle.m_pGameEntity = ped->IsInVehicle() ? ped->GetVehicle() : nullptr;
            const CEntity* entity = vehicle.m_pGameEntity ? &vehicle : ped;
            if (!entity->IsValid())
                return false;
            CMatrix matrix = CMatrix::Unity();
            entity->GetMatrix(&matrix);
            const auto position = matrix.GetPosition();
            snapshot.inVehicle = vehicle.m_pGameEntity != nullptr;
            snapshot.model = entity->GetModelIndex();
            snapshot.x = position.x;
            snapshot.y = position.y;
            snapshot.z = position.z;
            snapshot.rotation = snapshot.inVehicle ? std::atan2(-matrix.GetForward().x, matrix.GetForward().y) * 180.0f / 3.14159265358979323846f : ped->GetRotation();
            if (snapshot.rotation < 0.0f)
                snapshot.rotation += 360.0f;
            if (snapshot.inVehicle)
            {
                snapshot.color1 = CVehicle::GetNativePrimaryColor(vehicle.m_pGameEntity);
                snapshot.color2 = CVehicle::GetNativeSecondaryColor(vehicle.m_pGameEntity);
            }
            return true;
        }

#if defined(_DEBUG)
        bool DebugAvailable()
        {
            return g_settings.m_bDebugMode != FALSE;
        }
#endif // _DEBUG
    }

    // +0x684E0
    void Default(const char* params)
    {
        if (params && g_netGame)
            g_netGame->ChatMessage(params);
    }

    // +0x685F0
    void SetChatPageSize(const char* params)
    {
        ZoneScoped;
        if (HasText(params))
        {
            const int value = ToInt(params);
            if (value >= 10 && value <= 20)
            {
                if (g_chat)
                    g_chat->SetPageSize(value);

                WriteInt(CConfig::PARAM_PAGE_SIZE, value);
                return;
            }
        }

        PrintDebug("pagesize [10-20] (lines)");
    }

    // +0x68670
    void SetChatFontSize(const char* params)
    {
        ZoneScoped;
        if (!HasText(params))
        {
            PrintDebug("Valid fontsize: -3 to 5");
            return;
        }

        const int value = ToInt(params);
        if (value < -3 || value > 5)
        {
            PrintDebug("Valid fontsize: -3 to 5");
            return;
        }

        WriteInt(CConfig::PARAM_FONT_SIZE, value);

        if (g_fonts)
        {
            if (!g_fonts->Reset())
            {
                Log("failed to recreate fonts!");
                ExitProcess(1);
            }
        }

        if (g_chat)
        {
            g_chat->OnResetDevice();
            g_chat->UpdateScrollbar();
        }

        if (g_deathWindow)
            g_deathWindow->ResetFonts();
        render::ApplyGuiFontConfig();
    }

    // +0x68720 -> CNetGame helper at +0x8E90
    void DrawNameTagStatus(const char*)
    {
        ZoneScoped;
        bool enabled = !ReadBool(CConfig::PARAM_NO_NAMETAG_STATUS, false);
        enabled = !enabled;

        if (g_netGame)
            g_netGame->m_bNametagStatus = enabled;

        WriteInt(CConfig::PARAM_NO_NAMETAG_STATUS, enabled ? 0 : 1);
        PrintInfo(enabled ? "NameTag Player Status: ON" : "NameTag Player Status: OFF");
    }

    // +0x68730
    void DrawChatTimestamps(const char*)
    {
        ZoneScoped;
        if (!g_chat)
            return;

        const bool enabled = !g_chat->GetTimestamps();
        g_chat->SetTimestamps(enabled);
        WriteInt(CConfig::PARAM_TIMESTAMP, enabled ? 1 : 0);
    }

    // +0x68790
    void ToggleAudioStreamMessages(const char*)
    {
        const bool disabled = ReadBool(CConfig::PARAM_AUDIO_MESSAGES_OFF, false);
        WriteInt(CConfig::PARAM_AUDIO_MESSAGES_OFF, disabled ? 0 : 1);
        PrintInfo(disabled ? "Audio messages: On" : "Audio messages: Off");
    }

    // +0x68800
    void ToggleURLMessages(const char*)
    {
        const bool enabled = ReadBool(CConfig::PARAM_LOG_URLS, false);
        WriteInt(CConfig::PARAM_LOG_URLS, enabled ? 0 : 1);
        PrintInfo(enabled ? "URL messages: Off" : "URL messages: On");
    }

    // +0x68870
    void ToggleHUDScaleFix(const char*)
    {
        bool enabled = game::patch::hudscalefix::is_enabled();
        enabled = !enabled;
        g_hudScaleFix = enabled;
        game::patch::hudscalefix::set_enabled(enabled);

        WriteInt(CConfig::PARAM_NO_HUD_SCALE_FIX, enabled ? 0 : 1);
    }

    // +0x688B0
    void PrintMemory(const char*)
    {
        ZoneScoped;
        int value = 0;
        if (g_game)
            value = g_game->GetUsedMemory();
        else
            value = *reinterpret_cast<volatile const int*>(0x8A5A80);

        PrintDebug("Memory: %u", static_cast<unsigned int>(value));
    }

    // +0x688D0
    void SetFrameLimiter(const char* params)
    {
        ZoneScoped;
        if (!HasText(params))
            return;

        char* end = nullptr;
        const long value = std::strtol(params, &end, 10);
        const bool parsed = end != params;
        while (*end == ' ' || *end == '\t')
            ++end;
        if (!parsed || *end || value < 20 || value > 90)
        {
            PrintInfo("-> Frame Limiter: valid amounts are 20-90.");
            return;
        }
        game::patch::fps::set_limit(static_cast<std::uint32_t>(value));

        WriteInt(CConfig::PARAM_FPS_LIMIT, static_cast<int>(value));
        PrintInfo("-> Frame Limiter: %ld", value);
    }

    // +0x68960
    void ToggleHeadMoves(const char*)
    {
        ZoneScoped;
        bool enabled = g_game ? g_game->DoesHeadMoves() != FALSE : !ReadBool(CConfig::PARAM_DISABLE_HEAD_MOVE, false);
        enabled = !enabled;
        if (g_game)
            g_game->SetHeadMoves(enabled ? TRUE : FALSE);

        WriteInt(CConfig::PARAM_DISABLE_HEAD_MOVE, enabled ? 0 : 1);
        PrintInfo(enabled ? "-> Head movements enabled" : "-> Head movements disabled");
    }

    // +0x689E0
    void Quit(const char*)
    {
        // safe quit, calls 0x53C900
        if (g_game)
            PostMessageA(static_cast<HWND>(g_game->GetWindowHandle()), WM_CLOSE, 0, 0);
    }

    // +0x689F0 - intentionally empty in R5.
    // void CmpStat(const char*)
    // {}

    // +0x68A00
    void SavePosition(const char* params)
    {
        ZoneScoped;
        PlayerSnapshot snapshot;
        if (!QueryPlayerSnapshot(snapshot))
            return;

        const std::string directory = GetSampDirectory();
        if (directory.empty())
        {
            PrintDebug("I can't open the savepositions.txt file for append.");
            return;
        }

        const std::string path = directory + "savedpositions.txt";
        FILE* file = nullptr;
        fopen_s(&file, path.c_str(), "a");
        if (!file)
        {
            PrintDebug("I can't open the savepositions.txt file for append.");
            return;
        }

        const char* comment = params ? params : "";

        if (snapshot.inVehicle)
        {
            std::fprintf(file, "AddStaticVehicle(%u,%.4f,%.4f,%.4f,%.4f,%u,%u); // %s\n", snapshot.model, snapshot.x, snapshot.y, snapshot.z, snapshot.rotation, snapshot.color1,
                         snapshot.color2, comment);
            std::fclose(file);
            PrintInfo("-> InCar position saved");
            return;
        }

        std::fprintf(file, "AddPlayerClass(%u,%.4f,%.4f,%.4f,%.4f,0,0,0,0,0,0); // %s\n", snapshot.model, snapshot.x, snapshot.y, snapshot.z, snapshot.rotation, comment);
        std::fclose(file);
        PrintInfo("-> OnFoot position saved");
    }

    // +0x68B80
    void SavePositionOnly(const char* params)
    {
        ZoneScoped;
        PlayerSnapshot snapshot;
        if (!QueryPlayerSnapshot(snapshot))
            return;

        const char* comment = params ? params : "";

        if (snapshot.inVehicle)
        {
            // R5 really opens this one relative to the current working directory.
            FILE* file = nullptr;
            fopen_s(&file, "rawvehicles.txt", "a");
            if (!file)
            {
                PrintDebug("I can't open the rawvehicles.txt file for append.");
                return;
            }

            std::fprintf(file, "%u,%.4f,%.4f,%.4f,%.4f,%u,%u ; %s\n", snapshot.model, snapshot.x, snapshot.y, snapshot.z, snapshot.rotation, snapshot.color1, snapshot.color2,
                         comment);
            std::fclose(file);
            PrintDebug("-> InCar pos saved");
            return;
        }

        const std::string directory = GetSampDirectory();
        if (directory.empty())
        {
            // This text is also used on the on-foot failure path in R5.
            PrintDebug("I can't open the rawvehicles.txt file for append.");
            return;
        }

        const std::string path = directory + "rawpositions.txt";
        FILE* file = nullptr;
        fopen_s(&file, path.c_str(), "a");
        if (!file)
        {
            PrintDebug("I can't open the rawvehicles.txt file for append.");
            return;
        }

        std::fprintf(file, "%.4f,%.4f,%.4f,%.4f ; %s\n", snapshot.x, snapshot.y, snapshot.z, snapshot.rotation, comment);
        std::fclose(file);
        PrintDebug("-> OnFoot pos saved");
    }

    // +0x68FD0
    void PrintCurrentInterior(const char*)
    {
        const unsigned int interior = g_game ? static_cast<unsigned char>(g_game->GetCurrentInterior()) : 0;
        PrintDebug("Current Interior: %u", interior);
    }

    // +0x69000
    void ToggleObjectsLight(const char*)
    {
        bool enabled = g_objectsLight;
        enabled = !enabled;
        g_objectsLight = enabled;
    }

    // +0x69020
    void ToggleDebugLabels(const char*)
    {
        g_debugLabels = !g_debugLabels;
        render::SetVehicleDebugLabels(g_debugLabels);
    }

    // +0x69030
    void SendRconCommand(const char* params)
    {
        ZoneScoped;
        if (params && g_netGame)
            g_netGame->SendRconCommand(params);
    }

#if defined(_DEBUG)
    namespace Debug
    {
        // +0x68500
        void TestDeathWindow(const char*)
        {
            ZoneScoped;
            if (!g_deathWindow)
                return;

            const std::uint32_t white = 0xFFFFFFFFu;
            g_deathWindow->AddMessage("Pooper", "Pooper333", white, white, 1);
            g_deathWindow->AddMessage("Pooper", "Pooper", white, white, 5);
            g_deathWindow->AddMessage("Pooper", "Pooper", white, white, 15);
            g_deathWindow->AddMessage("Pooper", "Pooper", white, white, 14);
            g_deathWindow->AddMessage("Pooper", "Pooper", white, white, 2);
            g_deathWindow->AddMessage(nullptr, "PooperPooperPooper0001", white, white, 5);
            g_deathWindow->AddMessage(nullptr, "Pooper", white, white, 0xFF);
            g_deathWindow->AddMessage("Pooper", "PooperPooperPooper0001", white, white, 0);
        }

        // +0x685E0
        void ToggleCameraTargetLabels(const char*)
        {
            bool enabled = g_cameraTargetLabels;
            enabled = !enabled;
            g_cameraTargetLabels = enabled;
            render::SetCameraTargetLabels(enabled);
        }

        // +0x68D00
        void SetPlayerSkin(const char* params)
        {
            ZoneScoped;
            if (!DebugAvailable())
                return;

            if (!HasText(params))
            {
                PrintDebug("Usage: player_skin (skin number).");
                return;
            }

            if (!g_game || !g_game->IsStarted())
                return;

            if (!g_game || !g_game->GetPlayerPed() || !g_game->GetPlayerPed()->IsValid())
                return;

            if (auto* ped = g_game ? g_game->GetPlayerPed() : nullptr)
                ped->SetModelIndex(ToInt(params));
        }

        // +0x68ED0
        void SetWorldWeather(const char* params)
        {
            ZoneScoped;
            if (!DebugAvailable())
                return;

            if (!HasText(params))
            {
                PrintDebug("Usage: set_weather (weather number)");
                return;
            }

            const int weather = ToInt(params);
            if (g_game && weather >= 0 && weather <= 22)
                g_game->SetWorldWeather(static_cast<char>(weather));
        }

        // +0x68F20
        void SetWorldTime(const char* params)
        {
            ZoneScoped;
            if (!DebugAvailable())
                return;

            if (!HasText(params))
            {
                PrintDebug("Usage: set_time (hour) (minute)");
                return;
            }

            int hour = 0;
            int minute = 0;
            if (std::sscanf(params, "%d%d", &hour, &minute) != 2 || hour < 0 || hour > 23 || minute < 0 || minute > 59)
            {
                PrintDebug("Invalid Time. Use /set_time (hour 0-23) (minute 0-59)");
                return;
            }

            if (g_game && hour >= 0 && hour < 24 && minute >= 0 && minute < 60)
                g_game->SetWorldTime(static_cast<char>(hour), static_cast<char>(minute));
        }
    }

#endif // _DEBUG
    // +0x69110
    void Setup()
    {
        ZoneScoped;
        if (!g_input)
            return;

        g_input->SetDefaultCommand(&Default);

        g_input->AddCommand("quit", &Quit);
        g_input->AddCommand("q", &Quit);
        g_input->AddCommand("save", &SavePosition);
        g_input->AddCommand("rs", &SavePositionOnly);
        g_input->AddCommand("rcon", &SendRconCommand);
        g_input->AddCommand("mem", &PrintMemory);
        g_input->AddCommand("fpslimit", &SetFrameLimiter);
        g_input->AddCommand("pagesize", &SetChatPageSize);
        g_input->AddCommand("fontsize", &SetChatFontSize);
        g_input->AddCommand("nametagstatus", &DrawNameTagStatus);
        g_input->AddCommand("timestamp", &DrawChatTimestamps);
        g_input->AddCommand("headmove", &ToggleHeadMoves);
        g_input->AddCommand("hudscalefix", &ToggleHUDScaleFix);

#if defined(_DEBUG)
        g_input->AddCommand("testdw", &Debug::TestDeathWindow);
        g_input->AddCommand("ctd", &Debug::ToggleCameraTargetLabels);

        if (DebugAvailable())
        {
            g_input->AddCommand("player_skin", &Debug::SetPlayerSkin);
            g_input->AddCommand("set_weather", &Debug::SetWorldWeather);
            g_input->AddCommand("set_time", &Debug::SetWorldTime);
        }

#endif // _DEBUG
        g_input->AddCommand("interior", &PrintCurrentInterior);
        g_input->AddCommand("togobjlight", &ToggleObjectsLight);
        // g_input->AddCommand("cmpstat", &CmpStat);
        g_input->AddCommand("dl", &ToggleDebugLabels);
        g_input->AddCommand("audiomsg", &ToggleAudioStreamMessages);
        g_input->AddCommand("logurls", &ToggleURLMessages);
    }
}
