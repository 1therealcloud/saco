#pragma once

#include <cstdint>

namespace game::patch::fps
{

    void set_limit(std::uint32_t fps);
    std::uint32_t get_limit();
}

namespace game::patch::fps
{
    extern float g_frameTimeMs;
    extern std::uintptr_t g_idleHandler;
}
