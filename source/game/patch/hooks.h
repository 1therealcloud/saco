#pragma once

#include "game/RenderWare/rw/skeleton.h"

using EventHandlerFn = RsEventStatus(__cdecl*)(RsEvent, void*);
extern EventHandlerFn g_eventHandler;

void __cdecl ShutdownHook();
RsEventStatus __cdecl EventHandlerHook(RsEvent event, void* param);
void FrameLimiterHook();
void MovementAnimationHook();
void* __fastcall PlayerInfoHook(void* ped, void*);
void* __fastcall ShadowHook(void* manager, void*, void* ped);
void __fastcall ProcessControlHook(void* ped, void*);
void __cdecl DrawRadarHook();
void __cdecl DrawCrossHairsHook();

void PickupCollectedHook();
void CameraProcessHook();
void Opcode09C6();
void Opcode09D3();
void __cdecl WorldRenderHook();
void __cdecl FadingRenderHook();
void __cdecl NativeTextHook();
void __cdecl GangZoneRadarHook(bool menu);
const char* __fastcall MenuTextHook(void* text, void*, const char* key);
void* __cdecl LoadObjectInstanceHook(void* instance, const char* name);
void __fastcall EntityDestroyedHook(void* entity, void*);
void __cdecl OcclusionInitializedHook();
void __cdecl PoolsShutdownHook();
void __cdecl RenderObjectHook(void* entity);
bool __cdecl CameraToIgnoreObjectHook(void* entity);

namespace game::hooks
{
    using DrawFn = void(__cdecl*)();
    using LoadInstanceFn = void*(__cdecl*)(void*, const char*);
    using DestroyEntityFn = void(__thiscall*)(void*);
    using MenuTextFn = const char*(__thiscall*)(void*, const char*);
    using ObjectRenderFn = void(__cdecl*)(void*);
    using CameraIgnoreFn = bool(__cdecl*)(void*);
    extern DrawFn g_world, g_fading, g_text;
    extern LoadInstanceFn originalLoad;
    extern DestroyEntityFn originalDestroy;
    extern DrawFn originalOcclusionInit, originalPoolsShutdown;
    extern MenuTextFn g_getText;
    extern ObjectRenderFn g_originalRender;
    extern CameraIgnoreFn g_originalCameraIgnore;
}

void EmptyVertexStreamAllocateHook();
void EmptyVertexStreamLockHook();
void EmptyVertexStreamRelockHook();

bool __fastcall UsesSirenHook(void* vehicle, void*);
void __fastcall TrailerProcessHook(void* vehicle, void*);
void __fastcall VehicleAudioHook(void* audio, void*);
