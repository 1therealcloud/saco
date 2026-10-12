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
#include <vector>

#include "gui/CTextDraw.h"

class CTextDrawPool
{
public:
    static constexpr std::size_t MAX_TEXTDRAWS = 2048;
    static constexpr std::size_t MAX_LOCAL_TEXTDRAWS = 256;
    static constexpr std::size_t MAX_SLOTS = MAX_TEXTDRAWS + MAX_LOCAL_TEXTDRAWS;

public:
    BOOL m_bNotEmpty[MAX_SLOTS];     // 0x0000
    CTextDraw* m_pObject[MAX_SLOTS]; // 0x2400

public:
    CTextDrawPool();
    ~CTextDrawPool();

    CTextDrawPool(const CTextDrawPool&) = delete;
    CTextDrawPool& operator=(const CTextDrawPool&) = delete;

    void Delete(std::uint16_t id);
    void Draw();
    CTextDraw* Create(int id, const CTextDraw::Transmit* transmit, const char* text);

    void LoadPreviews();
    void UnloadPreviews();

    CTextDraw* Get(std::uint16_t id) const;

    static CTextDrawPool* GetActivePool();

    // R5 skips the whole draw pass when this external state exists and its
    // first DWORD is non-zero. Pass nullptr when that subsystem is absent.
    static void SetDrawBlockState(const std::uint32_t* state);
    const std::vector<std::uint16_t>& GetActiveIds() const
    {
        return m_activeIds;
    }

private:
    std::vector<std::uint16_t> m_activeIds;
};
