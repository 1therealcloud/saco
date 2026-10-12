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
#include "game/entities/CPed.h"
#include "pools/CActorPool.h"
#include "pools/CVehiclePool.h"
#include "players/CRemotePlayer.h"
#include <intrin.h>

namespace game::hooks
{
    void* crimeReportPed = nullptr;
}

void* __cdecl CrimeReportPedHook(int)
{
    return game::hooks::crimeReportPed;
}

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

std::uint32_t __fastcall WeaponSkillHook(void* ped, void*)
{
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
    bool InstallRenderHooks()
    {
        return hook::install(0x53E230, &NativeTextHook, &g_text) && hook::install(0x586650, &GangZoneRadarHook) && hook::install(0x553AA0, &WorldRenderHook, &g_world) &&
               hook::install(0x5531E0, &FadingRenderHook, &g_fading);
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

bool __fastcall FireInstantHitHook(void* weapon, void*, void* owner, CVector* origin, CVector* muzzle, void* target, CVector* targetPosition, CVector* driveBy, bool crosshair,
                                   int extra)
{
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

void __fastcall ComputeDamageResponseHook(game::bullets::detail::DamageCalculator* calculator, void*, void* victim, void* response, bool speak)
{
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
