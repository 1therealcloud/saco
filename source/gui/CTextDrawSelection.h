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

class CTextDrawPool;

class CTextDrawSelection
{
public:
    using ID = std::uint16_t;

public:
    BOOL m_bIsActive;        // 0x00
    D3DCOLOR m_hoveredColor; // 0x04
    ID m_nHoveredId;         // 0x08

public:
    CTextDrawSelection();
    ~CTextDrawSelection();

    void ResetTextDraws();
    void RawProcess();
    void RawProcess(POINT clientPosition);
    void Process();
    void Enable(D3DCOLOR hoveredColor);
    void SendNotification();
    void Disable(bool notify = true);
    BOOL MsgProc(int uMsg, int wParam, int lParam);

    static void SetWindow(HWND window);
};
