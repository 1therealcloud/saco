/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>

class FontFallback
{
public:
    explicit FontFallback(HDC primary) : m_primary(primary)
    {
        LOGFONTW description{};
        if (!GetObjectW(GetCurrentObject(primary, OBJ_FONT), sizeof(description), &description) || description.lfCharSet == SYMBOL_CHARSET)
            return;
        lstrcpyW(description.lfFaceName, L"Arial");
        description.lfCharSet = DEFAULT_CHARSET;
        m_font = CreateFontIndirectW(&description);
        if (m_font)
            m_dc = CreateCompatibleDC(primary);
        if (m_dc)
        {
            const auto previous = SelectObject(m_dc, m_font);
            if (!previous || previous == HGDI_ERROR)
            {
                DeleteDC(m_dc);
                m_dc = nullptr;
            }
        }
    }

    ~FontFallback()
    {
        if (m_dc)
            DeleteDC(m_dc);
        if (m_font)
            DeleteObject(m_font);
    }

    FontFallback(const FontFallback&) = delete;
    FontFallback& operator=(const FontFallback&) = delete;

    HDC GetDC(wchar_t character) const
    {
        if (!m_dc)
            return m_primary;
        WORD glyph = 0xFFFF;
        if (GetGlyphIndicesW(m_primary, &character, 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR || glyph == 0xFFFF)
            return m_dc;
        return m_primary;
    }

private:
    HDC m_primary = nullptr;
    HDC m_dc = nullptr;
    HFONT m_font = nullptr;
};
