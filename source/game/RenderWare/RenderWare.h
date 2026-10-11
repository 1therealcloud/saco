/*
    Plugin-SDK file
    Authors: GTA Community. See more here
    https://github.com/DK22Pac/plugin-sdk
    Do not delete this comment block. Respect others' work!
*/
#pragma once

#include <cstdint>
#include <d3d9.h>
#include <cassert>

namespace game::render::native
{
    template <std::uintptr_t Address, typename... Args>
    void call(Args... args)
    {
        reinterpret_cast<void(__cdecl*)(Args...)>(Address)(args...);
    }
    template <typename Result, std::uintptr_t Address, typename... Args>
    Result call_return(Args... args)
    {
        return reinterpret_cast<Result(__cdecl*)(Args...)>(Address)(args...);
    }
}

#include "rw/rwcore.h"
#include "rw/rpworld.h"
#include "rw/rtdict.h"
#include "rw/rphanim.h"
#include "rw/rpskin.h"
#include "rw/skeleton.h"
#include "rw/rwplcore.h"
#include <type_traits>

static inline auto& RwInitialized = *reinterpret_cast<bool*>(0xC920E8);
static inline auto& RwEngineInstance = *reinterpret_cast<RwGlobals**>(0xC97B24);
static inline auto& RsGlobal = *reinterpret_cast<RsGlobalType*>(0xC17040);
static inline auto& geometryTKList = *reinterpret_cast<RwPluginRegistry*>(0x8D628C);
static inline auto& RpUVAnimDictSchema = *reinterpret_cast<RtDictSchema*>(0x8DED50);
static inline auto& AmbientSaturated = *reinterpret_cast<RwRGBAReal*>(0x8E2418);

inline IDirect3DDevice9* GetD3D9Device()
{
    return *reinterpret_cast<IDirect3DDevice9**>(0xC97C28);
}

#ifndef D3DMATRIX_DEFINED
struct _D3DMATRIX;
#endif
inline _D3DMATRIX* GetD3DViewTransform()
{
    return reinterpret_cast<_D3DMATRIX*>(0xC9BC80);
}

inline _D3DMATRIX* GetD3DProjTransform()
{
    return reinterpret_cast<_D3DMATRIX*>(0x8E2458);
}

inline void _rpMaterialSetDefaultSurfaceProperties(RwSurfaceProperties* surfProps)
{
    ((void(__cdecl*)(RwSurfaceProperties*))0x74D870)(surfProps);
}

#define RWRSTATE(a) (reinterpret_cast<void*>(a))
#define PSGLOBAL(var) (((psGlobalType*)(RsGlobal.ps))->var)

struct RwResEntrySA : RwResEntry
{
    RxD3D9ResEntryHeader header;
    RxD3D9InstanceData meshData;
};

template <typename T>
inline T RwStreamRead(RwStream* stream)
{
    static_assert(std::is_trivially_copyable_v<T>);
    T data{};
    if (!stream || RwStreamRead(stream, &data, sizeof(T)) != sizeof(T))
        return T{};
    return data;
}
