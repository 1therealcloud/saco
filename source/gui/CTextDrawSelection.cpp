/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>
#include "main.h"
#include "net/CNetGame.h"

#include "game/CGame.h"
#include "render/render.h"

#include "CTextDrawSelection.h"

#include "pools/CTextDrawPool.h"

namespace
{
    constexpr CTextDrawSelection::ID INVALID_TEXTDRAW_ID = 0xFFFF;

    HWND g_window = nullptr;
    bool g_hudHidden = false;
    std::uint8_t g_savedHud = 0;
    std::uint8_t g_savedRadar = 0;

    D3DCOLOR ConvertHoveredColor(D3DCOLOR color)
    {
        return ((color & 0x000000FFu) << 24) | ((color & 0x0000FF00u) << 8) | ((color & 0x00FF0000u) >> 8) | ((color & 0xFF000000u) >> 24);
    }

    HWND GetGameWindow()
    {
        if (g_window)
            return g_window;

        return g_game ? static_cast<HWND>(g_game->GetWindowHandle()) : nullptr;
    }

    CTextDrawPool* GetTextDrawPool()
    {
        ZoneScoped;
        return g_netGame && g_netGame->m_pPools ? g_netGame->m_pPools->m_pTextDraw : nullptr;
    }

    void EnableHud(BOOL enable)
    {
        ZoneScoped;
        if (enable)
        {
            if (!g_hudHidden)
                return;
            CGame::SetHudState(g_savedHud);
            CGame::SetRadarState(g_savedRadar);
            g_hudHidden = false;
        }
        else
        {
            if (!g_hudHidden)
            {
                g_savedHud = CGame::GetHudState();
                g_savedRadar = CGame::GetRadarState();
                g_hudHidden = true;
            }
            CGame::SetHudState(0);
            CGame::SetRadarState(1);
        }
    }
}

CTextDrawSelection::CTextDrawSelection() : m_bIsActive(FALSE), m_hoveredColor(0xFFFFFFFFu), m_nHoveredId(INVALID_TEXTDRAW_ID)
{
    ZoneScoped;
}

CTextDrawSelection::~CTextDrawSelection()
{
    ZoneScoped;
    if (m_bIsActive)
        Disable(false);
}

void CTextDrawSelection::ResetTextDraws()
{
    ZoneScoped;
    CTextDrawPool* pool = GetTextDrawPool();
    if (!pool)
        return;

    for (const auto i : pool->GetActiveIds())
    {
        if (!pool->m_bNotEmpty[i])
            continue;

        CTextDraw* textDraw = pool->m_pObject[i];
        if (!textDraw)
            continue;

        textDraw->m_data.m_bSelected = 0;
        textDraw->m_data.m_selectedColor = 0;
    }
}

void CTextDrawSelection::RawProcess()
{
    ZoneScoped;
    m_nHoveredId = INVALID_TEXTDRAW_ID;
    POINT point{};
    const auto window = GetGameWindow();
    if (!window || GetForegroundWindow() != GetAncestor(window, GA_ROOT) || !GetCursorPos(&point) || !ScreenToClient(window, &point))
    {
        ResetTextDraws();
        return;
    }

    RawProcess(point);
}

void CTextDrawSelection::RawProcess(POINT point)
{
    ZoneScoped;
    m_nHoveredId = INVALID_TEXTDRAW_ID;

    CTextDrawPool* pool = GetTextDrawPool();
    if (!pool)
        return;

    m_nHoveredId = INVALID_TEXTDRAW_ID;

    for (const auto id : pool->GetActiveIds())
    {
        if (!pool->m_bNotEmpty[id])
            continue;

        CTextDraw* textDraw = pool->m_pObject[id];
        if (!textDraw)
            continue;

        textDraw->m_data.m_bSelected = 0;
        textDraw->m_data.m_selectedColor = 0;

        if (!textDraw->m_data.m_bSelectable || !textDraw->m_data.m_bDrawnThisFrame)
            continue;

        RECT rect = textDraw->GetComputedRect();
        if (!PtInRect(&rect, point))
            continue;

        m_nHoveredId = id;
        textDraw->m_data.m_bSelected = 1;
        textDraw->m_data.m_selectedColor = m_hoveredColor;
    }
}

void CTextDrawSelection::Process()
{
    ZoneScoped;
    if (!m_bIsActive)
        return;

    render::SetCursorMode(2, false);

    EnableHud(FALSE);

    RawProcess();
}

void CTextDrawSelection::Enable(D3DCOLOR hoveredColor)
{
    ZoneScoped;
    m_bIsActive = TRUE;
    m_hoveredColor = ConvertHoveredColor(hoveredColor);
    m_nHoveredId = INVALID_TEXTDRAW_ID;
    EnableHud(FALSE);
    render::SetCursorMode(2, false);
}

void CTextDrawSelection::SendNotification()
{
    ZoneScoped;
    if (g_netGame)
        g_netGame->SelectTextDraw(m_nHoveredId);
}

void CTextDrawSelection::Disable(bool notify)
{
    ZoneScoped;
    if (!m_bIsActive)
        return;
    m_bIsActive = FALSE;
    EnableHud(TRUE);

    render::SetCursorMode(0, false);

    m_nHoveredId = INVALID_TEXTDRAW_ID;
    if (notify)
        SendNotification();
    ResetTextDraws();
}

BOOL CTextDrawSelection::MsgProc(int uMsg, int wParam, int lParam)
{
    ZoneScoped;
    (void)wParam;
    (void)lParam;

    if (!m_bIsActive || uMsg != WM_LBUTTONUP)
        return FALSE;

    RawProcess(POINT{static_cast<SHORT>(LOWORD(lParam)), static_cast<SHORT>(HIWORD(lParam))});
    if (m_nHoveredId != INVALID_TEXTDRAW_ID)
        SendNotification();

    return TRUE;
}

void CTextDrawSelection::SetWindow(HWND window)
{
    ZoneScoped;
    g_window = window;
}
