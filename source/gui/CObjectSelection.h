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

#include "CObjectEdit.h"

class CFonts;

class CObjectSelection
{
public:
    using ID = std::uint16_t;

    enum SelectionType
    {
        SELECT_OBJECT_GLOBAL_OBJECT = 1,
        SELECT_OBJECT_PLAYER_OBJECT = 2
    };

    struct ObjectInfo;

    struct ObjectInfo
    {
        ID id = 0xFFFF;
        int type = 0;
        int modelId = 0;
        CObjectEdit::Matrix matrix{};
        float distance = 0.0f;
    };

public:
    BOOL m_bIsActive;    // 0x00
    ID m_nHoveredObject; // 0x04

public:
    CObjectSelection();
    ~CObjectSelection();
    CObjectSelection(const CObjectSelection&) = delete;
    CObjectSelection& operator=(const CObjectSelection&) = delete;

    ID DefineObject();
    void DrawLabels();
    void Enable(BOOL enable);
    void Draw();

    void SendNotification();

    BOOL MsgProc(int uMsg, int wParam, int lParam);
};
