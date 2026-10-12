#include <stdafx.h>
#include "patch/hooks.h"

#include "CGame.h"
#include "PlayerSlots.h"
#include "CPad.h"

#include "CAudio.h"
#include "CCamera.h"
#include "patch/FrameLimiter.h"
#include "Cursor.h"
#include "config/CConfig.h"
#include "main.h"
#include "ModelInfo.h"
#include "Scripting.h"
#include "entities/CObject.h"
#include "entities/CPed.h"
#include "entities/CVehicle.h"
#include "render/render.h"
#include "game/RenderWare/RenderWare.h"
#include "sync/AimStuff.h"

#include <cstdlib>
#include <cmath>
#include <memory>
#include <new>

namespace
{
    constexpr std::size_t kUsedPlayerSlots = 210;
    constexpr int kFirstVehicleModel = 400;
    constexpr int kVehicleModelCount = 212;
    constexpr int kMaxRequiredVehicleModels = 80;

    char* g_gameTextMessage = nullptr;
    std::array<BOOL, kUsedPlayerSlots> g_usedPlayerSlots{};
    std::array<GTAREF, 32> g_mapIcons{};
    float g_farClippingPlane = 1250.0f;

    constexpr scm::OpcodeInfo REQUEST_MODEL{0x0247, "i"};
    constexpr scm::OpcodeInfo LOAD_REQUESTED_MODELS{0x038B, ""};
    constexpr scm::OpcodeInfo MODEL_AVAILABLE{0x0248, "i"};
    constexpr scm::OpcodeInfo RELEASE_MODEL{0x0249, "i"};
    constexpr scm::OpcodeInfo SET_TIME{0x00C0, "ii"};
    constexpr scm::OpcodeInfo TOGGLE_WIDESCREEN{0x02A3, "i"};
    constexpr scm::OpcodeInfo CREATE_EXPLOSION{0x0948, "fffif"};
    constexpr scm::OpcodeInfo TRAIN_TRAFFIC{0x06D7, "i"};
    constexpr scm::OpcodeInfo REFRESH_RENDERER{0x04E4, "ff"};
    constexpr scm::OpcodeInfo REQUEST_ANIMATION{0x04ED, "s"};
    constexpr scm::OpcodeInfo ANIMATION_LOADED{0x04EE, "s"};
    constexpr scm::OpcodeInfo RELEASE_ANIMATION{0x04EF, "s"};
    constexpr scm::OpcodeInfo CLEAR_GAME_TEXT{0x00BE, ""};
    constexpr scm::OpcodeInfo GROUND_Z{0x02CE, "fffv"};
    constexpr scm::OpcodeInfo CREATE_MARKER{0x04CE, "fffiv"};
    constexpr scm::OpcodeInfo CREATE_MARKER_SPHERE{0x02A7, "fffiv"};
    constexpr scm::OpcodeInfo CREATE_MARKER_NO_SPHERE{0x02A8, "fffiv"};
    constexpr scm::OpcodeInfo CREATE_MARKER_ALT{0x0570, "fffiv"};
    constexpr scm::OpcodeInfo DELETE_MARKER{0x0164, "i"};
    constexpr scm::OpcodeInfo SET_MARKER_COLOR{0x0165, "ii"};
    constexpr scm::OpcodeInfo SET_MARKER_SCALE{0x0168, "ii"};
    constexpr scm::OpcodeInfo GET_INTERIOR{0x077E, "v"};
    constexpr scm::OpcodeInfo ADD_MONEY{0x0109, "ii"};
    constexpr scm::OpcodeInfo CREATE_PICKUP{0x0213, "iifffv"};
    constexpr scm::OpcodeInfo CREATE_PICKUP_WITH_AMMO{0x032B, "iiifffv"};
    constexpr scm::OpcodeInfo ZONE_DISPLAY{0x09BA, "i"};
    constexpr scm::OpcodeInfo CREATE_RACING_CHECKPOINT{0x06D5, "ifffffffv"};
    constexpr scm::OpcodeInfo DELETE_RACING_CHECKPOINT{0x06D6, "i"};
    constexpr scm::OpcodeInfo IS_PED_NEAR_POINT{0x00FE, "iffffffi"};

    constexpr std::array<const char*, 9> kRequiredAnimations{"PARACHUTE", "BAR", "GANGS", "PAULNMAC", "VENDING", "FIGHT_B", "FIGHT_C", "FIGHT_D", "FIGHT_E"};
    constexpr std::array<int, 9> kRequiredModels{3131, 1485, 1486, 1543, 1546, 343, 342, 344, 330};

    template <class T>
    T& Mem(std::uintptr_t address)
    {
        return *reinterpret_cast<T*>(address);
    }

    void SanitizeGameText(char* text)
    {
        ZoneScoped;
        if (!text)
            return;

        for (; *text; ++text)
        {
            const auto c = static_cast<unsigned char>(*text);
            if (c >= 1 && c < 0x20 && c != '\n')
                *text = ' ';
        }
    }
}

BOOL* CGame::ArrayUsedPlayerSlots()
{
    return g_usedPlayerSlots.data();
}

CGame::CGame() : m_pAudio(nullptr), m_pCamera(nullptr), m_pPlayerPed(nullptr)
{
    ZoneScoped;
    auto audio = std::make_unique<CAudio>();
    auto camera = std::make_unique<CCamera>();
    m_pAudio = audio.release();
    m_pCamera = camera.release();
    m_racingCheckpoint.m_bEnabled = FALSE;
    m_racingCheckpoint.m_handle = 0;
    m_checkpoint.m_bEnabled = FALSE;
    m_nCursorMode = CURSOR_NONE;
    m_nInputEnableWaitFrames = 0;
    m_bClockEnabled = FALSE;
    field_6d = 0;
    g_usedPlayerSlots.fill(FALSE);
    std::fill_n(m_aKeepLoadedVehicleModels, kVehicleModelCount, false);
    field_61 = 0;
    m_bHeadMove = TRUE;
    m_nFrameLimiter = 90;
}

CPed* CGame::GetPlayerPed()
{
    if (!m_pPlayerPed && reinterpret_cast<void*(__cdecl*)(int)>(0x56E210)(-1))
        m_pPlayerPed = new (std::nothrow) CPed();
    if (m_pPlayerPed)
        m_pPlayerPed->ResetPointers();
    return m_pPlayerPed;
}

CGame::~CGame()
{
    ResetMapIcons();
    delete m_pPlayerPed;
    delete m_pCamera;
    delete m_pAudio;
    std::free(g_gameTextMessage);
    g_gameTextMessage = nullptr;
}

float CGame::FindGroundZ(CVector point)
{
    float result = 0.0f;
    scm::ProcessCommand(&GROUND_Z, point.x, point.y, point.z, &result);
    return result;
}

void CGame::SetCursorMode(int mode, BOOL immediatelyHideCursor)
{
    ZoneScoped;
    if (mode < CURSOR_NONE || mode > CURSOR_LOCKCAM_NOCURSOR)
        return;

    if (mode == CURSOR_NONE)
    {
        m_nCursorMode = CURSOR_NONE;
        m_nInputEnableWaitFrames = immediatelyHideCursor ? 0u : 10u;
        return;
    }

    m_nCursorMode = mode;
    m_nInputEnableWaitFrames = 0;
    game::cursor::SetMode(mode);
}

bool CGame::InitGame()
{
    ZoneScoped;
    if (m_bInitialized)
        return true;

    if (!g_gameTextMessage)
        g_gameTextMessage = static_cast<char*>(std::calloc(1, 0x201));
    if (!g_gameTextMessage)
        return false;

    // the r5 private buffers and loader checks are handled by our bootstrap
    m_bInitialized = true;
    return true;
}

bool CGame::StartGame()
{
    ZoneScoped;
    if (m_bGameStarted)
        return true;
    if (!m_bInitialized || GetState() != 7)
        return false;

    // base patches already ran before the frontend; install game hooks once
    if (!CObject::InstallRenderHook() || !game::hooks::InstallRenderHooks())
        return false;

    int limit = 48;
    g_config.GetParam(CConfig::PARAM_FPS_LIMIT, limit);
    SetFrameLimiter(limit);
    if (!game::patch::fps::install())
        return false;

    // resource streaming must wait until the first gameplay frame
    Mem<std::uint32_t>(0xC8D4C0) = 8;
    Mem<std::uint8_t>(0xBA6831) = 1;
    Mem<std::uint8_t>(0xBA67A4) = 0;
    Mem<std::uint8_t>(0xBA677B) = 0;
    m_bGameStarted = true;
    return true;
}

void CGame::OnGameplayReady()
{
    ZoneScoped;
    if (m_bGameplayReady || !m_bGameStarted || !IsPlaying())
        return;

    if (!m_bGameplayAssetsRequested)
    {
        AimStuff::Initialize();
        AimStuff::ResetPlayerStats();
        bool disableHeadMove = false;
        g_config.GetParam(CConfig::PARAM_DISABLE_HEAD_MOVE, disableHeadMove);
        SetHeadMoves(disableHeadMove ? FALSE : TRUE);
        LoadAnimationsAndModels();
        m_bGameplayAssetsRequested = true;
    }

    for (const char* animation : kRequiredAnimations)
        if (!IsAnimationLoaded(animation))
            return;

    // ignore missing model entries while checking readiness
    for (int model : kRequiredModels)
        if (ModelInfo::GetRelocated(model) && !IsModelAvailable(model))
            return;

    m_bGameplayReady = true;
}

BOOL CGame::IsMenuVisible()
{
    return Mem<std::uint8_t>(0xBA67A4) != 0;
}

BOOL CGame::IsStarted()
{
    return Mem<std::uint8_t>(0xBA6831) == 0;
}

void CGame::RequestModel(int model, int)
{
    scm::ProcessCommand(&REQUEST_MODEL, model);
}

void CGame::LoadRequestedModels()
{
    scm::ProcessCommand(&LOAD_REQUESTED_MODELS);
}

BOOL CGame::IsModelAvailable(int model)
{
    if (model < 0 || model >= ModelInfo::MAX_MODELS)
        return FALSE;
    return scm::ProcessCommand(&MODEL_AVAILABLE, model) != 0;
}

void CGame::ReleaseModel(int model, bool gameFunc)
{
    ZoneScoped;
    // R5 sends negative IDs through its custom-model subsystem. That subsystem is not
    // implemented in this tree yet, so keep those IDs untouched until CustomModels is wired.
    if (model < 0)
        return;

    if (model >= ModelInfo::MAX_MODELS || !IsModelAvailable(model))
        return;

    if (gameFunc)
        reinterpret_cast<void(__cdecl*)(int)>(0x4089A0)(model);
    else
        scm::ProcessCommand(&RELEASE_MODEL, model);
}

void CGame::SetWorldTime(char hour, char minute)
{
    Mem<std::uint8_t>(0xB70152) = static_cast<std::uint8_t>(minute);
    Mem<std::uint8_t>(0xB70153) = static_cast<std::uint8_t>(hour);
    scm::ProcessCommand(&SET_TIME, static_cast<int>(hour), static_cast<int>(minute));
}

void CGame::GetWorldTime(char* hour, char* minute)
{
    if (minute)
        *minute = static_cast<char>(Mem<std::uint8_t>(0xB70152));
    if (hour)
        *hour = static_cast<char>(Mem<std::uint8_t>(0xB70153));
}

void CGame::LetTimeGo(bool let)
{
    const std::uint8_t opcode = let ? 0x56 : 0xC3;
    patch::write(0x52CF10, opcode);
    m_bClockEnabled = let ? TRUE : FALSE;
}

void CGame::CreateExplosion(CVector position, int type, float radius)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(radius))
        return;

    scm::ProcessCommand(&CREATE_EXPLOSION, position.x, position.y, position.z, type, radius);
}

void CGame::SetWorldWeather(char weather)
{
    ZoneScoped;
    const auto value = static_cast<std::int32_t>(weather);
    Mem<std::int32_t>(0xC81318) = value;
    if (!m_bClockEnabled)
    {
        Mem<std::int32_t>(0xC8131C) = value;
        Mem<std::int32_t>(0xC81320) = value;
    }
}

void CGame::SetFrameLimiter(int value)
{
    patch::write<std::int32_t>(0xC1704C, 200);
    game::patch::fps::set_limit(static_cast<std::uint32_t>(value));
    m_nFrameLimiter = static_cast<int>(game::patch::fps::get_limit());
}

void CGame::SetMaxStats()
{
    // r5 0x100a0be0: gta stat setup and decay patch
    reinterpret_cast<void(__cdecl*)()>(0x4399D0)();
    reinterpret_cast<void(__cdecl*)()>(0x439940)();
    patch::write<std::uint8_t>(0x55A070, 0xC3);
}

void CGame::DisableTrainTraffic()
{
    scm::ProcessCommand(&TRAIN_TRAFFIC, 0);
}

void CGame::RefreshRenderer(float x, float y)
{
    ZoneScoped;
    scm::ProcessCommand(&REFRESH_RENDERER, x, y);
}

void CGame::RequestAnimation(const char* file)
{
    if (file)
        scm::ProcessCommand(&REQUEST_ANIMATION, file);
}

BOOL CGame::IsAnimationLoaded(const char* file)
{
    return file && scm::ProcessCommand(&ANIMATION_LOADED, file) != 0;
}

void CGame::ReleaseAnimation(const char* file)
{
    if (file && IsAnimationLoaded(file))
        scm::ProcessCommand(&RELEASE_ANIMATION, file);
}

void CGame::DisplayGameText(const char* text, int type, int size)
{
    ZoneScoped;
    if (!text || size > 200)
        return;

    if (!g_gameTextMessage && !InitGame())
        return;

    scm::ProcessCommand(&CLEAR_GAME_TEXT);
    g_gameTextMessage[0] = '\0';
    std::strncpy(g_gameTextMessage, text, 0x200);
    g_gameTextMessage[0x200] = '\0';
    SanitizeGameText(g_gameTextMessage);
    reinterpret_cast<void(__cdecl*)(const char*, int, int)>(0x69F2B0)(g_gameTextMessage, type, size);
}

void CGame::DeleteRacingCheckpoint()
{
    if (m_racingCheckpoint.m_handle)
    {
        scm::ProcessCommand(&DELETE_RACING_CHECKPOINT, m_racingCheckpoint.m_handle);
        m_racingCheckpoint.m_handle = 0;
    }
    m_racingCheckpoint.m_bEnabled = FALSE;
}

GTAREF CGame::CreateMarker(int icon, CVector position, int color, int type)
{
    ZoneScoped;
    GTAREF handle = 0;
    const scm::OpcodeInfo* opcode = &CREATE_MARKER;
    if (type == 1)
        opcode = &CREATE_MARKER_NO_SPHERE;
    else if (type == 2)
        opcode = &CREATE_MARKER_ALT;
    else if (type == 3)
        opcode = &CREATE_MARKER_SPHERE;

    scm::ProcessCommand(opcode, position.x, position.y, position.z, icon, &handle);

    if (icon == 0)
    {
        scm::ProcessCommand(&SET_MARKER_COLOR, handle, color);
        scm::ProcessCommand(&SET_MARKER_SCALE, handle, color < 1004 ? 2 : 3);
    }
    return handle;
}

bool CGame::SetMarkerPosition(GTAREF handle, const CVector& position)
{
    ZoneScoped;
    if (!handle)
        return false;

    // gta sa 1.0 us: CRadar::GetActualBlipArrayIndex, ms_RadarTrace
    const int index = reinterpret_cast<int(__cdecl*)(int)>(0x582870)(static_cast<int>(handle));
    if (index < 0 || index >= 175)
        return false;

    constexpr std::uintptr_t traces = 0xBA86F0;
    constexpr std::uintptr_t stride = 0x28;
    auto* coords = reinterpret_cast<float*>(traces + static_cast<std::uintptr_t>(index) * stride + 8);
    coords[0] = position.x;
    coords[1] = position.y;
    coords[2] = position.z;
    return true;
}

void CGame::DeleteMarker(GTAREF handle)
{
    if (handle)
        scm::ProcessCommand(&DELETE_MARKER, handle);
}

char CGame::GetCurrentInterior()
{
    std::uint32_t interior = 0;
    scm::ProcessCommand(&GET_INTERIOR, &interior);
    return static_cast<char>(interior);
}

void CGame::UpdateFarClippingPlane()
{
    ZoneScoped;
    float distance = 1250.0f;
    auto* player = static_cast<const std::uint8_t*>(CPed::GetLocalPedHandle());
    if (player)
    {
        if (GetCurrentInterior() != 0)
            distance = 400.0f;
        else
        {
            const auto* matrix = *reinterpret_cast<const std::uint8_t* const*>(player + 0x14);
            if (matrix)
            {
                const float z = *reinterpret_cast<const float*>(matrix + 0x38);
                if (std::isfinite(z))
                    distance = std::max(700.0f, 1250.0f - z * 2.0f);
            }
        }
    }
    g_farClippingPlane = distance;

    // r5 stores this for its render hook; update gta's camera directly
    if (auto* camera = *reinterpret_cast<RwCamera**>(0xC1703C))
        RwCameraSetFarClipPlane(camera, distance);
}

void CGame::IncreasePlayerMoney(int increment)
{
    scm::ProcessCommand(&ADD_MONEY, 0, increment);
}

int CGame::GetPlayerMoney()
{
    return Mem<int>(0xB7CE50);
}

void CGame::PlayCrimeReport(int crime, const CVector* position, int inVehicle, int vehicleModel, int vehicleColor)
{
    ZoneScoped;
    if (!position || crime < 2 || crime > 22)
        return;

    auto* ped = static_cast<std::uint8_t*>(CPed::GetLocalPedHandle());
    if (!ped)
        return;

    auto* playerData = *reinterpret_cast<void**>(ped + 0x480);
    if (!playerData)
        return;

    auto* wanted = *reinterpret_cast<void**>(playerData);
    if (!wanted)
        return;

    // R5 supplies a temporary ped so the scanner describes the RPC vehicle,
    // independently of the vehicle currently occupied by the local player.
    alignas(16) std::array<std::uint8_t, 0x7A0> reportPed{};
    std::unique_ptr<CVehicle> vehicle;
    if (inVehicle && vehicleModel >= 400 && vehicleModel < 612)
    {
        vehicle = std::make_unique<CVehicle>(vehicleModel, CVector{}, 0.0f, FALSE, FALSE);
        if (vehicle->HasNativeVehicle())
        {
            vehicle->SetColor(static_cast<unsigned char>(vehicleColor), 255);
            vehicle->Remove();
            auto* nativeVehicle = vehicle->GetNativeVehicleHandle();
            std::memcpy(reportPed.data() + 0x58C, &nativeVehicle, sizeof(nativeVehicle));
            const std::uint32_t inVehicleFlag = 0x100;
            std::memcpy(reportPed.data() + 0x46C, &inVehicleFlag, sizeof(inVehicleFlag));
        }
    }

    constexpr std::uintptr_t scannerPatch = 0x4E7529;
    constexpr std::uintptr_t pedCall = 0x4E7427;
    std::array<std::uint8_t, 39> original{};
    std::array<std::uint8_t, 5> originalCall{};
    if (!patch::read(scannerPatch, original.data(), original.size()) || !patch::read(pedCall, originalCall.data(), originalCall.size()))
        return;
    if (!patch::nop(scannerPatch, original.size()))
        return;
    if (!patch::call(pedCall, reinterpret_cast<const void*>(&CrimeReportPedHook)))
    {
        patch::write(scannerPatch, original.data(), original.size());
        return;
    }

    game::hooks::crimeReportPed = reportPed.data();
    auto* scanner = static_cast<std::uint8_t*>(wanted) + 0x21C;
    reinterpret_cast<void(__thiscall*)(void*, int, int, const CVector*)>(0x4E71E0)(scanner, 0xA4, crime, position);
    patch::write(pedCall, originalCall.data(), originalCall.size());
    game::hooks::crimeReportPed = nullptr;
    patch::write(scannerPatch, original.data(), original.size());
}

const char* CGame::GetWeaponName(int weapon)
{
    ZoneScoped;
    switch (weapon)
    {
        case 1:
            return "Brass Knuckles";
        case 2:
            return "Golf Club";
        case 3:
            return "Nite Stick";
        case 4:
            return "Knife";
        case 5:
            return "Baseball Bat";
        case 6:
            return "Shovel";
        case 7:
            return "Pool Cue";
        case 8:
            return "Katana";
        case 9:
            return "Chainsaw";
        case 10:
            return "Dildo";
        case 11:
            return "Dildo";
        case 12:
            return "Vibrator";
        case 13:
            return "Vibrator";
        case 14:
            return "Flowers";
        case 15:
            return "Cane";
        case 16:
            return "Grenade";
        case 17:
            return "Teargas";
        case 18:
            return "Molotov";
        case 22:
            return "Colt 45";
        case 23:
            return "Silenced Pistol";
        case 24:
            return "Desert Eagle";
        case 25:
            return "Shotgun";
        case 26:
            return "Sawn-off Shotgun";
        case 27:
            return "Combat Shotgun";
        case 28:
            return "UZI";
        case 29:
            return "MP5";
        case 30:
            return "AK47";
        case 31:
            return "M4";
        case 32:
            return "TEC9";
        case 33:
            return "Rifle";
        case 34:
            return "Sniper Rifle";
        case 35:
            return "Rocket Launcher";
        case 36:
            return "Heat Seaker";
        case 37:
            return "Flamethrower";
        case 38:
            return "Minigun";
        case 39:
            return "Satchel Explosives";
        case 40:
            return "Bomb";
        case 41:
            return "Spray Can";
        case 42:
            return "Fire Extinguisher";
        case 43:
            return "Camera";
        case 46:
            return "Parachute";
        case 49:
            return "Vehicle";
        case 53:
            return "Drowned";
        case 54:
            return "Splat";
        default:
            return "";
    }
}

GTAREF CGame::CreateWeaponPickup(int model, int ammo, CVector position)
{
    ZoneScoped;
    if (model < 0 || model >= ModelInfo::MAX_MODELS)
        return 0;
    if (!ModelInfo::GetRelocated(model))
        model = 18631;

    if (!IsModelAvailable(model))
    {
        RequestModel(model);
        LoadRequestedModels();

        if (!IsModelAvailable(model))
            return 0;
    }

    // Type 4 is the one-shot weapon pickup used by SA-MP for dropped weapons.
    GTAREF rawHandle = 0;
    scm::ProcessCommand(&CREATE_PICKUP_WITH_AMMO, model, 4, ammo, position.x, position.y, position.z, &rawHandle);

    // Same contract as CreatePickup(): the raw GTA pickup reference is returned.
    return rawHandle;
}

::CWeaponInfo* CGame::GetWeaponInfo(int weapon, int skill)
{
    using GetWeaponInfoFn = ::CWeaponInfo*(__cdecl*)(int, int);
    return reinterpret_cast<GetWeaponInfoFn>(0x743C60)(weapon, skill);
}

GTAREF CGame::CreatePickup(int model, int type, CVector position, GTAREF* handle)
{
    ZoneScoped;
    if (handle)
        *handle = 0;
    if (model < 0 || model >= ModelInfo::MAX_MODELS)
        return 0;
    if (model >= 0 && model < ModelInfo::MAX_MODELS && !ModelInfo::GetRelocated(model))
        model = 18631;

    if (model >= 0 && model < ModelInfo::MAX_MODELS && !IsModelAvailable(model))
    {
        RequestModel(model);
        LoadRequestedModels();

        if (!IsModelAvailable(model))
            return 0;
    }

    GTAREF rawHandle = 0;
    scm::ProcessCommand(&CREATE_PICKUP, model, type, position.x, position.y, position.z, &rawHandle);

    if (handle)
        *handle = rawHandle & 0xFFFFu;

    // R5 leaves the raw GTA pickup reference in EAX. CPickupPool relies on
    // this return value even though older public headers declared this method void.
    return rawHandle;
}

IDirect3DDevice9* CGame::GetDevice()
{
    return GetD3D9Device();
}

void CGame::Restart()
{
    ZoneScoped;
    DisableCheckpoint();
    DeleteRacingCheckpoint();
    ResetMapIcons();
    Mem<std::uint8_t>(0xBA67A5) = 1;
    Mem<std::uint8_t>(0xBA67A6) = 1;
    reinterpret_cast<void(__cdecl*)()>(0x53C550)();
    reinterpret_cast<void(__cdecl*)()>(0x561AA0)();
    reinterpret_cast<void(__cdecl*)()>(0x53C680)();
    Mem<std::uint8_t>(0xBA6831) = 1;
}

void CGame::SetWorldGravity(float value)
{
    patch::write(0x863984, value);
}

void CGame::SetWantedLevel(char level)
{
    patch::write<std::uint8_t>(0x58DB60, static_cast<std::uint8_t>(level));
}

void CGame::SetNumberOfIntroTextLinesThisFrame(unsigned short value)
{
    Mem<std::uint16_t>(0xA44B68) = value;
}

void CGame::DrawGangZone(float* position, std::uint32_t color)
{
    ZoneScoped;
    if (!position)
        return;
    const int menuVisible = Mem<std::uint8_t>(0xBA67A4);
    reinterpret_cast<void(__cdecl*)(float*, const std::uint32_t*, bool)>(0x5853D0)(position, &color, menuVisible != 0);
}

void CGame::EnableZoneDisplaying(bool enable)
{
    scm::ProcessCommand(&ZONE_DISPLAY, enable ? 1 : 0);
}

void CGame::EnableStuntBonus(bool enable)
{
    patch::write<std::uint32_t>(0xA4A474, enable ? 1u : 0u);
}

void CGame::LoadScene(const char* file)
{
    if (file)
        reinterpret_cast<void(__cdecl*)(const char*)>(0x5B8700)(file);
}

int CGame::GetUsedMemory()
{
    return Mem<int>(0x8E4CB4);
}

int CGame::GetStreamingMemory()
{
    return Mem<int>(0x8A5A80);
}

void CGame::SetRequiredVehicleModels(unsigned char* modelCount)
{
    ZoneScoped;
    std::fill(std::begin(m_aKeepLoadedVehicleModels), std::end(m_aKeepLoadedVehicleModels), false);
    if (!modelCount)
        return;

    for (int model = kFirstVehicleModel; model < kFirstVehicleModel + kVehicleModelCount; ++model)
        if (IsModelAvailable(model))
            scm::ProcessCommand(&RELEASE_MODEL, model);

    std::array<int, kVehicleModelCount> models{};
    for (int i = 0; i < kVehicleModelCount; ++i)
        models[i] = kFirstVehicleModel + i;

    for (int end = kVehicleModelCount - 1; end > 0; --end)
    {
        for (int i = 0; i < end; ++i)
        {
            if (modelCount[i + 1] > modelCount[i])
            {
                std::swap(modelCount[i], modelCount[i + 1]);
                std::swap(models[i], models[i + 1]);
            }
        }
    }

    for (int i = 0; i < kMaxRequiredVehicleModels; ++i)
    {
        if (!modelCount[i])
            continue;
        const int model = models[i];
        RequestModel(model);
        LoadRequestedModels();
        m_aKeepLoadedVehicleModels[model - kFirstVehicleModel] = true;
    }
}

int CGame::GetTimer()
{
    return Mem<int>(0xB7CB84);
}

void CGame::SetTimer(int value)
{
    if (m_bClockEnabled)
        return;
    Mem<std::uint32_t>(0xB7CB84) = static_cast<std::uint32_t>(value) & 0x3FFFFFFFu;
}

void CGame::SetMapIcon(std::uint8_t id, CVector position, int icon, int color, int type)
{
    if (id >= g_mapIcons.size())
        return;

    RemoveMapIcon(id);
    g_mapIcons[id] = CreateMarker(icon, position, color, type);
}

void CGame::RemoveMapIcon(std::uint8_t id)
{
    if (id >= g_mapIcons.size() || !g_mapIcons[id])
        return;

    DeleteMarker(g_mapIcons[id]);
    g_mapIcons[id] = 0;
}

void CGame::ResetMapIcons()
{
    for (std::size_t id = 0; id < g_mapIcons.size(); ++id)
        RemoveMapIcon(static_cast<std::uint8_t>(id));
}

void CGame::LoadAnimationsAndModels()
{
    ZoneScoped;
    if (!IsPlaying())
        return;

    bool requested = false;
    for (const char* animation : kRequiredAnimations)
    {
        if (!IsAnimationLoaded(animation))
        {
            RequestAnimation(animation);
            requested = true;
        }
    }

    // r5 0x100a1a08..0x100a1b70: request the models without blocking on absent entries
    for (int model : kRequiredModels)
    {
        if (ModelInfo::GetRelocated(model) && !IsModelAvailable(model))
        {
            RequestModel(model);
            requested = true;
        }
    }

    if (requested)
        LoadRequestedModels();
}

void CGame::LoadCollisionFile(const char* file)
{
    if (file)
        reinterpret_cast<void(__cdecl*)(const char*, int)>(0x5B4E60)(file, 0);
}

void CGame::LoadCullZone(const char* line)
{
    if (line)
        reinterpret_cast<void(__cdecl*)(const char*)>(0x5B4B40)(line);
}

BOOL CGame::UsingGamepad()
{
    return Mem<std::uint8_t>(0xBA6818) != 0;
}

void CGame::DisableAutoAiming()
{
    const std::uint8_t ret = 0xC3;
    const std::array<std::uint8_t, 3> ret8{0xC2, 0x08, 0x00};
    patch::write(0x60DC50, ret);
    patch::write(0x60E530, ret8.data(), ret8.size());
}

void CGame::EnableHUD(BOOL enable)
{
    ZoneScoped;
    if (enable)
    {
        Mem<std::uint8_t>(0xBA6769) = 1;
        Mem<std::uint8_t>(0xBAA3FB) = 0;
    }
    else
    {
        Mem<std::uint8_t>(0xBA6769) = 0;
        Mem<std::uint8_t>(0xBAA3FB) = 1;
    }
}

void CGame::SetCheckpoint(CVector* position, CVector* size)
{
    ZoneScoped;
    if (!position || !size)
        return;

    m_checkpoint.m_position = *position;
    m_checkpoint.m_size = *size;
    m_checkpoint.m_bEnabled = TRUE;

    if (m_checkpoint.m_handle)
    {
        DeleteMarker(m_checkpoint.m_handle);
        m_checkpoint.m_handle = 0;

        scm::ProcessCommand(&CREATE_MARKER, m_checkpoint.m_position.x, m_checkpoint.m_position.y, m_checkpoint.m_position.z, 0, &m_checkpoint.m_handle);
        scm::ProcessCommand(&SET_MARKER_COLOR, m_checkpoint.m_handle, static_cast<int>(0xAA0000FFu));
        scm::ProcessCommand(&SET_MARKER_SCALE, m_checkpoint.m_handle, 3);
    }
}

void CGame::DisableCheckpoint()
{
    m_checkpoint.m_bEnabled = FALSE;
    if (!m_checkpoint.m_handle)
        return;

    DeleteMarker(m_checkpoint.m_handle);
    m_checkpoint.m_handle = 0;
}

void CGame::CreateRacingCheckpoint()
{
    ZoneScoped;
    if (m_racingCheckpoint.m_handle)
    {
        scm::ProcessCommand(&DELETE_RACING_CHECKPOINT, m_racingCheckpoint.m_handle);
        m_racingCheckpoint.m_handle = 0;
    }

    m_racingCheckpoint.m_bEnabled = FALSE;
    scm::ProcessCommand(&CREATE_RACING_CHECKPOINT, static_cast<int>(m_racingCheckpoint.m_nType), m_racingCheckpoint.m_currentPosition.x, m_racingCheckpoint.m_currentPosition.y,
                        m_racingCheckpoint.m_currentPosition.z, m_racingCheckpoint.m_nextPosition.x, m_racingCheckpoint.m_nextPosition.y, m_racingCheckpoint.m_nextPosition.z,
                        m_racingCheckpoint.m_fSize, &m_racingCheckpoint.m_handle);
    m_racingCheckpoint.m_bEnabled = TRUE;
}

void CGame::ProcessCheckpoints()
{
    ZoneScoped;
    auto* player = GetPlayerPed();
    const bool hasPlayer = player && player->IsValid() && player->m_handle;
    if (m_checkpoint.m_bEnabled)
    {
        if (hasPlayer)
            scm::ProcessCommand(&IS_PED_NEAR_POINT, player->m_handle, m_checkpoint.m_position.x, m_checkpoint.m_position.y, m_checkpoint.m_position.z, m_checkpoint.m_size.x,
                                m_checkpoint.m_size.y, m_checkpoint.m_size.z, 1);
        if (!m_checkpoint.m_handle && hasPlayer)
        {
            scm::ProcessCommand(&CREATE_MARKER, m_checkpoint.m_position.x, m_checkpoint.m_position.y, m_checkpoint.m_position.z, 0, &m_checkpoint.m_handle);
            scm::ProcessCommand(&SET_MARKER_COLOR, m_checkpoint.m_handle, static_cast<int>(0xAA0000FFu));
            scm::ProcessCommand(&SET_MARKER_SCALE, m_checkpoint.m_handle, 3);
        }
    }
    else if (m_checkpoint.m_handle)
    {
        DeleteMarker(m_checkpoint.m_handle);
        m_checkpoint.m_handle = 0;
    }

    if (m_racingCheckpoint.m_bEnabled)
    {
        if (!m_racingCheckpoint.m_marker && hasPlayer)
        {
            scm::ProcessCommand(&CREATE_MARKER, m_racingCheckpoint.m_currentPosition.x, m_racingCheckpoint.m_currentPosition.y, m_racingCheckpoint.m_currentPosition.z, 0,
                                &m_racingCheckpoint.m_marker);
            scm::ProcessCommand(&SET_MARKER_COLOR, m_racingCheckpoint.m_marker, static_cast<int>(0xAA0000FFu));
            scm::ProcessCommand(&SET_MARKER_SCALE, m_racingCheckpoint.m_marker, 3);
        }
    }
    else
    {
        if (m_racingCheckpoint.m_marker)
        {
            DeleteMarker(m_racingCheckpoint.m_marker);
            m_racingCheckpoint.m_marker = 0;
        }
        if (m_racingCheckpoint.m_handle)
        {
            scm::ProcessCommand(&DELETE_RACING_CHECKPOINT, m_racingCheckpoint.m_handle);
            m_racingCheckpoint.m_handle = 0;
        }
        m_racingCheckpoint.m_bEnabled = FALSE;
    }
}

void CGame::ResetMoney()
{
    const int money = GetPlayerMoney();
    if (money)
        IncreasePlayerMoney(-money);
}

void CGame::SetRacingCheckpoint(int type, CVector* currentPosition, CVector* nextPosition, float size)
{
    ZoneScoped;
    if (!currentPosition || !nextPosition)
        return;

    m_racingCheckpoint.m_currentPosition = *currentPosition;
    m_racingCheckpoint.m_nextPosition = *nextPosition;
    m_racingCheckpoint.m_fSize = size;
    m_racingCheckpoint.m_nType = static_cast<char>(type);

    if (m_racingCheckpoint.m_marker)
    {
        DeleteMarker(m_racingCheckpoint.m_marker);
        m_racingCheckpoint.m_marker = 0;
        scm::ProcessCommand(&CREATE_MARKER, m_racingCheckpoint.m_currentPosition.x, m_racingCheckpoint.m_currentPosition.y, m_racingCheckpoint.m_currentPosition.z, 0,
                            &m_racingCheckpoint.m_marker);
        scm::ProcessCommand(&SET_MARKER_COLOR, m_racingCheckpoint.m_marker, static_cast<int>(0xAA0000FFu));
        scm::ProcessCommand(&SET_MARKER_SCALE, m_racingCheckpoint.m_marker, 3);
    }

    CreateRacingCheckpoint();
}

void CGame::EnableRadar(BOOL enable)
{
    Mem<std::uint8_t>(0xBAA3FB) = enable ? 0 : 1;
}

void* CGame::GetWindowHandle()
{
    return Mem<HWND>(0xC97C1C);
}

CAudio* CGame::GetAudio()
{
    return m_pAudio;
}

CCamera* CGame::GetCamera()
{
    return m_pCamera;
}

BOOL CGame::DoesHeadMoves()
{
    return m_bHeadMove;
}

void CGame::SetHeadMoves(BOOL enable)
{
    if (m_bHeadMove && !enable)
        if (auto* ped = GetPlayerPed())
            ped->StopLooking();
    m_bHeadMove = enable ? TRUE : FALSE;
}

void CGame::ToggleWidescreen(bool enable)
{
    scm::ProcessCommand(&TOGGLE_WIDESCREEN, enable ? 1 : 0);
}

void CGame::EnableClock(bool enable)
{
    ZoneScoped;
    static constexpr char kClockFormat[10] = {'%', '0', '2', 'd', ':', '%', '0', '2', 'd', '\0'};
    const std::uint8_t opcode = enable ? 0x56 : 0xC3;
    patch::write(0x52CF10, opcode);
    m_bClockEnabled = enable ? TRUE : FALSE;

    if (enable)
        patch::write(0x859A6C, kClockFormat, sizeof(kClockFormat));
    else
    {
        const std::array<std::uint8_t, 10> zero{};
        patch::write(0x859A6C, zero.data(), zero.size());
    }
}

void CGame::Sleep(int elapsed, int fpsLimit)
{
    if (elapsed <= 0 || fpsLimit <= 0)
        return;

    const int frameTime = 1000 / fpsLimit;
    if (elapsed < frameTime)
        ::Sleep(static_cast<DWORD>(frameTime - elapsed - 1));
}

CPed* CGame::CreatePed(int model, CVector position, float rotation, int, int)
{
    ZoneScoped;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(rotation) || model < 0 || model >= ModelInfo::MAX_MODELS)
        return nullptr;
    auto* info = ModelInfo::GetRelocated(model);
    if (!info || *static_cast<std::uintptr_t*>(info) != 0x85BDC0)
        return nullptr;
    if (!IsModelAvailable(model))
    {
        RequestModel(model);
        LoadRequestedModels();
        if (!IsModelAvailable(model))
            return nullptr;
    }
    const int slot = game::players::AcquireSlot();
    if (slot < 0)
        return nullptr;
    auto* ped = new (std::nothrow) CPed(slot, model, position, rotation);
    if (!ped || !ped->DoesExist())
    {
        if (ped)
            ped->Destroy();
        delete ped;
        game::players::ReleaseSlot(static_cast<std::uint8_t>(slot));
        return nullptr;
    }
    return ped;
}

BOOL CGame::RemovePed(CPed* ped)
{
    if (!ped || !ped->m_nPlayerNumber)
        return FALSE;
    ped->Destroy();
    delete ped;
    return TRUE;
}

CVehicle* CGame::CreateVehicle(int model, CVector position, float rotation, BOOL hasSiren)
{
    ZoneScoped;
    if (model < kFirstVehicleModel || model >= kFirstVehicleModel + kVehicleModelCount || !ModelInfo::GetRelocated(model) || !std::isfinite(position.x) ||
        !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(rotation))
        return nullptr;

    const bool keepLoaded = model >= kFirstVehicleModel && model < kFirstVehicleModel + kVehicleModelCount && m_aKeepLoadedVehicleModels[model - kFirstVehicleModel];
    auto* vehicle = new (std::nothrow) CVehicle(model, position, rotation, keepLoaded ? TRUE : FALSE, hasSiren);
    if (vehicle && !vehicle->DoesExist())
    {
        delete vehicle;
        return nullptr;
    }
    return vehicle;
}

CObject* CGame::CreateObject(int model, CVector position, CVector rotation, float drawDistance, char hourOn, char hourOff)
{
    ZoneScoped;
    if (model < 0 || model >= ModelInfo::MAX_MODELS || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(rotation.x) ||
        !std::isfinite(rotation.y) || !std::isfinite(rotation.z) || !std::isfinite(drawDistance))
        return nullptr;

    auto* object = new (std::nothrow) CObject(model, position, rotation, drawDistance, 0, hourOn, hourOff);
    if (object && !object->IsValid())
    {
        delete object;
        return nullptr;
    }
    return object;
}

void CGame::ProcessInputEnabling()
{
    ZoneScoped;
    if (m_nCursorMode != CURSOR_NONE)
        return;

    if (m_nInputEnableWaitFrames != 0)
    {
        if (static_cast<int>(m_nInputEnableWaitFrames) > 0)
            --m_nInputEnableWaitFrames;
        return;
    }

    game::cursor::SetMode(CURSOR_NONE);
    m_nInputEnableWaitFrames = std::numeric_limits<unsigned int>::max();
}

void CGame::ProcessFrameLimiter()
{
    ZoneScoped;
    // game::patch::fps owns pacing and accepts changes from /fpslimit
    m_nFrameLimiter = static_cast<int>(game::patch::fps::get_limit());
}

std::uint8_t CGame::GetState()
{
    return *reinterpret_cast<volatile const std::uint8_t*>(0xC8D4C0);
}
void CGame::SetState(std::uint8_t value)
{
    *reinterpret_cast<volatile std::uint8_t*>(0xC8D4C0) = value;
}
bool CGame::IsPlaying()
{
    return GetState() == 9;
}
bool CGame::IsPaused()
{
    return *reinterpret_cast<const bool*>(0xBA67A4);
}
int CGame::ScreenWidth()
{
    return *reinterpret_cast<const int*>(0xC17044);
}
int CGame::ScreenHeight()
{
    return *reinterpret_cast<const int*>(0xC17048);
}
float CGame::FrameSeconds()
{
    return *reinterpret_cast<const float*>(0xB7CB5C) * 0.02f;
}
float CGame::HudScaleX()
{
    return *reinterpret_cast<const float*>(0x859520);
}
float CGame::HudScaleY()
{
    return *reinterpret_cast<const float*>(0x859524);
}
bool CGame::HasFontResources()
{
    return *reinterpret_cast<void**>(0xC9BCC0) != nullptr;
}
void CGame::ExpandControlKeys(char* text)
{
    reinterpret_cast<void(__cdecl*)(char*)>(0x69E160)(text);
}
void CGame::FlushFonts()
{
    reinterpret_cast<void(__cdecl*)()>(0x71A210)();
}
std::uint8_t CGame::GetHudState()
{
    return *reinterpret_cast<const std::uint8_t*>(0xBA6769);
}
std::uint8_t CGame::GetRadarState()
{
    return *reinterpret_cast<const std::uint8_t*>(0xBAA3FB);
}
void CGame::SetHudState(std::uint8_t value)
{
    *reinterpret_cast<std::uint8_t*>(0xBA6769) = value;
}
void CGame::SetRadarState(std::uint8_t value)
{
    *reinterpret_cast<std::uint8_t*>(0xBAA3FB) = value;
}

void* CGame::TraceObjectRay(const CVector& origin, const CVector& target)
{
    std::array<float, 11> colPoint{}; // gta CColPoint, 0x2c bytes
    void* entity = nullptr;
    using ProcessLineOfSight = bool(__cdecl*)(const CVector&, const CVector&, void*, void**, bool, bool, bool, bool, bool, bool, bool, bool);
    if (!reinterpret_cast<ProcessLineOfSight>(0x56BA00)(origin, target, colPoint.data(), &entity, true, false, false, true, false, false, false, false))
        return nullptr;
    return entity;
}

bool CGame::GetMapClickPosition(CVector& position)
{
    ZoneScoped;
    static bool rightButtonDown = false;
    const bool down = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool pressed = down && !rightButtonDown;
    rightButtonDown = down;
    if (!pressed || !IsPlaying() || !IsPaused() || !Mem<std::uint32_t>(0xBA6774) || Mem<std::uint8_t>(0xBA8298))
        return false;

    // r5 finds the active destination blip in gta's radar trace array
    for (std::uintptr_t entry = 0xBA8718; entry < 0xBA8718 + 175 * 0x28; entry += 0x28)
    {
        if (Mem<std::uint16_t>(entry + 0x25) != 0x1203)
            continue;
        position.x = Mem<float>(entry + 8);
        position.y = Mem<float>(entry + 12);
        if (!std::isfinite(position.x) || !std::isfinite(position.y))
            return false;
        position.z = reinterpret_cast<float(__cdecl*)(float, float)>(0x569660)(position.x, position.y) + 2.0f;
        return std::isfinite(position.z);
    }
    return false;
}
