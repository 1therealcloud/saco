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

#include <cstdint>

struct RwTexture;

class CObjectMaterialText
{
public:
    // samp.dll 0.3.7-R5 + 0x70880
    explicit CObjectMaterialText(IDirect3DDevice9* device);

    // samp.dll 0.3.7-R5 + 0x708A0 -> +0x70830
    ~CObjectMaterialText();

    CObjectMaterialText(const CObjectMaterialText&) = delete;
    CObjectMaterialText& operator=(const CObjectMaterialText&) = delete;

    // samp.dll 0.3.7-R5 + 0x70830
    void DeleteDeviceObjects();
    void ClearCache();

    // samp.dll 0.3.7-R5 + 0x70860
    void RestoreDeviceObjects();

    // Project lifecycle aliases. The original R5 reset path calls the two
    // methods above directly.
    void OnLostDevice()
    {
        DeleteDeviceObjects();
    }

    void OnResetDevice()
    {
        RestoreDeviceObjects();
    }

    // samp.dll 0.3.7-R5 + 0x708B0
    IDirect3DTexture9* Create(const char* text, const char* font, int fontSize, int width, int height, D3DCOLOR fontColor, D3DCOLOR backgroundColor, bool bold, std::uint8_t align);

    // adapts the generated d3d texture to our owned renderware material texture
    RwTexture* CreateRwTexture(const char* text, const char* font, int fontSize, int width, int height, D3DCOLOR fontColor, D3DCOLOR backgroundColor, bool bold, std::uint8_t align,
                               bool* cacheHit = nullptr);

private:
    // Exact x86 R5 object layout. R5 stores two ID3DXSprite* values at +0x04
    // and +0x08. SACO keeps the slots for layout parity but deliberately does
    // not depend on D3DX; text rendering is handled by CFont instead.
    IDirect3DDevice9* m_pDevice; // 0x00
    void* m_pTextSprite;         // 0x04, ID3DXSprite* in R5
    void* m_pSprite;             // 0x08, ID3DXSprite* in R5
};
