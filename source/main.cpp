/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include <shlobj.h> // GetSampUserDirectory()
#include <chrono>
#include <cstdarg>
#include <cstdio>

#include "main.h"

#include "archive/keys.h"
#include "archive/saa.h"

#include "audio/CAudioStream.h"

#include "config/CConfig.h"
#include "config/CSettings.h"

#include "game/BuildingRemoval.h"
#include "game/CAudio.h"
#include "game/CCamera.h"
#include "game/CGame.h"
#include "game/render/models.h"
#include "game/CPad.h"
#include "game/entities/CPed.h"
#include "game/d3d9hook.h"
#include "game/Debug.h"
#include "game/FileSystem.h"
#include "game/patch/HudScaleFix.h"
#include "game/InputHandler.h"
#include "game/PlayerSlots.h"
#include "game/Scripting.h"

#include "game/patch/patch.h"
#include "game/patch/hooks.h"

#include "gui/Commands.h"

#include "net/CNetGame.h"
#include "players/CLocalPlayer.h"
#include "sync/KeyStuff.h"

#include "render/render.h"

// original samp keys container
static const std::vector<saa::KeySet> keys = {saa::OfficialKeySet()};

// saa archive
saa::Archive g_archive;

// config & settings
CConfig g_config;
CSettings g_settings;
std::string g_sampUserDirectory;

bool g_DLClient = true;

std::unique_ptr<CNetGame> g_netGame;   // net game
CAudioStream* g_audioStream = nullptr; // audiostream
CAudio* g_audio = nullptr;             // audio

// game
std::unique_ptr<CGame> g_game;

// log
#if defined(_DEBUG)
void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    vprintf(fmt, args);
    putchar('\n');

    va_end(args);
}
#endif // _DEBUG

// calculate build number
#if defined(_DEBUG)
inline constexpr int CalculateBuildNumber(std::string_view date = __DATE__)
{
    constexpr std::string_view months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    if (date.size() != 11)
        return -1;

    unsigned month = 0;
    for (unsigned i = 0; i < 12; ++i)
    {
        if (date.substr(0, 3) == months.substr(i * 3, 3))
        {
            month = i + 1;
            break;
        }
    }

    const auto digit = [](char value) {
        return value >= '0' && value <= '9';
    };
    if (date[3] != ' ' || date[6] != ' ' || (date[4] != ' ' && !digit(date[4])) || !digit(date[5]) || !digit(date[7]) || !digit(date[8]) || !digit(date[9]) || !digit(date[10]))
        return -1;

    const unsigned day = (date[4] == ' ' ? 0 : date[4] - '0') * 10 + date[5] - '0';
    const int year = (date[7] - '0') * 1000 + (date[8] - '0') * 100 + (date[9] - '0') * 10 + date[10] - '0';
    const std::chrono::year_month_day compiled{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
    if (!compiled.ok())
        return -1;

    constexpr auto released = std::chrono::sys_days{std::chrono::year{2006} / 5 / 10};
    return static_cast<int>((std::chrono::sys_days{compiled} - released).count());
}
#endif

// get samp directory
static std::string GetSampUserDirectory()
{
    char documents[MAX_PATH]{};

    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents)))
        return {};

    return std::string(documents) + "\\GTA San Andreas User Files\\SAMP\\";
}

// static check
bool IsGtaSa10Us()
{
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const auto entry = base + nt->OptionalHeader.AddressOfEntryPoint;
    return entry == 0x824570 || entry == 0x82457C;
}

// initializes resources once, then waits for the debug scene to be ready
static bool InitializeGameplay(IDirect3DDevice9* device)
{
    ZoneScoped;
    static std::once_flag resourcesOnce;
    std::call_once(resourcesOnce, [&] {
        // init ui
        // InitializeUi must be BEFORE
        render::InitializeUi(device);

        g_game->OnGameplayReady();

        if (!g_settings.m_bDebugMode)
        {
            g_netGame = std::make_unique<CNetGame>(g_settings.m_szHost, std::atoi(g_settings.m_szPort), g_settings.m_szNick, g_settings.m_szPass);
            bool hideNametagStatus = false;
            g_config.GetParam(CConfig::PARAM_NO_NAMETAG_STATUS, hideNametagStatus);
            g_netGame->m_bNametagStatus = !hideNametagStatus;
        }
        g_audio = g_game->GetAudio();

        g_audioStream = new CAudioStream(&g_config, g_chat.get(), g_fonts.get());
        g_audioStream->Reset();

        Commands::Setup();

        if (!game::inputhandler::install())
        {
#if defined(_DEBUG)
            Log("failed to install input handler!");
#endif // _DEBUG
            ExitProcess(1);
        }
#if defined(_DEBUG)
        Log("[inputhandler] installed");
#endif // _DEBUG
    });

    return !g_settings.m_bDebugMode || Debug::Initialize();
}

// game loop
void Process()
{
    ZoneScoped;

    if (!CGame::IsPlaying())
        return;

    if (auto* game = g_game.get())
    {
        game->OnGameplayReady();
        game->GetPlayerPed();
        game->ProcessInputEnabling();
        game->UpdateFarClippingPlane();
        if (!game->IsMenuVisible())
            game->ProcessCheckpoints();
    }

    if (g_audio)
        g_audio->Process();

    game::players::SetUseCJWalk(g_netGame && g_netGame->m_pSettings && g_netGame->m_pSettings->m_bUseCJWalk);
    if (g_netGame)
        g_netGame->Process();

    game::render::models::collect_custom_models();
    BuildingRemoval::Process();

    if (g_game && !g_settings.m_bDebugMode)
    {
        static bool hudHidden = false;
        static std::uint8_t savedHud = 0;
        static std::uint8_t savedRadar = 0;
        static void* lockedLocalPed = nullptr;

        auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
        auto* local = pool ? pool->GetLocalPlayer() : nullptr;
        const bool playing = g_netGame && g_netGame->GetState() == CNetGame::GAME_MODE_CONNECTED && local && local->m_bIsActive && !local->m_bIsWasted &&
                             !local->m_bDoesSpectating && !local->m_classSelection.m_bIsActive;
        if (playing)
        {
            if (hudHidden)
            {
                CGame::SetHudState(savedHud);
                CGame::SetRadarState(savedRadar);
                hudHidden = false;
            }
            lockedLocalPed = nullptr;
        }
        else
        {
            if (!hudHidden)
            {
                savedHud = CGame::GetHudState();
                savedRadar = CGame::GetRadarState();
                hudHidden = true;
            }
            CGame::SetHudState(0);
            CGame::SetRadarState(1);

            auto* ped = g_game->GetPlayerPed();
            if (!ped || !ped->HasNativePed() || (local && local->m_bIsWasted) || ped->IsDead())
                lockedLocalPed = nullptr;
            else
            {
                // gta scripts can restore controls before the server allows spawning
                auto* pad = KeyStuff::GetInternalKeys();
                if (lockedLocalPed != ped->GetNativePedHandle() || (pad && !pad->DisablePlayerControls))
                {
                    ped->SetControllable(FALSE);
                    lockedLocalPed = ped->GetNativePedHandle();
                }
            }
        }
    }

    if (CGame::IsPaused())
        return;

    CActor::ProcessAnimations();
    CObject::ProcessAll(CGame::FrameSeconds());
}

// render loop
void Frame(IDirect3DDevice9* device)
{
    ZoneScoped;

    // update map marker (rpc)
    CVector mapPosition{};
    if (CGame::GetMapClickPosition(mapPosition) && g_netGame && g_netGame->GetState() == CNetGame::GAME_MODE_CONNECTED)
        g_netGame->MapMarker(mapPosition.x, mapPosition.y, mapPosition.z);

    // radio control runs every present, before the playing-state gate (as before the refactor)
    if (g_audioStream && g_audioStream->IsPlaying())
        g_audioStream->ControlGameRadio();

    // wait for the game to load
    if (!device || CGame::GetState() != 9 || FAILED(device->TestCooperativeLevel()))
        return;

    // init gameplay
    static bool gameplayInitialized = false;
    if (!gameplayInitialized)
    {
        gameplayInitialized = InitializeGameplay(device);
        if (!gameplayInitialized)
            return;
    }

    render::RenderUi(device);
}

DWORD WINAPI InitializeThread(LPVOID)
{
    ZoneScoped;

    // check gta version
    if (!IsGtaSa10Us())
    {
        MessageBoxA(nullptr, "Unsupported GTA version.\r\nSA:MP only supports GTA:SA v1.0 US.", "SA:MP - Version Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // alloc debug-only console
#if defined(_DEBUG)
    AllocConsole();

    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);

    Log("SA:MP build %d, %s %s", CalculateBuildNumber(), __DATE__, __TIME__);
#endif

    // init minhook
    if (MH_Initialize() != MH_OK)
    {
#if defined(_DEBUG)
        Log("failed to initialize MinHook!");
#endif // _DEBUG
        return 1;
    }

    // resolve the user directory before crash reporting and game initialization
    g_sampUserDirectory = GetSampUserDirectory();

    // init exceptions
    exceptions::install();

    // hook gta shutdown
    // 0x53C900 - default gta shutdown function
    if (!game::hooks::InstallShutdownHook())
    {
#if defined(_DEBUG)
        Log("failed to install game shutdown hook!");
#endif // _DEBUG
        return 1;
    }

    // open saa archive with original keys
    if (!g_archive.Open("samp.saa", keys))
    {
#if defined(_DEBUG)
        Log("failed to load samp.saa!");
#endif // _DEBUG
        return 1;
    }

    // hook filesystem
    if (!filesystem::install())
    {
#if defined(_DEBUG)
        Log("failed to install filesystem!");
#endif // _DEBUG
        return 1;
    }

    // install patches
    if (!game::patch::install())
    {
#if defined(_DEBUG)
        Log("failed to apply patches!");
#endif // _DEBUG
        return 1;
    }

    // init game loop hock
    if (!game::hooks::InstallGameLoopHook())
    {
#if defined(_DEBUG)
        Log("failed to init game loop hook!");
#endif // _DEBUG
        return 1;
    }

    // init script engine
    if (!scm::install())
    {
#if defined(_DEBUG)
        Log("failed to install SCM opcodes!");
#endif // _DEBUG
        return 1;
    }

    // hook gxt
    if (!game::hooks::InstallMenuTextHook())
    {
#if defined(_DEBUG)
        Log("failed to install menu text hook!");
#endif // _DEBUG
        return 1;
    }

    // init building removal
    if (!game::hooks::InstallBuildingRemovalHooks())
    {
#if defined(_DEBUG)
        Log("failed to install building removal hook!");
#endif // _DEBUG
        return 1;
    }

    // initialize cgame before frontend state 5
    g_game = std::make_unique<CGame>();
    if (!g_game || !g_game->InitGame())
    {
#if defined(_DEBUG)
        Log("failed to initialize CGame!");
#endif // _DEBUG
        return 1;
    }

    if (!game::hooks::InstallCameraHook())
    {
#if defined(_DEBUG)
        Log("failed to install GTA camera process hook!");
#endif // _DEBUG
        return 1;
    }

    // starts frontend initialization after the filesystem is ready
    // ------------------------------------------------------------
    CGame::SetState(5);

    // skip main menu (from r5 disasm)
    while (CGame::GetState() != 7)
        Sleep(5);

    // init settings & config
    // ------------------------------------------------------------

    g_settings.Initialize();

    const auto& userDirectory = g_sampUserDirectory;
    if (userDirectory.empty() || !g_config.Load((userDirectory + "sa-mp.cfg").c_str()))
    {
#if defined(_DEBUG)
        Log("failed to load sa-mp.cfg!");
#endif // _DEBUG
        return 1;
    }

    bool noHudScaleFix = false;
    g_config.GetParam(CConfig::PARAM_NO_HUD_SCALE_FIX, noHudScaleFix);
    game::patch::hudscalefix::set_enabled(!noHudScaleFix);

    bool multicore = true;
    g_config.GetParam(CConfig::PARAM_MULTICORE, multicore);
    if (!multicore && !SetProcessAffinityMask(GetCurrentProcess(), 1))
    {
#if defined(_DEBUG)
        Log("[config] failed to apply multicore=0, error=%lu", GetLastError());
#endif // _DEBUG
    }

    // init d3d9 hook
    // ------------------------------------------------------------
    if (!d3d9hook::install())
    {
#if defined(_DEBUG)
        Log("failed to install d3d9 hooks!");
#endif // _DEBUG
        return 1;
    }

    // r5 enters state 8 after game-side startup
    if (!g_game->StartGame())
    {
#if defined(_DEBUG)
        Log("failed to start CGame!");
#endif // _DEBUG
        return 1;
    }

    return 0;
}
