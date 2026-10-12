/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "FrameLimiter.h"

namespace
{
    std::uint32_t g_limit = 48;

}

namespace game::patch::fps
{
    float g_frameTimeMs = 1000.0f / 48.0f;
    std::uintptr_t g_idleHandler = 0;

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
