#pragma once

#include <cstdint>

struct Animation
{
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
