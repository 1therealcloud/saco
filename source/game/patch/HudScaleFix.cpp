#include <stdafx.h>

#include "HudScaleFix.h"
#include "hooks.h"

#include <atomic>
#include <cmath>

namespace game::patch::hudscalefix
{
    namespace detail
    {
        constexpr std::uintptr_t kHudScaleX = 0x00859520;
        constexpr std::uintptr_t kHudScaleY = 0x00859524;
        constexpr std::uintptr_t kScreenWidth = 0x00C17044;
        constexpr std::uintptr_t kScreenHeight = 0x00C17048;

        constexpr std::uintptr_t kRadarWidth = 0x00866B74;
        constexpr std::uintptr_t kRadarHeight = 0x00866B78;

        constexpr std::uintptr_t kDrawRadar = 0x0058A330;
        constexpr std::uintptr_t kDrawCrossHairsCall = 0x0058FBBF;

        constexpr float kMinAspectRatio = 1.6f;
        constexpr float kFixedScaleX = 0.00222f;
        constexpr float kFixedScaleY = 0.00242f;
        constexpr float kFixedRadarWidth = 82.0f;
        constexpr float kFixedRadarHeight = 96.0f;

        std::atomic_bool g_enabled{true};
        DrawRadarFn g_drawRadar = nullptr;

        float& RefFloat(std::uintptr_t address)
        {
            return *reinterpret_cast<float*>(address);
        }

        int ReadInt(std::uintptr_t address)
        {
            return *reinterpret_cast<const volatile int*>(address);
        }

        bool Apply(ScaleState& state)
        {
            if (!g_enabled.load(std::memory_order_relaxed))
                return false;

            const int width = ReadInt(kScreenWidth);
            const int height = ReadInt(kScreenHeight);
            if (width <= 0 || height <= 0)
                return false;

            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            if (!std::isfinite(aspect) || aspect < kMinAspectRatio)
                return false;

            state.scaleX = RefFloat(kHudScaleX);
            state.scaleY = RefFloat(kHudScaleY);
            state.radarWidth = RefFloat(kRadarWidth);
            state.radarHeight = RefFloat(kRadarHeight);

            RefFloat(kRadarWidth) = kFixedRadarWidth;
            RefFloat(kRadarHeight) = kFixedRadarHeight;
            RefFloat(kHudScaleY) = kFixedScaleY;
            RefFloat(kHudScaleX) = kFixedScaleX / aspect;

            state.applied = true;
            return true;
        }

        void Restore(const ScaleState& state)
        {
            if (!state.applied)
                return;

            RefFloat(kHudScaleX) = state.scaleX;
            RefFloat(kHudScaleY) = state.scaleY;
            RefFloat(kRadarWidth) = state.radarWidth;
            RefFloat(kRadarHeight) = state.radarHeight;
        }

    }

    using namespace detail;

    ScopedScaleFix::ScopedScaleFix(bool apply)
    {
        if (apply)
            detail::Apply(m_state);
    }

    ScopedScaleFix::~ScopedScaleFix()
    {
        detail::Restore(m_state);
    }

    bool install()
    {
        static bool installed = false;
        if (installed)
            return false;

        // the scoped draw wrappers update these constants every frame
        DWORD oldProtect{};
        if (!VirtualProtect(reinterpret_cast<void*>(kHudScaleX), sizeof(float) * 2, PAGE_READWRITE, &oldProtect) ||
            !VirtualProtect(reinterpret_cast<void*>(kRadarWidth), sizeof(float) * 2, PAGE_READWRITE, &oldProtect))
            return false;

        // vital stats redirects here too while TAB is held
        if (!hook::install(kDrawRadar, &DrawRadarHook, &g_drawRadar))
            return false;

        if (!::patch::call(kDrawCrossHairsCall, reinterpret_cast<const void*>(&DrawCrossHairsHook)))
            return false;

        installed = true;
        Log("[game::patch::hudscalefix] installed");
        return true;
    }

    void set_enabled(bool enabled)
    {
        g_enabled.store(enabled, std::memory_order_relaxed);
        Log("[game::patch::hudscalefix] %s", enabled ? "enabled" : "disabled");
    }

    bool is_enabled()
    {
        return g_enabled.load(std::memory_order_relaxed);
    }
}
