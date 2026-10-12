/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

#include "gui/CMenu.h"

class CControllerState;

class CMenuPool
{
public:
    enum
    {
        MAX_MENUS = 128,
        INVALID_MENU = MAX_MENUS
    };

public:
    CMenu* m_pObject[MAX_MENUS]; // 0x000
    BOOL m_bNotEmpty[MAX_MENUS]; // 0x200
    std::uint8_t m_nCurrent;     // 0x400
    bool m_bCanceled;            // 0x401

public:
    CMenuPool();
    ~CMenuPool();

    CMenuPool(const CMenuPool&) = delete;
    CMenuPool& operator=(const CMenuPool&) = delete;
    CMenu* GetAt(std::uint8_t id) const;

    CMenu* Create(std::uint8_t id, float x, float y, char columns, float firstColumnWidth, float secondColumnWidth, const CMenu::Interaction* interaction);
    BOOL Delete(std::uint8_t id);
    void Show(std::uint8_t id);
    void Hide(std::uint8_t id);
    char* GetTextPointer(const char* text);
    void Process();
    void Process(const CControllerState& current, const CControllerState& previous);
};
