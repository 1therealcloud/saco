/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#   define NOMINMAX
#endif

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
struct RwTexture;

class CLicensePlate
{
public:
    static constexpr const char* DEFAULT_PLATE_FONT = "Arial";
    static constexpr const char* DEFAULT_PLATE_TEXT = "XYZSR998";

    enum : D3DCOLOR
    {
        DEFAULT_PLATE_TEXT_COLOR = 0xEE444470u,
        DEFAULT_PLATE_BG_COLOR = 0xFFBEB6A8u
    };

public:
    // Exact SA-MP 0.3.7-R5 x86 layout.
    IDirect3DDevice9* m_pDevice;        // 0x00
    void* m_pRenderer;                  // 0x04, ID3DXRenderToSurface* in R5
    IDirect3DTexture9* m_pTexture;      // 0x08, 128x32 render-target texture
    IDirect3DSurface9* m_pSurface;      // 0x0C, level 0 of m_pTexture
    D3DDISPLAYMODE m_displayMode;       // 0x10
    IDirect3DTexture9* m_pDefaultPlate; // 0x20

public:
    // samp.dll 0.3.7-R5 + 0x6D9B0
    explicit CLicensePlate(IDirect3DDevice9* device);

    // samp.dll 0.3.7-R5 + 0x6D9E0 -> +0x6D7B0
    ~CLicensePlate();

    CLicensePlate(const CLicensePlate&) = delete;
    CLicensePlate& operator=(const CLicensePlate&) = delete;

    // samp.dll 0.3.7-R5 + 0x6D7B0
    void OnLostDevice();

    // samp.dll 0.3.7-R5 + 0x6D800
    void OnResetDevice();

    // samp.dll 0.3.7-R5 + 0x6D880
    IDirect3DTexture9* Create(const char* text);
    // copies the plate just rendered by Create into a native gta texture
    RwTexture* CreateRwTexture();
};
