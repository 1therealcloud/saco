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

class CTextDraw
{
public:
    struct Vector3
    {
        float x;
        float y;
        float z;
    };

    struct Transmit
    {
        union
        {
            struct
            {
                std::uint8_t m_bBox : 1;
                std::uint8_t m_bLeft : 1;
                std::uint8_t m_bRight : 1;
                std::uint8_t m_bCenter : 1;
                std::uint8_t m_bProportional : 1;
            };
            std::uint8_t m_nFlags;
        };

        float m_fLetterWidth;
        float m_fLetterHeight;
        D3DCOLOR m_letterColor;
        float m_fBoxWidth;
        float m_fBoxHeight;
        D3DCOLOR m_boxColor;
        std::uint8_t m_nShadow;
        std::uint8_t m_bOutline;
        D3DCOLOR m_backgroundColor;
        std::uint8_t m_nStyle;
        std::uint8_t m_bSelectable;
        float m_fX;
        float m_fY;
        std::uint16_t m_nModel;
        Vector3 m_rotation;
        float m_fZoom;
        std::uint16_t m_aColor[2];
    };

    struct Data
    {
        float m_fLetterWidth;             // 0x00
        float m_fLetterHeight;            // 0x04
        D3DCOLOR m_letterColor;           // 0x08
        std::uint8_t m_nUnknown;          // 0x0C
        std::uint8_t m_bCenter;           // 0x0D
        std::uint8_t m_bBox;              // 0x0E
        float m_fBoxSizeX;                // 0x0F
        float m_fBoxSizeY;                // 0x13
        D3DCOLOR m_boxColor;              // 0x17
        std::uint8_t m_bProportional;     // 0x1B
        D3DCOLOR m_backgroundColor;       // 0x1C
        std::uint8_t m_nShadow;           // 0x20
        std::uint8_t m_nOutline;          // 0x21
        std::uint8_t m_bLeft;             // 0x22
        std::uint8_t m_bRight;            // 0x23
        std::int32_t m_nStyle;            // 0x24
        float m_fX;                       // 0x28
        float m_fY;                       // 0x2C
        std::uint8_t m_pad38[8];          // 0x30
        std::uint32_t m_nTextureSlot;     // 0x38 / field_99B
        std::uint32_t m_nTextureSlot2;    // 0x3C / field_99F
        std::uint32_t m_nIndex;           // 0x40 / texture-resource index
        std::uint8_t m_bSelectable;       // 0x44
        std::uint16_t m_nModel;           // 0x45
        Vector3 m_rotation;               // 0x47
        float m_fZoom;                    // 0x53
        std::uint16_t m_aColor[2];        // 0x57
        std::uint8_t m_nUnknown9BE;       // 0x5B
        std::uint8_t m_bDrawnThisFrame;   // 0x5C
        std::uint8_t m_bTextContainsKeys; // 0x5D
        std::uint32_t m_nComputedLeft;    // 0x5E
        std::uint32_t m_nComputedTop;     // 0x62
        std::uint32_t m_nComputedRight;   // 0x66
        std::uint32_t m_nComputedBottom;  // 0x6A
        std::uint8_t m_bSelected;         // 0x6E
        D3DCOLOR m_selectedColor;         // 0x6F
    };

public:
    char m_szText[801];    // 0x000
    char m_szString[1602]; // 0x321
    Data m_data;           // 0x963

public:
    CTextDraw(const Transmit* transmit, const char* text);
    ~CTextDraw();

    CTextDraw(const CTextDraw&) = delete;
    CTextDraw& operator=(const CTextDraw&) = delete;

    void SetText(const char* text);
    void Draw();

    void LoadPreview();
    void UnloadPreview();

    RECT GetComputedRect() const;

    // only after all textdraws have been destroyed
    static void ResetResourcePool();
    static void BeginDrawPass();
    static void EndDrawPass();

private:
    void DrawText();
    void DrawTexture();
    void LoadTexture();
    void ReleaseResource();
    void SetComputedRect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
};
