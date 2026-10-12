/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#if defined(LEGACY_SPAWN_SCREEN)

#   include <Windows.h>
#   include <d3d9.h>
#   include <cstddef>

class CFonts;

class CSpawnScreen
{
public:
    BOOL m_bEnabled;                          // 0x00
    char* m_szSpawnText;                      // 0x04
    CFonts* m_pFont;                          // 0x08
    IDirect3DDevice9* m_pDevice;              // 0x0C
    IDirect3DTexture9* m_pTexture;            // 0x10
    IDirect3DStateBlock9* m_pStateBlockSaved; // 0x14
    IDirect3DStateBlock9* m_pStateBlockDraw;  // 0x18
    void* m_pSprite;                          // 0x1C, unused R5/D3DX ABI slot

public:
    explicit CSpawnScreen(IDirect3DDevice9* device);
    ~CSpawnScreen();

    CSpawnScreen(const CSpawnScreen&) = delete;
    CSpawnScreen& operator=(const CSpawnScreen&) = delete;

    void SetText(const char* text);
    void OnResetDevice();
    void OnLostDevice();
    void Draw();

private:
    bool LoadLogoTexture();
    bool CreateStateBlocks();
};

#endif // LEGACY_SPAWN_SCREEN
