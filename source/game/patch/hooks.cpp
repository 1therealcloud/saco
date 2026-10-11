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

using namespace game::patch::fps;
using namespace game::players::detail;
using namespace game::patch::hudscalefix::detail;
using game::patch::hudscalefix::ScopedScaleFix;

void __fastcall VehicleAudioHook(void* audio, void*)
{
    using namespace game::patch::vehicle::detail;
    auto* vehicle = static_cast<std::uint8_t*>(audio) - 0x138;
    auto* driver = *reinterpret_cast<std::uint8_t**>(vehicle + 0x460);
    auto* localPed = *reinterpret_cast<void**>(0xB7CD98);
    if (!driver || driver == localPed || !FindSlot(driver))
    {
        originalVehicleAudio(audio);
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
}

void __cdecl ShutdownHook()
{
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
    SetPositionUpdate(false);
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

void __cdecl DrawRadarHook()
{
    ScopedScaleFix fix(true);
    g_drawRadar();
}

void __cdecl DrawCrossHairsHook()
{
    // R5 only applies the HUD scale correction to this crosshair camera mode.
    const auto cameraMode = *reinterpret_cast<const volatile std::uint8_t*>(0x00B6F1A8);
    ScopedScaleFix fix(cameraMode == 0x35);
    reinterpret_cast<void(__cdecl*)()>(0x0058E020)();
}

namespace game::hooks
{
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
    game::hooks::g_world();
    render::RenderWorldObjects();
}

void __cdecl FadingRenderHook()
{
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

static void __cdecl UpdateAttachedCamera()
{
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

bool __fastcall UsesSirenHook(void* vehicle, void*)
{
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
