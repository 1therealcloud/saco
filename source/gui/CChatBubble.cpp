/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "main.h"

#include "CChatBubble.h"

#include "gui/CLabel.h"
#include "net/CNetGame.h"
#include "pools/CPlayerPool.h"
#include "players/CRemotePlayer.h"
#include "game/entities/CPed.h"

#include <cmath>

namespace
{
    constexpr int kWrapLength = 36;
    constexpr int kMinimumWordTail = 12;
    constexpr std::size_t kWrapHardLimit = 512;

    D3DCOLOR ToOpaqueD3DColor(D3DCOLOR rgba)
    {
        // R5 drops the incoming alpha byte here rather than rotating it:
        // RRGGBBAA -> FFRRGGBB.
        return (rgba >> 8u) | 0xFF000000u;
    }

    int WrapText(char* text, int lineLength, int minimumWordTail)
    {
        ZoneScoped;
        if (!text)
            return -1;

        const std::size_t sourceLength = std::strlen(text);
        if (sourceLength > kWrapHardLimit)
            return -1;

        if (lineLength <= 0)
            return 1;

        int remaining = static_cast<int>(sourceLength);
        int lineStart = 0;
        int target = lineLength;
        int lines = 1;

        while (remaining > lineLength)
        {
            int split = lineLength;
            if (text[lineStart + split] != ' ')
            {
                while (split > 0 && text[lineStart + split] != ' ')
                    --split;
            }

            // This is the same branch used by samp.dll+0x17690: when the
            // closest preceding space is more than 12 chars away from the
            // 36-char boundary, insert a newline at the boundary instead of
            // replacing that distant space.
            if ((lineLength - split) > minimumWordTail)
            {
                const std::size_t tailLength = std::strlen(text + target);
                std::memmove(text + target + 1, text + target, tailLength + 1);
                text[target] = '\n';

                remaining -= lineLength;
                lineStart = target + 1;
                target = lineStart + lineLength;
            }
            else
            {
                lineStart += split;
                remaining -= split;
                text[lineStart] = '\n';
                target = lineStart + lineLength;
            }

            ++lines;
        }

        return lines;
    }
}

CChatBubble::CChatBubble()
{
    // R5 zeroes the full 0x44A20-byte object in the constructor.
    std::memset(m_player, 0, sizeof(m_player));
}

void CChatBubble::Add(ID player, const char* text, D3DCOLOR color, float drawDistance, int lifeSpan)
{
    ZoneScoped;
    if (player >= MAX_PLAYERS || !text || !std::isfinite(drawDistance) || drawDistance < 0 || lifeSpan < 0)
        return;

    const auto active = std::lower_bound(m_activeIds.begin(), m_activeIds.end(), player);
    if (active != m_activeIds.end() && *active == player)
        m_activeIds.erase(active);
    Player& bubble = m_player[player];

    // The original clears an existing entry before validating the new string.
    std::memset(&bubble, 0, sizeof(bubble));

    const std::size_t length = std::strlen(text);
    if (length > MAX_TEXT_LENGTH)
        return;

    std::memcpy(bubble.m_szText, text, length + 1);
    bubble.m_creationTick = static_cast<int>(GetTickCount());
    bubble.m_lifeSpan = lifeSpan;
    bubble.m_color = ToOpaqueD3DColor(color);
    bubble.m_fDrawDistance = drawDistance;
    bubble.m_nMaxLineLength = WrapText(bubble.m_szText, kWrapLength, kMinimumWordTail);
    bubble.m_bExists = TRUE;
    m_activeIds.insert(std::lower_bound(m_activeIds.begin(), m_activeIds.end(), player), player);
}

void CChatBubble::Delete(ID player)
{
    if (player >= MAX_PLAYERS)
        return;
    m_player[player] = {};
    const auto active = std::lower_bound(m_activeIds.begin(), m_activeIds.end(), player);
    if (active != m_activeIds.end() && *active == player)
        m_activeIds.erase(active);
}

void CChatBubble::Clear()
{
    std::memset(m_player, 0, sizeof(m_player));
    m_activeIds.clear();
}

void CChatBubble::Draw()
{
    ZoneScoped;

    if (!g_label)
        return;

    const std::uint32_t now = GetTickCount();

    bool begun = false;

    for (const ID id : m_activeIds)
    {
        Player& bubble = m_player[id];
        if (!bubble.m_bExists)
            continue;

        const std::uint32_t created = static_cast<std::uint32_t>(bubble.m_creationTick);
        const std::uint32_t lifeSpan = static_cast<std::uint32_t>(bubble.m_lifeSpan);
        if (now - created >= lifeSpan)
        {
            bubble.m_bExists = FALSE;
            continue;
        }

        CVector position{};
        float distanceToLocal = 0.0f;
        float distanceToCamera = 0.0f;
        auto* netGame = g_netGame.get();
        auto* playerPool = netGame ? netGame->GetPlayerPool() : nullptr;
        auto* remote = playerPool ? playerPool->GetPlayer(id) : nullptr;
        auto* ped = remote ? remote->m_pPed : nullptr;
        if (!ped || !ped->DoesExist() || !ped->IsOnScreen())
            continue;
        ped->GetBonePosition(8, &position);
        distanceToLocal = ped->GetDistanceToLocalPlayer();
        distanceToCamera = ped->GetDistanceToCamera();

        if (distanceToLocal > bubble.m_fDrawDistance)
            continue;

        const int lineCount = bubble.m_nMaxLineLength > 0 ? bubble.m_nMaxLineLength : 1;
        const float extraLines = static_cast<float>(lineCount - 1);

        // Exact x87 expression reconstructed from R5 CChatBubble::Draw.
        position.z += 0.2f + 0.02f * extraLines + distanceToCamera * (0.065f + 0.0125f * extraLines);

        if (!begun)
        {
            g_label->Begin();
            begun = true;
        }
        g_label->Draw(&position, bubble.m_szText, bubble.m_color, TRUE, false);
    }

    if (begun)
        g_label->End();
    std::erase_if(m_activeIds, [this](ID id) {
        return !m_player[id].m_bExists;
    });
}
