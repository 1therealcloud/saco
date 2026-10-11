#include <stdafx.h>

#include "FrameLimiter.h"
#include "hooks.h"

namespace
{
    constexpr std::uintptr_t kIdleDelay = 0x53E923;

    constexpr std::uintptr_t kLimitFlag = 0x748D81;
    constexpr std::uintptr_t kLimitHook = 0x748D83;

    constexpr std::uintptr_t kNoLimitFlag = 0x748D98;
    constexpr std::uintptr_t kNoLimitHook = 0x748D9A;

    std::uint32_t g_limit = 48;
    bool g_installed = false;

}

namespace game::patch::fps
{
    float g_frameTimeMs = 1000.0f / 48.0f;
    std::uintptr_t g_idleHandler = 0;
    bool install()
    {
        if (g_installed)
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

        g_installed = true;
        Log("[framelimiter] installed, fpslimit=%u", g_limit);
        return true;
    }

    void set_limit(std::uint32_t fps)
    {
        if (fps < 20 || fps > 90)
            fps = 48;
        g_limit = fps;
        g_frameTimeMs = 1000.0f / static_cast<float>(fps);
    }

    std::uint32_t get_limit()
    {
        return g_limit;
    }
}
