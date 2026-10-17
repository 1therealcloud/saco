/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

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
        BOOL m_bExists;
        char m_szText[MAX_TEXT_LENGTH * 4 + 128];
        int m_creationTick;
        int m_lifeSpan;
        D3DCOLOR m_color;
        float m_fDrawDistance;
        int m_nMaxLineLength;
    };

public:
    Player m_player[MAX_PLAYERS]; // 0x00000

public:
    CChatBubble();

    CChatBubble(const CChatBubble&) = delete;
    CChatBubble& operator=(const CChatBubble&) = delete;

    void Add(ID player, const char* text, D3DCOLOR color, float drawDistance, int lifeSpan);
    void Delete(ID player);
    void Clear();
    void Draw();

private:
    std::vector<ID> m_activeIds;
};
