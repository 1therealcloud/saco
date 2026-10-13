/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>
#include "game/Scripting.h"
#include "sync/KeyStuff.h"
#include "game/CPad.h"

#include "CMenuPool.h"
#include "main.h"
#include "net/CNetGame.h"
#include "game/ControllerState.h"

#include <new>

CMenuPool::CMenuPool() : m_pObject{}, m_bNotEmpty{}, m_nCurrent(INVALID_MENU), m_bCanceled(false)
{}

CMenuPool::~CMenuPool()
{
    for (std::uint8_t id = 0; id < MAX_MENUS; ++id)
        Delete(id);
}

CMenu* CMenuPool::GetAt(std::uint8_t id) const
{
    return id < MAX_MENUS && m_bNotEmpty[id] ? m_pObject[id] : nullptr;
}

CMenu* CMenuPool::Create(std::uint8_t id, float x, float y, char columns, float firstColumnWidth, float secondColumnWidth, const CMenu::Interaction* interaction)
{
    ZoneScoped;
    if (id >= MAX_MENUS)
        return nullptr;
    auto* menu = new (std::nothrow) CMenu(x, y, columns, firstColumnWidth, secondColumnWidth, interaction);
    if (!menu)
        return nullptr;
    Delete(id);
    menu->m_nId = id;
    m_pObject[id] = menu;
    m_bNotEmpty[id] = TRUE;
    return menu;
}

BOOL CMenuPool::Delete(std::uint8_t id)
{
    ZoneScoped;
    if (id >= MAX_MENUS || !m_pObject[id])
        return FALSE;
    Hide(id);
    delete m_pObject[id];
    m_pObject[id] = nullptr;
    m_bNotEmpty[id] = FALSE;
    return TRUE;
}

void CMenuPool::Show(std::uint8_t id)
{
    ZoneScoped;
    CMenu* menu = GetAt(id);
    if (!menu)
        return;
    Hide(m_nCurrent);
    const auto* pad = KeyStuff::GetInternalKeys();
    m_restorePlayerControl = !pad || !pad->bPlayerSafe;
    menu->Show();
    m_nCurrent = menu->IsVisible() ? id : INVALID_MENU;
    m_bCanceled = false;
    constexpr scm::OpcodeInfo opSetControl{0x01B4, "ii"};
    if (m_nCurrent != INVALID_MENU)
        scm::ProcessCommand(&opSetControl, 0, 0);
}

void CMenuPool::Hide(std::uint8_t id)
{
    ZoneScoped;
    if (id >= MAX_MENUS || id != m_nCurrent)
        return;
    if (CMenu* menu = GetAt(id))
        menu->Hide();
    m_nCurrent = INVALID_MENU;
    m_bCanceled = false;
    constexpr scm::OpcodeInfo opSetControl{0x01B4, "ii"};
    scm::ProcessCommand(&opSetControl, 0, int(m_restorePlayerControl));
}

void CMenuPool::UpdatePlayerControllable(bool enabled)
{
    if (m_nCurrent == INVALID_MENU)
        return;
    m_restorePlayerControl = enabled;
    constexpr scm::OpcodeInfo opSetControl{0x01B4, "ii"};
    scm::ProcessCommand(&opSetControl, 0, 0);
}

char* CMenuPool::GetTextPointer(const char* text)
{
    ZoneScoped;
    CMenu* menu = GetAt(m_nCurrent);
    if (!menu || !menu->IsVisible() || !text)
        return nullptr;
    if (std::strncmp(text, "SAMP", 4) == 0)
        text += 4;
    if (std::strcmp(text, "HED") == 0)
        return menu->GetTitle();
    if (std::strcmp(text, "RW1") == 0)
        return menu->GetColumnTitle(0);
    if (std::strcmp(text, "RW2") == 0)
        return menu->GetColumnTitle(1);
    if (strnlen_s(text, 4) != 3 || (text[0] != '0' && text[0] != '1') || text[1] < '0' || text[1] > '9' || text[2] < '0' || text[2] > '9')
        return nullptr;
    const auto column = static_cast<std::uint8_t>(text[0] - '0');
    const auto row = static_cast<std::uint8_t>((text[1] - '0') * 10 + text[2] - '0');
    return menu->GetItem(column, row);
}

void CMenuPool::Process()
{
    ZoneScoped;

    if (m_nCurrent == INVALID_MENU)
        return;
    CPad snapshot{};
    const auto* internalKeys = KeyStuff::GetInternalKeys();
    if (!internalKeys)
        return;
    std::memcpy(&snapshot, internalKeys, sizeof(snapshot));
    Process(snapshot.NewState, snapshot.OldState);
}

void CMenuPool::Process(const CControllerState& current, const CControllerState& previous)
{
    ZoneScoped;
    CMenu* menu = GetAt(m_nCurrent);
    if (!menu || !menu->IsVisible())
    {
        Hide(m_nCurrent);
        return;
    }
    // close on the next tick; don't emit the same event again before closing
    if (m_bCanceled)
    {
        Hide(m_nCurrent);
        return;
    }
    if (current.ButtonCross && !previous.ButtonCross)
    {
        const auto row = static_cast<std::uint8_t>(menu->GetActiveRow());
        if (row >= CMenu::MAX_MENU_ITEMS || !menu->m_interaction.m_bRow[row])
            return;
        if (g_netGame)
            g_netGame->MenuSelect(row);
        m_bCanceled = true;
    }
    else if (current.ButtonTriangle && !previous.ButtonTriangle)
    {
        if (g_netGame)
            g_netGame->MenuQuit();
        m_bCanceled = true;
    }
}
