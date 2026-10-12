/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

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
