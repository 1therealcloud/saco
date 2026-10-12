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

#include "utils/math/CVector.h"

class CLabel
{
public:
    IDirect3DDevice9* m_pDevice; // 0x00
    void* m_pSprite;             // 0x04, original R5: ID3DXSprite*

public:
    explicit CLabel(IDirect3DDevice9* device);
    ~CLabel();

    CLabel(const CLabel&) = delete;
    CLabel& operator=(const CLabel&) = delete;

    void OnLostDevice();
    void OnResetDevice();
    bool HasNoObstacles(CVector position);
    void Begin();
    void End();
    void Draw(CVector* position, const char* text, D3DCOLOR color, BOOL shadow, bool testLineOfSight);
};
