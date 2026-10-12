#include <stdafx.h>
#include "game/Profiler.h"

#include "d3d9hook.h"
#include "render/render.h"
#include "main.h"

namespace d3d9hook
{
    namespace
    {
        constexpr std::uintptr_t kGameDevice = 0xC97C28;
        constexpr std::size_t kResetIndex = 16;
        constexpr std::size_t kPresentIndex = 17;
        constexpr std::size_t kSetTransformIndex = 44;

        using PresentFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
        using ResetFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
        using SetTransformFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);

        PresentFn g_present = nullptr;
        ResetFn g_reset = nullptr;
        SetTransformFn g_setTransform = nullptr;
        IDirect3DDevice9* g_device = nullptr;
        bool g_installed = false;
        bool g_deviceLost = false;
        std::mutex g_matrixMutex;
        D3DMATRIX g_view{}, g_projection{};
        bool g_hasView = false, g_hasProjection = false;

        HRESULT WINAPI present(IDirect3DDevice9* device, const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty)
        {
            ZoneScoped;
#if defined(_DEBUG)
            if (device == g_device)
                profiler::BeginFrame();
#endif
            if (device == g_device && !g_deviceLost)
                Frame(device);
            HRESULT result;
            {
                PROFILE_SCOPE(Present);
                ZoneNamedN(tracyPresent, "D3D9 Present / wait", true);
                result = g_present(device, source, destination, window, dirty);
            }
#if defined(_DEBUG)
            if (device == g_device)
                profiler::EndFrame();
#endif
            if (device == g_device)
            {
                static HRESULT previous = D3D_OK;
                if (FAILED(result) && result != previous)
                    Log("[d3d9hook:present] result=%08X", static_cast<unsigned>(result));
                previous = result;
                FrameMark;
            }
            return result;
        }

        HRESULT WINAPI reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters)
        {
            ZoneScoped;
            if (device == g_device && !g_deviceLost)
            {
                g_deviceLost = true;
                {
                    std::scoped_lock lock(g_matrixMutex);
                    g_hasView = g_hasProjection = false;
                }
                render::LostUiDevice(device);
            }

            if (device == g_device)
                Log("[d3d9hook:reset] begin, lost=%d", g_deviceLost);
            const HRESULT result = g_reset(device, parameters);
            if (device == g_device)
                Log("[d3d9hook:reset] result=%08X", static_cast<unsigned>(result));
            if (device == g_device && SUCCEEDED(result) && parameters)
            {
                render::ResetUiDevice(device, *parameters);
                g_deviceLost = false;
            }
            return result;
        }

        HRESULT WINAPI set_transform(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix)
        {
            ZoneScoped;
            const HRESULT result = g_setTransform(device, state, matrix);
            if (device == g_device && SUCCEEDED(result) && matrix && (state == D3DTS_VIEW || state == D3DTS_PROJECTION))
            {
                std::scoped_lock lock(g_matrixMutex);
                if (state == D3DTS_VIEW)
                {
                    g_view = *matrix;
                    g_hasView = true;
                }
                else
                {
                    g_projection = *matrix;
                    g_hasProjection = true;
                }
            }
            return result;
        }
    }

    bool install()
    {
        ZoneScoped;
        if (g_installed)
            return true;

        auto* device = *reinterpret_cast<IDirect3DDevice9**>(kGameDevice);
        if (!device)
            return false;

        // detours compare against g_device and read the matrices, so prepare both before enabling any hook
        g_device = device;
        {
            std::scoped_lock lock(g_matrixMutex);
            g_hasView = SUCCEEDED(device->GetTransform(D3DTS_VIEW, &g_view));
            g_hasProjection = SUCCEEDED(device->GetTransform(D3DTS_PROJECTION, &g_projection));
        }

        auto** table = *reinterpret_cast<void***>(device);
        // prepare every trampoline before enabling any detour
        const struct
        {
            const char* name;
            void* target;
            void* detour;
            void** original;
        } hooks[] = {{"Reset", table[kResetIndex], reinterpret_cast<void*>(&reset), reinterpret_cast<void**>(&g_reset)},
                     {"Present", table[kPresentIndex], reinterpret_cast<void*>(&present), reinterpret_cast<void**>(&g_present)},
                     {"SetTransform", table[kSetTransformIndex], reinterpret_cast<void*>(&set_transform), reinterpret_cast<void**>(&g_setTransform)}};
        for (const auto& hook : hooks)
        {
            const auto status = MH_CreateHook(hook.target, hook.detour, hook.original);
            if (status != MH_OK)
            {
                Log("[d3d9hook] can't create %s hook, status=%d", hook.name, static_cast<int>(status));
                ExitProcess(1);
            }
        }
        for (const auto& hook : hooks)
        {
            const auto status = MH_QueueEnableHook(hook.target);
            if (status != MH_OK)
            {
                Log("[d3d9hook] can't queue %s hook, status=%d", hook.name, static_cast<int>(status));
                ExitProcess(1);
            }
        }
        const auto status = MH_ApplyQueued();
        if (status != MH_OK)
        {
            Log("[d3d9hook] can't enable hooks, status=%d", static_cast<int>(status));
            ExitProcess(1);
        }

        g_installed = true;
        Log("[d3d9hook] installed");
        return true;
    }

    bool matrices(D3DMATRIX& view, D3DMATRIX& projection)
    {
        std::scoped_lock lock(g_matrixMutex);
        if (!g_hasView || !g_hasProjection)
            return false;
        view = g_view;
        projection = g_projection;
        return true;
    }
}
