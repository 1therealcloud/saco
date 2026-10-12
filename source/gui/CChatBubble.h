#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "utils/math/CVector.h"

class CChatBubble
{
public:
    using ID = std::uint16_t;

    enum
    {
        MAX_PLAYERS = 1004,
        MAX_TEXT_LENGTH = 144
    };

    struct Player
    {
        BOOL m_bExists;        // 0x000
        char m_szText[256];    // 0x004
        int m_creationTick;    // 0x104
        int m_lifeSpan;        // 0x108
        D3DCOLOR m_color;      // 0x10C, AARRGGBB with alpha forced to FF by Add()
        float m_fDrawDistance; // 0x110
        int m_nMaxLineLength;  // 0x114, R5 actually stores the wrapped line count here
    };

public:
    Player m_player[MAX_PLAYERS]; // 0x00000

public:
    CChatBubble();

    CChatBubble(const CChatBubble&) = delete;
    CChatBubble& operator=(const CChatBubble&) = delete;

    void Add(ID player, const char* text, D3DCOLOR color, float drawDistance, int lifeSpan);
    void Draw();

private:
    std::vector<ID> m_activeIds;
};
