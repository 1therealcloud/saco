/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>
#include "game/entities/CVehicle.h"
#include "game/patch/patch.h"

#include "hooks.h"
#include "main.h"
#include "audio/CAudioStream.h"
#include "config/CSettings.h"
#include "game/CGame.h"
#include "game/CCamera.h"
#include "game/BuildingRemoval.h"
#include "game/Scripting.h"
#include "game/entities/CObject.h"
#include "gui/CMenu.h"
#include "game/Debug.h"
#include "game/patch/FrameLimiter.h"
#include "game/patch/HudScaleFix.h"
#include "game/InputHandler.h"
#include "game/PlayerSlots.h"
#include "net/CNetGame.h"
#include "render/render.h"
#include "sync/AimStuff.h"
#include "sync/KeyStuff.h"
#include "players/CLocalPlayer.h"
#include "players/PlayerColors.h"
#include "game/entities/CPed.h"
#include "pools/CActorPool.h"
#include "pools/CVehiclePool.h"
#include "players/CRemotePlayer.h"
#include <intrin.h>

EventHandlerFn g_eventHandler = nullptr;

namespace game::hooks
{
    void* crimeReportPed = nullptr;
}

void* __cdecl CrimeReportPedHook(int)
{
    return game::hooks::crimeReportPed;
}

namespace
{
    using EnterVehicleTaskFn = void*(__thiscall*)(void*, void*);
    using ExitVehicleTaskFn = void*(__thiscall*)(void*, void*, int, int, bool, bool);
    EnterVehicleTaskFn g_enterVehicleTask = nullptr;
    ExitVehicleTaskFn g_exitVehicleTask = nullptr;

    CLocalPlayer* VehicleTaskLocalPlayer()
    {
        auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
        auto* local = pool ? pool->GetLocalPlayer() : nullptr;
        return local && local->m_bIsActive && !local->m_bDoesSpectating ? local : nullptr;
    }

    void* __fastcall EnterVehicleTaskHook(void* task, void*, void* vehicle)
    {
        ZoneScoped;
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        void* result = g_enterVehicleTask(task, vehicle);
        if (caller == 0x570A20 || caller == 0x570A99)
        {
            auto* local = VehicleTaskLocalPlayer();
            auto* pool = g_netGame ? g_netGame->GetVehiclePool() : nullptr;
            if (local && pool)
                local->EnterVehicle(pool->Find(vehicle), FALSE);
        }
        return result;
    }

    void* __fastcall ExitVehicleTaskHook(void* task, void*, void* vehicle, int door, int delay, bool sensible, bool force)
    {
        ZoneScoped;
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        void* result = g_exitVehicleTask(task, vehicle, door, delay, sensible, force);
        if (caller == 0x5704A1 || caller == 0x5703FC)
        {
            auto* local = VehicleTaskLocalPlayer();
            auto* pool = g_netGame ? g_netGame->GetVehiclePool() : nullptr;
            if (local && local->m_pPed && local->m_pPed->GetVehicle() == vehicle && pool)
                local->ExitVehicle(pool->Find(vehicle));
        }
        return result;
    }
}

bool game::hooks::InstallVehicleTransitionHooks()
{
    return hook::install(0x6402F0, reinterpret_cast<EnterVehicleTaskFn>(&EnterVehicleTaskHook), &g_enterVehicleTask) &&
           hook::install(0x63B8C0, reinterpret_cast<ExitVehicleTaskFn>(&ExitVehicleTaskHook), &g_exitVehicleTask);
}

using namespace game::patch::fps;
using namespace game::players::detail;
using namespace game::patch::hudscalefix::detail;
using game::patch::hudscalefix::ScopedScaleFix;

void __fastcall VehicleAudioHook(void* audio, void*)
{
    ZoneScoped;
    using namespace game::patch::vehicle::detail;
    // Audio must resolve the local listener, even inside remote vehicle control.
    auto& current = *reinterpret_cast<std::uint8_t*>(0xB7CD74);
    const auto savedCurrent = current;
    current = 0;
    auto* vehicle = static_cast<std::uint8_t*>(audio) - 0x138;
    auto* driver = *reinterpret_cast<std::uint8_t**>(vehicle + 0x460);
    auto* localPed = *reinterpret_cast<void**>(0xB7CD98);
    if (!driver || driver == localPed || !FindSlot(driver))
    {
        originalVehicleAudio(audio);
        current = savedCurrent;
        return;
    }

    // R5 processes remote engine audio as a civilian driver, not the local radio listener.
    auto& type = *reinterpret_cast<std::uint32_t*>(driver + 0x598);
    auto& status = vehicle[0x36];
    auto& listener = *reinterpret_cast<void**>(0xB6B990);
    const auto savedType = type;
    const auto savedStatus = status;
    auto* savedListener = listener;
    type = 4;
    status = 0x1A;
    originalVehicleAudio(audio);
    status = savedStatus;
    type = savedType;
    listener = savedListener;
    current = savedCurrent;
}

void __cdecl ShutdownHook()
{
    ZoneScoped;
    // g_game is created during bootstrap; net/ui may not exist if loading failed
    if (g_game)
    {
        game::inputhandler::uninstall();

        // disconnect
        if (g_netGame)
            g_netGame->Disconnect();

        if (g_audioStream)
        {
            delete g_audioStream;
            g_audioStream = nullptr;
        }

        // pools release native entities, so net goes before ui (same order as before the refactor)

        g_netGame.reset();
        render::ShutdownUi();

        g_audio = nullptr;
        g_game.reset();
    }

    ExitProcess(0);
}

RsEventStatus __cdecl EventHandlerHook(RsEvent event, void* param)
{
    // call game loop
    if (event == rsIDLE)
        Process();

    return g_eventHandler(event, param);
}

__declspec(naked) void FrameLimiterHook()
{
    __asm
    {
        // al = 1: use the frame limiter
        // al = 0: process the frame immediately
		test al, al
		jz process_frame

            // gta already calculated elapsed milliseconds at [esp + 0x14].
            // process the frame when elapsed >= our target frame time.
		fld dword ptr [esp + 14h]
		fcomp dword ptr [g_frameTimeMs]
		fnstsw ax
		test ah, 1
		jnz skip_frame

	process_frame:
		push 1
		push 1Ah

		call dword ptr [g_idleHandler]

		add esp, 8

	skip_frame:
		mov eax, 748DA3h
		jmp eax
    }
}

__declspec(naked) void MovementAnimationHook()
{
    __asm
    {
            cmp byte ptr [g_useCJWalk], 0
            je preserve_skin_group
            mov [esi + 0x4D4], eax
        preserve_skin_group:
            ret
    }
}

void* __fastcall PlayerInfoHook(void* ped, void*)
{
    const int slot = FindSlot(ped);
    return slot ? reinterpret_cast<void*>(0xB7CD98 + slot * 0x190) : g_info(ped);
}

void* __fastcall ShadowHook(void* manager, void*, void* ped)
{
    ZoneScoped;
    if (!FindSlot(ped))
        return g_shadow(manager, ped);
    // GTA reserves one shadow for PLAYER1; remote players need separate pool entries
    auto& type = *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(ped) + 0x598);
    const auto savedType = type;
    type = 4; // PED_TYPE_CIVMALE selects the ordinary shadow pool
    void* shadow = g_shadow(manager, ped);
    type = savedType;
    return shadow;
}

void __fastcall ProcessControlHook(void* ped, void*)
{
    ZoneScoped;

    const int slot = FindSlot(ped);
    auto& current = *reinterpret_cast<std::uint8_t*>(0xB7CD74);
    if (!ped || !slot || current != 0)
    {
        ApplyLocalControlSuppressions();
        EnableLocalCameraRotation();
        g_control(ped);
        DisableLocalCameraRotation();
        return;
    }

    KeyStuff::UpdateKeys();
    KeyStuff::ApplyKeys(slot);

    const char savedMode = *AimStuff::RefInternalCameraMode();
    auto& secondaryMode = *reinterpret_cast<std::uint16_t*>(0xB6F858);
    const auto savedSecondaryMode = secondaryMode;
    const auto remoteMode = AimStuff::GetCameraMode(static_cast<std::uint8_t>(slot));
    *AimStuff::RefInternalCameraMode() = remoteMode;
    secondaryMode = remoteMode == 4 ? 0 : static_cast<std::uint8_t>(remoteMode);

    AimStuff::UpdateCameraExtZoomAndAspectRatio();
    AimStuff::ApplyCameraExtZoomAndAspectRatio(static_cast<std::uint8_t>(slot));
    AimStuff::UpdateAim();
    AimStuff::ApplyAim(slot);
    AimStuff::UpdatePlayerStats();
    AimStuff::ApplyPlayerStats(static_cast<std::uint8_t>(slot));

    current = static_cast<std::uint8_t>(slot);
    auto* intelligence = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(ped) + 0x47C);
    using FindTaskFn = void*(__thiscall*)(void*, int);
    const auto findTask = reinterpret_cast<FindTaskFn>(0x681740);
    const bool vehicleTask = intelligence && (findTask(intelligence + 4, 700) || findTask(intelligence + 4, 701) || findTask(intelligence + 4, 712) ||
                                              findTask(intelligence + 4, 713) || findTask(intelligence + 4, 702) || findTask(intelligence + 4, 704));
    SetPositionUpdate(vehicleTask);
    g_control(ped);
    SetPositionUpdate(true);

    AimStuff::ApplyPlayerStats();
    *AimStuff::RefInternalCameraMode() = savedMode;
    secondaryMode = savedSecondaryMode;
    AimStuff::ApplyCameraExtZoomAndAspectRatio();
    current = 0;
    KeyStuff::ApplyKeys();
    AimStuff::ApplyAim();
}

namespace
{
    struct VehicleControlMethod
    {
        std::uintptr_t vtable;
        game::players::detail::ControlFn original;
    };
    std::array<VehicleControlMethod, 10> g_vehicleControlMethods{{{0x871120, nullptr},
                                                                  {0x8721A0, nullptr},
                                                                  {0x871360, nullptr},
                                                                  {0x871948, nullptr},
                                                                  {0x871680, nullptr},
                                                                  {0x871528, nullptr},
                                                                  {0x8717D8, nullptr},
                                                                  {0x871AE8, nullptr},
                                                                  {0x872370, nullptr},
                                                                  {0x871C28, nullptr}}};

    bool VehicleUsesNativeVerticalPhysics(const std::uint8_t* vehicle)
    {
        ZoneScoped;
        auto* matrix = *reinterpret_cast<const CMatrix* const*>(vehicle + 0x14);
        auto* local = *reinterpret_cast<const std::uint8_t* const*>(0xB7CD98);
        auto* localMatrix = local ? *reinterpret_cast<const CMatrix* const*>(local + 0x14) : nullptr;
        if (!matrix || !localMatrix)
            return false;
        const auto& position = matrix->GetPosition();
        const auto& localPosition = localMatrix->GetPosition();
        const float x = position.x - localPosition.x;
        const float y = position.y - localPosition.y;
        if (x * x + y * y >= 40000.0f)
        {
            using HasCollisionLoaded = bool(__cdecl*)(const CVector*, int);
            return reinterpret_cast<HasCollisionLoaded>(0x410CE0)(&position, 0);
        }
        return false;
    }

    void __fastcall VehicleControlHook(void* native, void*)
    {
        ZoneScoped;
        const auto vtable = *static_cast<const std::uintptr_t*>(native);
        const auto method = std::find_if(g_vehicleControlMethods.begin(), g_vehicleControlMethods.end(), [vtable](const auto& entry) {
            return entry.vtable == vtable;
        });
        if (method == g_vehicleControlMethods.end() || !method->original)
            return;
        auto* vehicle = static_cast<std::uint8_t*>(native);
        auto* driver = *reinterpret_cast<void**>(vehicle + 0x460);
        auto& current = *reinterpret_cast<std::uint8_t*>(0xB7CD74);
        const int slot = driver ? FindSlot(driver) : 0;
        if (!slot || current != 0)
        {
            method->original(native);
            return;
        }

        // R5 swaps the driver's input around the vehicle's virtual ProcessControl too.
        KeyStuff::UpdateKeys();
        KeyStuff::ApplyKeys(slot);
        const char savedMode = *AimStuff::RefInternalCameraMode();
        auto& secondaryMode = *reinterpret_cast<std::uint16_t*>(0xB6F858);
        const auto savedSecondaryMode = secondaryMode;
        const auto remoteMode = AimStuff::GetCameraMode(static_cast<std::uint8_t>(slot));
        *AimStuff::RefInternalCameraMode() = remoteMode;
        secondaryMode = remoteMode == 4 ? 0 : static_cast<std::uint8_t>(remoteMode);
        AimStuff::UpdateCameraExtZoomAndAspectRatio();
        AimStuff::ApplyCameraExtZoomAndAspectRatio(static_cast<std::uint8_t>(slot));
        AimStuff::UpdateAim();
        AimStuff::ApplyAim(slot);
        current = static_cast<std::uint8_t>(slot);
        auto* matrix = *reinterpret_cast<const CMatrix* const*>(vehicle + 0x14);
        const float savedZ = matrix ? matrix->GetPosition().z : 0.0f;
        const float savedSpeedZ = *reinterpret_cast<const float*>(vehicle + 0x4C);
        const auto model = *reinterpret_cast<const std::uint16_t*>(vehicle + 0x22);
        const bool train = model == 538 || model == 537 || model == 449 || model == 569 || model == 570;
        const bool airOrWater = vtable == 0x8721A0 || vtable == 0x871948 || vtable == 0x871680;
        const bool preserveZ = matrix && (airOrWater || (!train && !VehicleUsesNativeVerticalPhysics(vehicle)));
        method->original(native);
        // R5 retains synchronized Z for nearby vehicles, boats and aircraft.
        if (preserveZ)
        {
            auto* updatedMatrix = *reinterpret_cast<CMatrix**>(vehicle + 0x14);
            if (updatedMatrix)
            {
                updatedMatrix->GetPosition().z = savedZ;
                *reinterpret_cast<float*>(vehicle + 0x4C) = savedSpeedZ;
            }
        }
        current = 0;
        KeyStuff::ApplyKeys();
        *AimStuff::RefInternalCameraMode() = savedMode;
        secondaryMode = savedSecondaryMode;
        AimStuff::ApplyCameraExtZoomAndAspectRatio();
        AimStuff::ApplyAim();
    }
}

bool game::hooks::InstallVehicleControlHooks()
{
    ZoneScoped;
    for (auto& method : g_vehicleControlMethods)
    {
        auto* entry = reinterpret_cast<game::players::detail::ControlFn*>(method.vtable + 0x28);
        DWORD protection{};
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &protection))
            return false;
        method.original = *entry;
        *entry = reinterpret_cast<game::players::detail::ControlFn>(&VehicleControlHook);
        DWORD unused{};
        VirtualProtect(entry, sizeof(*entry), protection, &unused);
    }
    return true;
}

std::uint32_t __fastcall WeaponSkillHook(void* ped, void*)
{
    ZoneScoped;
    const int slot = FindSlot(ped);
    const bool switchStats = slot && *reinterpret_cast<const std::uint8_t*>(0xB7CD74) == 0;
    if (switchStats)
    {
        AimStuff::UpdatePlayerStats();
        AimStuff::ApplyPlayerStats(static_cast<std::uint8_t>(slot));
    }

    const auto* native = static_cast<const std::uint8_t*>(ped);
    const auto weaponSlot = *reinterpret_cast<const std::int8_t*>(native + 0x718);
    const auto weapon = *reinterpret_cast<const int*>(native + 0x5A0 + weaponSlot * 0x1C);
    const auto skill = reinterpret_cast<std::uint8_t(__thiscall*)(void*, int)>(0x5E3B60)(ped, weapon);

    if (switchStats)
        AimStuff::ApplyPlayerStats();
    return skill;
}

void __cdecl DrawRadarHook()
{
    ZoneScoped;
    ScopedScaleFix fix(true);
    g_drawRadar();
}

void __cdecl DrawCrossHairsHook()
{
    ZoneScoped;
    // R5 only applies the HUD scale correction to this crosshair camera mode.
    const auto cameraMode = *reinterpret_cast<const volatile std::uint8_t*>(0x00B6F1A8);
    ScopedScaleFix fix(cameraMode == 0x35);
    reinterpret_cast<void(__cdecl*)()>(0x0058E020)();
}

namespace game::hooks
{
    RadarColorFn g_radarColor = nullptr;

    bool InstallRenderHooks()
    {
        ZoneScoped;
        return (g_originalRender || hook::install(0x553260, &RenderObjectHook, &g_originalRender)) &&
               (g_originalCameraIgnore || hook::install(0x563F40, &CameraToIgnoreObjectHook, &g_originalCameraIgnore)) &&
               hook::install(0x584770, &RadarTraceColorHook, &g_radarColor) && hook::install(0x53E230, &NativeTextHook, &g_text) && hook::install(0x586650, &GangZoneRadarHook) &&
               hook::install(0x553AA0, &WorldRenderHook, &g_world) && hook::install(0x5531E0, &FadingRenderHook, &g_fading);
    }

    DrawFn g_world = nullptr, g_fading = nullptr, g_text = nullptr;
    LoadInstanceFn originalLoad = nullptr;
    DestroyEntityFn originalDestroy = nullptr;
    DrawFn originalOcclusionInit = nullptr, originalPoolsShutdown = nullptr;
    MenuTextFn g_getText = nullptr;
    ObjectRenderFn g_originalRender = nullptr;
    CameraIgnoreFn g_originalCameraIgnore = nullptr;
}
static void __cdecl NotifyPickupCollected(void* pickup)
{
    auto* pool = g_netGame ? g_netGame->GetPickupPool() : nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(pickup);
    if (pool && address >= 0x9788C0 && (address - 0x9788C0) % 0x20 == 0)
        pool->SendNotification(static_cast<int>((address - 0x9788C0) / 0x20));
}

__declspec(naked) void NegativeModelGuardHook()
{
    __asm
    {
        cmp ebp, 0
        jl negative_model
        push esi
        push edi
        lea edi, [ebp + ebp*4]
        push 0x4087F0
        ret
    negative_model:
        pop ebp
        pop ebx
        ret
    }
}

__declspec(naked) void PickupCollectedHook()
{
    __asm
    {
            pushad
            pushfd
            push esi
            call NotifyPickupCollected
            add esp, 4
            popfd
            popad
            mov al, [esi + 0x1C]
            cmp al, 6
            push 0x4579CB
            ret
    }
}

void __cdecl WorldRenderHook()
{
    ZoneScoped;

    game::hooks::g_world();
    render::RenderWorldObjects();
}

void __cdecl FadingRenderHook()
{
    ZoneScoped;

    game::hooks::g_fading();
    render::RenderFadingObjects();
}

void __cdecl NativeTextHook()
{
    game::hooks::g_text();
    render::RenderNativeTextDraws();
}

void __cdecl GangZoneRadarHook(bool menu)
{
    render::DrawGangZoneOverlay(menu);
}

std::uint32_t __cdecl RadarTraceColorHook(std::uint32_t color, bool bright, bool friendly)
{
    // R5's internal radar colours are codes, not packed RGBA values.
    if (color >= 1004 && color <= 1006)
        return PlayerColors::GetColor(color);
    return game::hooks::g_radarColor(color, bright, friendly);
}

static void __cdecl UpdateAttachedCamera()
{
    ZoneScoped;
    if (auto* game = g_game.get())
        if (auto* camera = game->GetCamera())
            camera->SetToOwner();
}

__declspec(naked) void CameraProcessHook()
{
    __asm
    {
            pushad
            call UpdateAttachedCamera
            popad
            mov eax, 0x0052B730
            jmp eax
    }
}

const char* __fastcall MenuTextHook(void* text, void*, const char* key)
{
    if (!key)
        return "";
    if (const char* resolved = CMenu::ResolveTextKey(key))
        return resolved;
    return game::hooks::g_getText(text, key);
}

static void __cdecl Dispatch09C6(void* script)
{
    ZoneScoped;
    const auto collectParameters = reinterpret_cast<void(__thiscall*)(void*, short)>(0x464080);
    collectParameters(script, 4);

    volatile const auto* params = reinterpret_cast<volatile const std::uint32_t*>(0xA43C78);

    const auto type = static_cast<scm::EventType>(params[0]);
    const std::uint32_t param1 = params[1];
    const std::uint32_t param2 = params[2];
    const std::uint32_t param3 = params[3];

    scm::handle_09c6(type, param1, param2, param3);
}

static void __cdecl Dispatch09D3(void* script)
{
    const bool result = scm::handle_09d3();
    const auto updateCompareFlag = reinterpret_cast<void(__thiscall*)(void*, bool)>(0x4859D0);

    updateCompareFlag(script, result);
}

__declspec(naked) void Opcode09C6()
{
    __asm
    {
			pushad

			push esi
			call Dispatch09C6
			add esp, 4

			popad

			mov ebx, 0x47BF2B
			jmp ebx
    }
}

__declspec(naked) void Opcode09D3()
{
    __asm
    {
			pushad

			push esi
			call Dispatch09D3
			add esp, 4

			popad

			mov ebx, 0x47BF2B
			jmp ebx
    }
}

void* __cdecl LoadObjectInstanceHook(void* instance, const char* name)
{
    auto* entity = game::hooks::originalLoad(instance, name);
    BuildingRemoval::detail::QueueLoadedInstance(entity, instance);
    return entity;
}

void __fastcall EntityDestroyedHook(void* entity, void*)
{
    BuildingRemoval::detail::ForgetEntity(entity);
    game::hooks::originalDestroy(entity);
}

void __cdecl OcclusionInitializedHook()
{
    BuildingRemoval::detail::ClearSnapshots();
    game::hooks::originalOcclusionInit();
}

void __cdecl PoolsShutdownHook()
{
    BuildingRemoval::detail::ClearSnapshots();
    game::hooks::originalPoolsShutdown();
}

void __cdecl RenderObjectHook(void* entity)
{
    ZoneScoped;
    game::objects::detail::RenderNativeObject(entity, game::hooks::g_originalRender);
}

bool __cdecl CameraToIgnoreObjectHook(void* entity)
{
    if (game::objects::detail::IgnoresCamera(entity))
        return true;
    return game::hooks::g_originalCameraIgnore ? game::hooks::g_originalCameraIgnore(entity) : false;
}

__declspec(naked) void EmptyVertexStreamAllocateHook()
{
    __asm
    {
        mov edx, [eax + 8]
        test edx, edx
        jz empty_stream
        imul ecx, edx
        push 757C42h
        ret
    empty_stream:
        push 757C7Dh
        ret
    }
}
__declspec(naked) void EmptyVertexStreamLockHook()
{
    __asm
    {
        cmp dword ptr [eax + 8], 0
        je empty_stream
        push 800h
        push ecx
        push 757D62h
        ret
    empty_stream:
        push 757D76h
        ret
    }
}

__declspec(naked) void EmptyVertexStreamRelockHook()
{
    __asm
    {
        mov ecx, [eax + 8]
        test ecx, ecx
        jz empty_stream
        mov bl, [eax + 0Eh]
        push 757DC1h
        ret
    empty_stream:
        push 757DFBh
        ret
    }
}

bool __fastcall UsesSirenHook(void* vehicle, void*)
{
    ZoneScoped;
    using namespace game::patch::vehicle::detail;
    if (nextVehicleHasSiren)
    {
        nextVehicleHasSiren = false;
        return true;
    }
    if (originalUsesSiren && originalUsesSiren(vehicle))
        return true;
    if (auto* wrapper = CVehicle::FindNative(vehicle))
        return wrapper->HasSiren() != FALSE;
    return false;
}

void __fastcall TrailerProcessHook(void* vehicle, void*)
{
    ZoneScoped;
    float health{};
    void* tractor = nullptr;
    if (vehicle)
    {
        std::memcpy(&health, static_cast<const std::uint8_t*>(vehicle) + 0x4C0, sizeof(health));
        std::memcpy(&tractor, static_cast<const std::uint8_t*>(vehicle) + 0x4C4, sizeof(tractor));
    }
    if (!game::patch::vehicle::detail::detachingTrailer && health > 0.0f)
        if (auto* wrapper = CVehicle::FindNative(tractor))
            if (!wrapper->DriverIsPlayerPed() && wrapper->m_pTrailer)
                return;
    reinterpret_cast<void(__thiscall*)(void*)>(0x6A4400)(vehicle);
}

bool __fastcall FireInstantHitHook(void* weapon, void*, void* owner, CVector* origin, CVector* muzzle, void* target, CVector* targetPosition, CVector* driveBy, bool crosshair,
                                   int extra)
{
    ZoneScoped;
    using namespace game::bullets::detail;
    if (FindSlot(owner) && !remoteShot)
        return false;
    void* previous = shooter;
    shooter = owner;
    const bool result = instant(weapon, owner, origin, muzzle, target, targetPosition, driveBy, crosshair, extra);
    shooter = previous;
    return result;
}

bool __fastcall FireSniperHook(void* weapon, void*, void* owner, void* target, CVector* targetPosition)
{
    ZoneScoped;
    using namespace game::bullets::detail;
    if (FindSlot(owner) && !remoteShot)
        return false;
    void* previous = shooter;
    shooter = owner;
    const bool result = sniper(weapon, owner, target, targetPosition);
    shooter = previous;
    return result;
}

void __cdecl SniperBulletHook(void* owner, int weapon, CVector position, CVector velocity)
{
    // r5 resolves sniper bullets immediately, while the shot and aim context are still active
    velocity *= 50.0f;
    reinterpret_cast<bool(__cdecl*)(void*, int, CVector, CVector)>(0x736010)(owner, weapon, position, velocity);
    reinterpret_cast<void(__cdecl*)()>(0x7360D0)();
}

bool __cdecl BulletTraceHook(const CVector& origin, const CVector& target, void* point, void** entity, bool buildings, bool vehicles, bool peds, bool objects, bool dummies,
                             bool seeThrough, bool cameraIgnore, bool shootThrough)
{
    ZoneScoped;
    using namespace game::bullets::detail;
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (!shooter || caller < 0x730000 || caller >= 0x744000)
        return trace(origin, target, point, entity, buildings, vehicles, peds, objects, dummies, seeThrough, cameraIgnore, shootThrough);
    CVector end = target;
    if (remoteShot)
    {
        end = remoteShot->target;
        if (remoteShot->entity && lagCompMode != 2)
        {
            const auto* native = static_cast<const std::uint8_t*>(remoteShot->entity);
            const auto* matrix = *reinterpret_cast<const CMatrix* const*>(native + 0x14);
            if (matrix)
            {
                const auto hit = lagCompMode == 0 ? matrix->TransformPoint(remoteShot->center) : matrix->GetPosition() + remoteShot->center;
                end = hit * 2.0f - origin;
            }
        }
    }
    const bool hit = trace(origin, end, point, entity, buildings, vehicles, peds, objects, dummies, seeThrough, cameraIgnore, shootThrough);
    if (!shooter || !point || !entity)
        return hit;
    auto* localNative = *reinterpret_cast<void**>(0xB7CD98);
    if (remoteShot)
    {
        if (lagCompMode != 2 && !remoteShot->entity && (*entity == localNative || (g_game && g_game->GetPlayerPed() && *entity == g_game->GetPlayerPed()->GetVehicle())))
        {
            *entity = nullptr;
            return false;
        }
        return hit;
    }
    if (shooter == localNative)
    {
        auto* pool = g_netGame ? g_netGame->GetPlayerPool() : nullptr;
        auto* local = pool ? pool->GetLocalPlayer() : nullptr;
        if (local)
            local->SendBulletData(origin, hit ? *reinterpret_cast<const CVector*>(point) : end, hit ? *entity : nullptr);
    }
    return hit;
}

bool __fastcall DamageAffectsPedHook(void* event, void*, void* victim)
{
    // R5 A53A0: local damage events remain valid while remote player controls run.
    if (g_netGame && victim == *reinterpret_cast<void**>(0xB7CD98) && *reinterpret_cast<const std::uint8_t*>(0xB7CD74) != 0)
        return true;
    return game::bullets::detail::affectsPed(event, victim);
}

void __fastcall ComputeDamageResponseHook(game::bullets::detail::DamageCalculator* calculator, void*, void* victim, void* response, bool speak)
{
    ZoneScoped;
    auto* pools = g_netGame ? g_netGame->m_pPools : nullptr;
    auto* players = pools ? pools->m_pPlayer : nullptr;
    auto* local = players ? players->GetLocalPlayer() : nullptr;
    auto* ped = local ? local->m_pPed : nullptr;
    if (!calculator || !victim || !ped || !ped->HasNativePed() || g_netGame->GetState() != CNetGame::GAME_MODE_CONNECTED)
    {
        game::bullets::detail::damage(calculator, victim, response, speak);
        return;
    }
    auto* localNative = ped->GetNativePedHandle();
    auto* damager = calculator->damager;
    // r5 excludes damage to ordinary GTA peds from non-local sources
    if (damager != localNative && *static_cast<const std::uintptr_t*>(victim) == 0x86C0A8)
        return;
    const bool taking = victim == localNative;
    if (taking || damager == localNative)
    {
        if (taking && ((damager && ped->GetVehicle() == damager) || calculator->weapon == 54))
            return;
        const auto findPlayer = [&](void* entity) {
            auto id = players->Find(entity);
            if (id != CPlayerPool::INVALID_ID || !entity || !pools->m_pVehicle)
                return id;
            const auto vehicle = pools->m_pVehicle->Find(entity);
            if (vehicle != CVehiclePool::INVALID_ID)
                for (int i = 0; i <= players->m_nLargestId; ++i)
                    if (auto* remote = players->GetPlayer(static_cast<CPlayerPool::ID>(i)))
                        if (remote->m_pPed && remote->m_nVehicleId == vehicle)
                            return static_cast<CPlayerPool::ID>(i);
            return CPlayerPool::INVALID_ID;
        };
        const auto id = findPlayer(taking ? damager : victim);
        if (taking)
            local->SendTakeDamage(id, calculator->damage, calculator->weapon, calculator->bodyPart);
        else if (id != CPlayerPool::INVALID_ID)
        {
            local->SendGiveDamage(id, calculator->damage, calculator->weapon, calculator->bodyPart);
            if (auto* remote = players->GetPlayer(id))
                if (remote->field_3)
                    return;
        }
        else if (pools->m_pActor)
        {
            const auto actor = pools->m_pActor->Find(victim);
            if (actor != CActorPool::INVALID_ID)
            {
                g_netGame->GiveActorDamage(false, actor, calculator->damage, calculator->weapon, calculator->bodyPart);
                return;
            }
        }
        if (g_netGame->m_pSettings && g_netGame->m_pSettings->m_bFriendlyFire && local->m_nTeam != 255)
            if (auto* remote = players->GetPlayer(id))
                if (remote->m_nTeam == local->m_nTeam)
                    return;
    }
    game::bullets::detail::damage(calculator, victim, response, speak);
}

namespace
{
    bool SkipVehicleCollision(void* vehicle, void* other)
    {
        if (!g_netGame || !g_netGame->m_bDisableCollision || !vehicle || !other)
            return false;
        const auto* first = static_cast<const std::uint8_t*>(vehicle);
        const auto* second = static_cast<const std::uint8_t*>(other);
        const auto model = *reinterpret_cast<const std::uint16_t*>(second + 0x22);
        return model >= 400 && model <= 611 && *reinterpret_cast<void* const*>(first + 0x460) && *reinterpret_cast<void* const*>(second + 0x460);
    }
}

int __fastcall AutomobileCollisionHook(void* vehicle, void*, void* other, void* points)
{
    if (SkipVehicleCollision(vehicle, other))
        return 0;
    return reinterpret_cast<int(__thiscall*)(void*, void*, void*)>(0x6BDEA0)(vehicle, other, points);
}

int __fastcall BikeCollisionHook(void* vehicle, void*, void* other, void* points)
{
    if (SkipVehicleCollision(vehicle, other))
        return 0;
    return reinterpret_cast<int(__thiscall*)(void*, void*, void*)>(0x6C8AE0)(vehicle, other, points);
}

int __fastcall VehicleCollisionHook(void* vehicle, void*, void* other, void* points)
{
    if (SkipVehicleCollision(vehicle, other))
        return 0;
    return reinterpret_cast<int(__thiscall*)(void*, void*, void*)>(0x546D00)(vehicle, other, points);
}

bool game::hooks::InstallVehicleCollisionHooks()
{
    // R5 replaces the ProcessEntityCollision slots for automobiles, monster trucks,
    // bikes and trailers, without disabling their collisions with the world.
    return ::patch::write<std::uintptr_t>(0x8713B8, reinterpret_cast<std::uintptr_t>(&AutomobileCollisionHook)) &&
           ::patch::write<std::uintptr_t>(0x871580, reinterpret_cast<std::uintptr_t>(&AutomobileCollisionHook)) &&
           ::patch::write<std::uintptr_t>(0x871830, reinterpret_cast<std::uintptr_t>(&BikeCollisionHook)) &&
           ::patch::write<std::uintptr_t>(0x8721F8, reinterpret_cast<std::uintptr_t>(&VehicleCollisionHook));
}

bool game::hooks::InstallGameLoopHook()
{
    ZoneScoped;
    constexpr std::uintptr_t kIdleCall = 0x748D9B;

    std::int32_t relative{};
    if (!::patch::read(kIdleCall + 1, &relative, sizeof(relative)))
        return false;

    const auto target = kIdleCall + 5 + static_cast<std::uintptr_t>(relative);

    return hook::install(target, &EventHandlerHook, &g_eventHandler);
}

bool game::hooks::InstallShutdownHook()
{
    return hook::install(0x53C900, &ShutdownHook);
}

bool game::hooks::InstallCameraHook()
{
    ZoneScoped;
    // r5 redirects this call, not the whole gta camera function.
    if (!::patch::expect_call(0x53C104, 0x52B730))
        return false;

    return ::patch::call(0x53C104, reinterpret_cast<const void*>(&CameraProcessHook));
}

bool game::hooks::InstallMenuTextHook()
{
    static bool installed = false;
    if (installed)
        return true;
    if (!hook::install(0x6A0050, reinterpret_cast<decltype(game::hooks::g_getText)>(&MenuTextHook), &game::hooks::g_getText))
        return false;
    installed = true;
    return true;
}

bool game::hooks::InstallBulletHooks()
{
    return ::patch::call(0x73AC4B, reinterpret_cast<const void*>(&SniperBulletHook)) &&
           hook::install(0x73FB10, reinterpret_cast<game::bullets::detail::InstantFn>(&FireInstantHitHook), &game::bullets::detail::instant) &&
           hook::install(0x73AAC0, reinterpret_cast<game::bullets::detail::SniperFn>(&FireSniperHook), &game::bullets::detail::sniper) &&
           hook::install(0x56BA00, &BulletTraceHook, &game::bullets::detail::trace) &&
           hook::install(0x4B5AC0, reinterpret_cast<game::bullets::detail::DamageFn>(&ComputeDamageResponseHook), &game::bullets::detail::damage) &&
           hook::install(0x4B35A0, reinterpret_cast<game::bullets::detail::AffectsPedFn>(&DamageAffectsPedHook), &game::bullets::detail::affectsPed);
}

bool game::hooks::InstallBuildingRemovalHooks()
{
    if (!hook::install(0x538090, &LoadObjectInstanceHook, &game::hooks::originalLoad) ||
        !hook::install(0x535E90, reinterpret_cast<game::hooks::DestroyEntityFn>(&EntityDestroyedHook), &game::hooks::originalDestroy) ||
        !hook::install(0x71DCA0, &OcclusionInitializedHook, &game::hooks::originalOcclusionInit) ||
        !hook::install(0x5519F0, &PoolsShutdownHook, &game::hooks::originalPoolsShutdown))
        return false;
    return true;
}

namespace
{
    constexpr std::uintptr_t kVertexDeclarationInstColor = 0x754AE0;
    using VertexDeclarationInstColorFn = std::int32_t(__cdecl*)(std::uint8_t*, const std::uint8_t*, std::int32_t, std::uint32_t);
    VertexDeclarationInstColorFn g_vertexDeclarationInstColor = nullptr;

    bool AllowsRead(DWORD protect)
    {
        ZoneScoped;
        if (protect & (PAGE_GUARD | PAGE_NOACCESS))
            return false;
        switch (protect & 0xFF)
        {
            case PAGE_READONLY:
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                return true;
            default:
                return false;
        }
    }

    bool AllowsWrite(DWORD protect)
    {
        ZoneScoped;
        if (protect & (PAGE_GUARD | PAGE_NOACCESS))
            return false;
        switch (protect & 0xFF)
        {
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                return true;
            default:
                return false;
        }
    }

    bool ValidateSpan(const void* data, std::size_t size, bool write)
    {
        ZoneScoped;
        if (!size)
            return true;
        if (!data)
            return false;

        const auto begin = reinterpret_cast<std::uintptr_t>(data);
        if (size - 1 > std::numeric_limits<std::uintptr_t>::max() - begin)
            return false;
        const auto end = begin + size;

        for (auto cursor = begin; cursor < end;)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
                !(write ? AllowsWrite(info.Protect) : AllowsRead(info.Protect)))
                return false;

            const auto region = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            if (info.RegionSize > std::numeric_limits<std::uintptr_t>::max() - region)
                return false;
            const auto regionEnd = region + info.RegionSize;
            if (regionEnd <= cursor)
                return false;
            cursor = regionEnd < end ? regionEnd : end;
        }
        return true;
    }

    std::int32_t __cdecl VertexDeclarationInstColorHook(std::uint8_t* mem, const std::uint8_t* color, std::int32_t numVerts, std::uint32_t stride)
    {
        ZoneScoped;
        if (numVerts <= 0)
            return FALSE;

        constexpr std::size_t kColorSize = 4;
        const auto count = static_cast<std::size_t>(numVerts);
        if (count > std::numeric_limits<std::size_t>::max() / kColorSize)
            return FALSE;
        const auto colorBytes = count * kColorSize;

        const auto lastVertex = count - 1;
        if (stride && lastVertex > (std::numeric_limits<std::size_t>::max() - kColorSize) / stride)
            return FALSE;
        const auto destinationBytes = lastVertex * static_cast<std::size_t>(stride) + kColorSize;

        if (!ValidateSpan(color, colorBytes, false) || !ValidateSpan(mem, destinationBytes, true))
        {
            return FALSE;
        }

        return g_vertexDeclarationInstColor(mem, color, numVerts, stride);
    }

    bool InstallRenderWareCrashFix()
    {
        ZoneScoped;
        // unused streams have zero stride; do not allocate or lock a buffer for them
        return hook::install(kVertexDeclarationInstColor, &VertexDeclarationInstColorHook, &g_vertexDeclarationInstColor) &&
               ::patch::jump(0x757C3C, reinterpret_cast<const void*>(&EmptyVertexStreamAllocateHook), 6) &&
               ::patch::jump(0x757D5C, reinterpret_cast<const void*>(&EmptyVertexStreamLockHook), 6) &&
               ::patch::jump(0x757DBB, reinterpret_cast<const void*>(&EmptyVertexStreamRelockHook), 6);
    }

}

bool game::hooks::InstallRenderWareHooks()
{
    return InstallRenderWareCrashFix();
}

namespace
{
    constexpr std::array<std::uintptr_t, 5> TRAILER_VTABLE_SLOTS = {
        0x871218, 0x871778, 0x8718D0, 0x871A40, 0x871BE0,
    };
}

bool game::hooks::InstallVehicleHooks()
{
    ZoneScoped;
    static bool installed = false;
    if (installed)
        return true;

    if (!hook::install(0x6D8470, reinterpret_cast<game::patch::vehicle::detail::UsesSirenFn>(&UsesSirenHook), &game::patch::vehicle::detail::originalUsesSiren))
        return false;
    if (!hook::install(0x502280, reinterpret_cast<game::patch::vehicle::detail::VehicleAudioFn>(&VehicleAudioHook), &game::patch::vehicle::detail::originalVehicleAudio))
        return false;
    const auto replacement = reinterpret_cast<std::uintptr_t>(&TrailerProcessHook);
    for (const auto slot : TRAILER_VTABLE_SLOTS)
        if (!::patch::write(slot, replacement))
            return false;

    installed = true;
    return true;
}

namespace
{
    using ReadKeyboardFn = void(__thiscall*)(void*);
    ReadKeyboardFn g_readKeyboard = nullptr;
    void __fastcall ReadKeyboardHook(void* manager, void*)
    {
        for (const uintptr_t address : {0xB72CB0u, 0xB72F20u, 0xB73190u})
            game::inputhandler::filter_game_keyboard(reinterpret_cast<std::uint16_t*>(address));
        game::inputhandler::FinishKeyboardFilter();
        g_readKeyboard(manager);
    }
}
bool game::hooks::InstallKeyboardHook()
{
    return g_readKeyboard || hook::install(0x531140, reinterpret_cast<ReadKeyboardFn>(&ReadKeyboardHook), &g_readKeyboard);
}
void game::hooks::RemoveKeyboardHook()
{
    if (!g_readKeyboard)
        return;
    MH_DisableHook(reinterpret_cast<void*>(0x531140));
    MH_RemoveHook(reinterpret_cast<void*>(0x531140));
    g_readKeyboard = nullptr;
}
LRESULT CALLBACK game::hooks::WindowProcHook(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    return game::inputhandler::ProcessWindowMessage(window, message, wParam, lParam);
}

bool game::hooks::InstallPlayerHooks()
{
    if (!hook::install(0x609FF0, reinterpret_cast<InfoFn>(&PlayerInfoHook), &g_info) || !hook::install(0x706970, reinterpret_cast<ShadowFn>(&ShadowHook), &g_shadow))
        return false;
    const auto controlHook = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&ProcessControlHook));
    if (!::patch::write(0x86D190, controlHook))
        return false;
    // r5 also selects remote weapon skills outside ProcessControl
    if (!::patch::call(0x7330A2, reinterpret_cast<const void*>(&WeaponSkillHook)))
        return false;
    std::array<std::uint8_t, 6> animationCall{0xE8, 0, 0, 0, 0, 0x90};
    const auto relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&MovementAnimationHook) - 0x609A4E - 5);
    std::memcpy(animationCall.data() + 1, &relative, sizeof(relative));
    if (!::patch::write(0x609A4E, animationCall.data(), animationCall.size()))
        return false;
    return true;
}

bool game::hooks::InstallHudHooks()
{
    static bool installed = false;
    if (installed)
        return false;
    // The scoped draw wrappers update these constants every frame.
    DWORD oldProtect{};
    if (!VirtualProtect(reinterpret_cast<void*>(0x859520), sizeof(float) * 2, PAGE_READWRITE, &oldProtect) ||
        !VirtualProtect(reinterpret_cast<void*>(0x866B74), sizeof(float) * 2, PAGE_READWRITE, &oldProtect))
        return false;

    // vital stats redirects here too while TAB is held
    if (!hook::install(0x58A330, &DrawRadarHook, &g_drawRadar))
        return false;

    if (!::patch::call(0x58FBBF, reinterpret_cast<const void*>(&DrawCrossHairsHook)))
        return false;

    installed = true;
    return true;
}

bool game::hooks::InstallFrameLimiterHooks()
{
    ZoneScoped;
    constexpr std::uintptr_t kIdleDelay = 0x53E923;

    constexpr std::uintptr_t kLimitFlag = 0x748D81;
    constexpr std::uintptr_t kLimitHook = 0x748D83;

    constexpr std::uintptr_t kNoLimitFlag = 0x748D98;
    constexpr std::uintptr_t kNoLimitHook = 0x748D9A;

    static bool installed = false;
    if (installed)
        return true;

    // preserve the existing idle call, including a loader or another hook
    std::int32_t relative = 0;
    if (!::patch::read(kNoLimitFlag + 4, &relative, sizeof(relative)))
        return false;
    const std::uintptr_t handler = kNoLimitFlag + 8 + static_cast<std::uintptr_t>(relative);
    g_idleHandler = handler;

    // remove gta's extra 14 ms idle delay
    if (!::patch::write<std::uint16_t>(kIdleDelay, 0x43EB))
        return false;

    // preserve gta's original menu/replay/audio decision:
    // this path reaches the hook with limiter enabled.
    if (!::patch::write<std::uint16_t>(kLimitFlag, 0x01B0))
        return false;

    if (!::patch::jump(kLimitHook, reinterpret_cast<const void*>(&FrameLimiterHook)))
        return false;

    // this path reaches the same hook with limiter disabled.
    if (!::patch::write<std::uint16_t>(kNoLimitFlag, 0xC030))
        return false;

    if (!::patch::jump(kNoLimitHook, reinterpret_cast<const void*>(&FrameLimiterHook)))
        return false;

    installed = true;
    return true;
}
bool game::hooks::InstallPickupHook()
{
    return ::patch::jump(0x4579C6, reinterpret_cast<const void*>(&PickupCollectedHook));
}

bool game::hooks::InstallScmHooks()
{
    const std::uint32_t opcode09C6 = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&Opcode09C6));
    const std::uint32_t opcode09D3 = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&Opcode09D3));

    if (!::patch::write(0x47BF54, opcode09C6))
        return false;

    if (!::patch::write(0x47BF88, opcode09D3))
        return false;

    return true;
}

bool game::hooks::InstallCrimeReportHook()
{
    return ::patch::call(0x4E7427, reinterpret_cast<const void*>(&CrimeReportPedHook));
}

namespace
{
    std::uint32_t g_guardDestination{};
}
bool game::hooks::BuildModelGuardDetour(std::array<std::uint8_t, 6>& result)
{
    // ff 25 [ptr] jumps through our permanently allocated handler pointer
    g_guardDestination = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&NegativeModelGuardHook));
    const auto operand = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_guardDestination));
    result = {0xFF, 0x25, 0, 0, 0, 0};
    std::memcpy(result.data() + 2, &operand, sizeof(operand));
    return true;
}
namespace
{
    WNDPROC g_originalWndProc = nullptr;
    HWND g_window = nullptr;
}
bool game::hooks::InstallWindowHook(HWND window)
{
    ZoneScoped;
    if (!window)
        return false;

    const auto current = reinterpret_cast<WNDPROC>(GetWindowLongA(window, GWL_WNDPROC));
    if (!current)
        return false;

    // another subclass can still call us; leave its wndproc chain intact
    if (g_window)
        return g_window == window && g_originalWndProc;
    if (current == game::hooks::WindowProcHook)
        return false;

    SetLastError(ERROR_SUCCESS);
    if (!SetWindowLongA(window, GWL_WNDPROC, reinterpret_cast<LONG>(game::hooks::WindowProcHook)) && GetLastError() != ERROR_SUCCESS)
        return false;

    g_originalWndProc = current;
    g_window = window;
    return true;
}
void game::hooks::RemoveWindowHook()
{
    if (g_window && g_originalWndProc)
    {
        const auto current = reinterpret_cast<WNDPROC>(GetWindowLongA(g_window, GWL_WNDPROC));
        if (current == game::hooks::WindowProcHook)
            SetWindowLongA(g_window, GWL_WNDPROC, reinterpret_cast<LONG>(g_originalWndProc));
    }

    g_originalWndProc = nullptr;
    g_window = nullptr;
}
LRESULT game::hooks::ForwardWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!g_originalWndProc)
        return DefWindowProcA(window, message, wParam, lParam);

    return CallWindowProcA(g_originalWndProc, window, message, wParam, lParam);
}

namespace game::patch::vehicle::detail
{
    UsesSirenFn originalUsesSiren = nullptr;
    VehicleAudioFn originalVehicleAudio = nullptr;
}

namespace game::patch::hudscalefix::detail
{
    DrawRadarFn g_drawRadar = nullptr;
}

namespace game::players::detail
{
    InfoFn g_info = nullptr;
    ShadowFn g_shadow = nullptr;
}

namespace game::bullets::detail
{
    DamageFn damage = nullptr;
    AffectsPedFn affectsPed = nullptr;
    InstantFn instant = nullptr;
    SniperFn sniper = nullptr;
    TraceFn trace = nullptr;
}
