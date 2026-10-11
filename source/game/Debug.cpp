#include "stdafx.h"

#include "Debug.h"

#include "main.h"
#include "config/CSettings.h"
#include "game/CCamera.h"
#include "game/CGame.h"
#include "game/CPad.h"
#include "render/render.h"

#include "game/Scripting.h"
#include "sync/KeyStuff.h"
#include "utils/math/CVector.h"

#ifdef _DEBUG
#   include "game/entities/CPed.h"
#   include "gui/CChat.h"
#   include <fstream>
#   include <cctype>
#   include <cerrno>
#   include <cmath>
#   include <span>
#   include <vector>
#endif // _DEBUG

namespace Debug
{
    namespace
    {
        static CVector spawnPosition;
        static float spawnHeading = 0.0f;
        static int spawnInterior = 0;
        static bool debugInitialized = false;
        static bool waitingForRespawn = false;
        static std::uint32_t deathTime = 0;

        constexpr std::uint32_t respawnDelayMs = 3000;

        constexpr scm::OpcodeInfo actor{0x01F5, "iv"};
        constexpr scm::OpcodeInfo coordinates{0x00A0, "ivvv"};
        constexpr scm::OpcodeInfo heading{0x0172, "iv"};
        constexpr scm::OpcodeInfo leaveVehicle{0x0362, "ifff"};
        constexpr scm::OpcodeInfo selectInterior{0x04BB, "i"};
        constexpr scm::OpcodeInfo linkInterior{0x0860, "ii"};
        constexpr scm::OpcodeInfo loadScene{0x03CB, "fff"};
        constexpr scm::OpcodeInfo controllable{0x01B4, "ii"};

    }

    void DebugProcess()
    {
        if (!g_settings.m_bDebugMode)
            return;

        const auto findPlayerPed = reinterpret_cast<std::uint8_t*(__cdecl*)(int)>(0x56E210);
        auto* ped = findPlayerPed(-1);
        if (!ped)
            return;

        std::uint32_t handle = 0; // actor handle

        if (!debugInitialized)
        {
            // MAIN.SCM sets its fixed camera after creating the player and then fades in
            const auto screenFadeStatus = reinterpret_cast<int(__thiscall*)(void*)>(0x50AE20);
            const auto fadingDirection = reinterpret_cast<int(__thiscall*)(void*)>(0x50ADF0);
            void* nativeCamera = reinterpret_cast<void*>(0xB6F028);
            if (fadingDirection(nativeCamera) != 1 && screenFadeStatus(nativeCamera) != 0)
                return;

            CCamera camera;
            camera.Restore();
#ifdef _DEBUG
            if (g_settings.m_szDebugScript[0])
                Script::Initialize(g_settings.m_szDebugScript);
#endif // _DEBUG
            scm::ProcessCommand(&actor, 0, &handle);
            scm::ProcessCommand(&coordinates, handle, &spawnPosition.x, &spawnPosition.y, &spawnPosition.z);
            scm::ProcessCommand(&heading, handle, &spawnHeading);
            spawnInterior = ped[0x2F];
            debugInitialized = true;
            return;
        }

        const auto state = *reinterpret_cast<const std::uint32_t*>(ped + 0x530);
        const bool dead = *reinterpret_cast<const float*>(ped + 0x540) <= 0.0f || state == 54 || state == 55 || state == 56;
        if (!dead)
        {
            waitingForRespawn = false;
            return;
        }

        const auto now = static_cast<std::uint32_t>(g_game->GetTimer());
        if (!waitingForRespawn)
        {
            waitingForRespawn = true;
            deathTime = now;
            return;
        }
        if (now - deathTime < respawnDelayMs)
            return;

        scm::ProcessCommand(&actor, 0, &handle);
        // detach through gta's task/vehicle handling before native resurrection clears the pointer
        if (*reinterpret_cast<const std::uint32_t*>(ped + 0x46C) & 0x100)
            scm::ProcessCommand(&leaveVehicle, handle, spawnPosition.x, spawnPosition.y, spawnPosition.z);

        // use the resurrection routine directly: the full GTA restart also advances time by 12 hours
        const auto resurrect = reinterpret_cast<void(__cdecl*)(void*, CVector, float)>(0x442060);
        resurrect(ped, spawnPosition, spawnHeading);
        const auto playerIndex = *reinterpret_cast<const std::uint8_t*>(0xB7CD74);
        // CPlayerInfo::m_nPlayerState
        *reinterpret_cast<std::uint8_t*>(0xB7CD98 + playerIndex * 0x190 + 0xDC) = 0; // PLAYERSTATE_PLAYING
        // CGameLogic::GameState
        *reinterpret_cast<std::uint8_t*>(0x96A8B0) = 0; // GAMELOGIC_STATE_PLAYING
        // CGameLogic::TimeOfLastEvent
        *reinterpret_cast<std::uint32_t*>(0x96A8AC) = 0;
        scm::ProcessCommand(&selectInterior, spawnInterior);
        scm::ProcessCommand(&linkInterior, handle, spawnInterior);
        scm::ProcessCommand(&loadScene, spawnPosition.x, spawnPosition.y, spawnPosition.z);
        scm::ProcessCommand(&controllable, 0, 1);
        KeyStuff::GetInternalKeys()->DisablePlayerControls = 0;
        CCamera camera;
        camera.Restore();
        render::SetCursorMode(0, false);

        waitingForRespawn = false;
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
