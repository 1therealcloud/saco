/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <cstdint>
#include <cmath>

struct Animation
{
    static std::uint8_t EncodeFrameDelta(float value)
    {
        if (!std::isfinite(value))
            return 0;
        float byte = std::fmod(std::trunc(value), 256.0f);
        if (byte < 0)
            byte += 256.0f;
        return static_cast<std::uint8_t>(byte);
    }

    union
    {
        struct
        {
            std::uint32_t m_nId : 16;
            std::uint32_t m_nFramedelta : 8;
            std::uint32_t m_nLoopA : 1;
            std::uint32_t m_nLockX : 1;
            std::uint32_t m_nLockY : 1;
            std::uint32_t m_nLockF : 1;
            std::uint32_t m_nTime : 2;
            std::uint32_t : 2;
        };
        std::uint32_t m_value = 0;
    };
};

static_assert(sizeof(Animation) == 4);
