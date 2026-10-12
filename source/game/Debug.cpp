#include "stdafx.h"

#include "Debug.h"

#include "main.h"
#include "config/CSettings.h"
#include "game/CCamera.h"
#include "game/CGame.h"

#ifdef _DEBUG
#   include "game/entities/CPed.h"
#   include "gui/CChat.h"
#   include <fstream>
#   include <cctype>
#   include <cerrno>
#   include <cmath>
#   include <span>
#endif // _DEBUG

namespace Debug
{
    bool Initialize()
    {
        ZoneScoped;
        if (!g_settings.m_bDebugMode)
            return true;

        const auto findPlayerPed = reinterpret_cast<void*(__cdecl*)(int)>(0x56E210);
        if (!findPlayerPed(-1))
            return false;

        // MAIN.SCM sets its fixed camera after creating the player and then fades in
        const auto screenFadeStatus = reinterpret_cast<int(__thiscall*)(void*)>(0x50AE20);
        const auto fadingDirection = reinterpret_cast<int(__thiscall*)(void*)>(0x50ADF0);
        void* nativeCamera = reinterpret_cast<void*>(0xB6F028);
        if (fadingDirection(nativeCamera) != 1 && screenFadeStatus(nativeCamera) != 0)
            return false;

        CCamera camera;
        camera.Restore();
#ifdef _DEBUG
        if (g_settings.m_szDebugScript[0])
            Script::Initialize(g_settings.m_szDebugScript);
#endif // _DEBUG
        return true;
    }

#ifdef _DEBUG
    namespace Script
    {
        namespace
        {
            void SkipSpace(const char*& cursor)
            {
                while (std::isspace(static_cast<unsigned char>(*cursor)))
                    ++cursor;
            }
            struct Call
            {
                std::string name;
                std::vector<double> arguments;
            };

            bool ParseCall(const std::string& text, Call& call)
            {
                ZoneScoped;
                call = {};
                const char* cursor = text.c_str();
                SkipSpace(cursor);
                const char* name = cursor;
                if (!std::isalpha(static_cast<unsigned char>(*cursor)) && *cursor != '_')
                    return false;
                while (std::isalnum(static_cast<unsigned char>(*cursor)) || *cursor == '_')
                    ++cursor;
                call.name.assign(name, cursor);
                SkipSpace(cursor);
                if (*cursor != '(')
                    return false;
                ++cursor;
                SkipSpace(cursor);
                if (*cursor != ')')
                {
                    for (;;)
                    {
                        char* end = nullptr;
                        errno = 0;
                        const double value = std::strtod(cursor, &end);
                        if (end == cursor || errno == ERANGE || !std::isfinite(value))
                            return false;
                        call.arguments.push_back(value);
                        cursor = end;
                        SkipSpace(cursor);
                        if (*cursor == ')')
                            break;
                        if (*cursor != ',')
                            return false;
                        ++cursor;
                        SkipSpace(cursor);
                    }
                }
                ++cursor;
                SkipSpace(cursor);
                return !*cursor;
            }

            bool SetPlayerPos(std::span<const double> arguments)
            {
                ZoneScoped;
                auto* ped = g_game ? g_game->GetPlayerPed() : nullptr;
                if (!ped || !ped->IsValid())
                    return false;
                const CVector position{static_cast<float>(arguments[1]), static_cast<float>(arguments[2]), static_cast<float>(arguments[3])};
                if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
                    return false;
                // playerid is ignored, just like the original debug script
                ped->Teleport(position);
                return true;
            }

            struct Function
            {
                const char* name;
                std::size_t argumentCount;
                bool (*handler)(std::span<const double>);
            };
            constexpr Function functions[] = {{"SetPlayerPos", 4, &SetPlayerPos}};

            bool Execute(const Call& call)
            {
                ZoneScoped;
                for (const auto& function : functions)
                {
                    if (call.name != function.name)
                        continue;
                    if (call.arguments.size() != function.argumentCount)
                    {
                        Log("[debugscript] %s expects %zu arguments, got %zu", function.name, function.argumentCount, call.arguments.size());
                        return false;
                    }
                    if (!function.handler(call.arguments))
                    {
                        Log("[debugscript] %s failed", function.name);
                        return false;
                    }
                    return true;
                }
                Log("[debugscript] unknown function: %s", call.name.c_str());
                return false;
            }

        }
        bool Initialize(const char* file)
        {
            ZoneScoped;
            if (!g_settings.m_bDebugMode || !file || !*file)
                return false;
            if (g_chat)
                g_chat->PrintDebug("DEBUGSCRIPT: Loading %s", file);
            std::ifstream input(file);
            if (!input)
            {
                Log("[debugscript] can't open %s", file);
                return false;
            }
            std::size_t calls = 0;
            std::string statement;
            while (std::getline(input, statement, ';'))
            {
                if (statement.find_first_not_of(" \t\r\n") == std::string::npos)
                    continue;
                Call call;
                if (input.eof() || !ParseCall(statement, call))
                {
                    Log("[debugscript] invalid call #%zu: expected function(arguments);", calls + 1);
                    return false;
                }
                if (!Execute(call))
                    return false;
                ++calls;
            }
            if (input.bad())
            {
                Log("[debugscript] can't read %s", file);
                return false;
            }
            Log("[debugscript] loaded %s, calls=%zu", file, calls);
            return true;
        }
    }
#endif // _DEBUG
}
