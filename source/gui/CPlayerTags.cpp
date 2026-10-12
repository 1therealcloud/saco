/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "CPlayerTags.h"

#include "main.h"

#include "gui/CDeathWindow.h"
#include "render/CFonts.h"
#include "render/CRender.h"

#include <cmath>
#include <optional>

namespace
{
    std::optional<CFont::Batch> g_labelBatch;

    constexpr float kTagDistanceScale = 0.0475f;
    constexpr float kTagBaseHeight = 0.2f;
    constexpr float kHealthScale = 0.384615391f;

    constexpr D3DCOLOR kBorderColor = 0xFF000000u;
    constexpr D3DCOLOR kHealthBackground = 0xFF4B0B14u;
    constexpr D3DCOLOR kHealthColor = 0xFFB92228u;
    constexpr D3DCOLOR kArmourBackground = 0xFF282828u;
    constexpr D3DCOLOR kArmourColor = 0xFFC8C8C8u;

    bool ProjectTagPoint(IDirect3DDevice9* device, CRender* render, const CVector& position, float distanceToCamera, LONG& x, LONG& y)
    {
        ZoneScoped;
        if (!device || !render)
            return false;

        CVector world = position;
        world.z += distanceToCamera * kTagDistanceScale + kTagBaseHeight;

        float screenX = 0.0f;
        float screenY = 0.0f;
        float depth = 0.0f;
        if (!render->WorldToScreen({world.x, world.y, world.z}, screenX, screenY, &depth) || depth >= 1.0f)
            return false;

        // R5 passes the projected coordinates through __ftol2_sse before drawing.
        x = static_cast<LONG>(screenX);
        y = static_cast<LONG>(screenY);
        return true;
    }

    template <class Renderer>
    void DrawBar(Renderer* render, float x, float y, float value, D3DCOLOR background, D3DCOLOR fill)
    {
        ZoneScoped;
        if (!render || !std::isfinite(value))
            return;

        const float clamped = std::clamp(value, 0.0f, 100.0f);

        // original r5 strip dimensions
        render->DrawBox(x - 20.0f, y, 41.0f, 6.0f, kBorderColor);
        render->DrawBox(x - 19.0f, y + 1.0f, 38.0f, 4.0f, background);
        render->DrawBox(x - 19.0f, y + 1.0f, clamped * kHealthScale, 4.0f, fill);
    }
}

CPlayerTags::CPlayerTags(IDirect3DDevice9* device) : m_pDevice(device), m_pStates(nullptr), m_pSprite(nullptr)
{
    // Original R5 creates an ID3DXSprite here. saco is D3DX-free; the pointer
    // slot remains in place so the instance layout is still exact.
}

CPlayerTags::~CPlayerTags()
{
    ZoneScoped;
    g_labelBatch.reset();

    if (m_pStates)
    {
        m_pStates->Release();
        m_pStates = nullptr;
    }

    m_pSprite = nullptr;
}

void CPlayerTags::EndHealthBar()
{
    if (g_render)
        g_render->EndRender();
}

void CPlayerTags::BeginLabel()
{
    if (g_labelBatch || !g_fonts || !g_fonts->GetLittleFont())
        return;

    g_labelBatch.emplace(g_fonts->GetLittleFont());
}

void CPlayerTags::EndLabel()
{
    g_labelBatch.reset();
}

void CPlayerTags::DrawTag(CVector* position, const char* text, D3DCOLOR color, float health, float armour, float distanceToCamera, bool drawStatus, int status)
{
    ZoneScoped;
    LONG x = 0, y = 0;
    if (!position || !text || !*text || !g_fonts || !g_render || !ProjectTagPoint(m_pDevice, g_render.get(), *position, distanceToCamera, x, y))
        return;
    const bool ownedBatch = !g_labelBatch;
    const bool statusIcon = drawStatus && status == 2 && g_deathWindow;
    // Auxiliary icons use a different atlas; finish previous tags before that ordered boundary.
    if (statusIcon)
        EndLabel();
    BeginLabel();
    DrawLabelAt(x, y, text, color, drawStatus, status);
    if (statusIcon)
    {
        EndLabel();
        BeginLabel();
    }
    if (health > 0)
        DrawHealthBarAt(x, y, health, armour, true);
    if (ownedBatch)
        EndLabel();
}

void CPlayerTags::DrawLabel(CVector* position, const char* text, D3DCOLOR color, float distanceToCamera, bool drawStatus, int status)
{
    ZoneScoped;
    if (!position || !text || !*text || !g_fonts || !g_render)
        return;

    LONG x = 0;
    LONG y = 0;
    if (!ProjectTagPoint(m_pDevice, g_render.get(), *position, distanceToCamera, x, y))
        return;

    DrawLabelAt(x, y, text, color, drawStatus, status);
}

void CPlayerTags::DrawLabelAt(LONG x, LONG y, const char* text, D3DCOLOR color, bool drawStatus, int status)
{
    ZoneScoped;
    SIZE size{};
    g_fonts->GetLittleTextScreenSize(&size, text, 0);

    RECT rect{};
    rect.left = x - size.cx / 2;
    rect.top = y + 17 - size.cy;
    rect.right = x + size.cx / 2;
    rect.bottom = y;

    // Original: format 0x100 (DT_NOCLIP), always with the SA-MP text shadow.
    g_fonts->DrawLittleText(text, rect, DT_NOCLIP, color, true);

    if (!drawStatus || status != 2 || !g_deathWindow)
        return;

    if (!g_deathWindow->m_bAuxFontInitialized)
        g_deathWindow->InitializeAuxFonts();

    CFont* aux1 = g_deathWindow->m_pAuxFont1;
    CFont* aux2 = g_deathWindow->m_pAuxFont2;
    if (!aux1 || !aux2)
        return;

    // R5 measures the "C" glyph in SAMPAUX3/22 with 0x505, then overlays
    // "C" and "E" to form the timeout/status marker next to the nickname.
    const SIZE glyph = aux2->GetTextSize("C", DT_CENTER | DT_VCENTER | DT_NOCLIP);
    const LONG left = x - 22 - glyph.cx / 2;
    const LONG top = y + 17 + glyph.cy / 2;

    RECT iconRect{left, top, left + 1, top + 2};
    constexpr DWORD format = DT_CENTER | DT_VCENTER | DT_NOCLIP;
    aux2->DrawTextA("C", 1, &iconRect, format, 0xFF000000u);

    --iconRect.left;
    iconRect.top -= 2;
    aux1->DrawTextA("E", 1, &iconRect, format, 0xFFE3E1E3u);
}

void CPlayerTags::DrawHealthBar(CVector* position, float health, float armour, float distanceToCamera)
{
    ZoneScoped;
    if (!position || !g_render || health <= 0.0f)
        return;

    LONG x = 0;
    LONG y = 0;
    if (!ProjectTagPoint(m_pDevice, g_render.get(), *position, distanceToCamera, x, y))
        return;

    DrawHealthBarAt(x, y, health, armour);
}

void CPlayerTags::DrawHealthBarAt(LONG x, LONG y, float health, float armour, bool fontBars)
{
    ZoneScoped;
    const bool hasArmour = armour > 0.0f;
    const float baseY = static_cast<float>(y + 18);
    const float healthY = baseY + (hasArmour ? 8.0f : 0.0f);

    // R5 draws health first. When armour is present, health is moved down by
    // eight pixels and the armour strip occupies the original health position.
    if (fontBars)
        DrawBar(g_fonts->GetLittleFont(), static_cast<float>(x), healthY, health, kHealthBackground, kHealthColor);
    else
        DrawBar(g_render.get(), static_cast<float>(x), healthY, health, kHealthBackground, kHealthColor);

    if (hasArmour)
    {
        if (fontBars)
            DrawBar(g_fonts->GetLittleFont(), static_cast<float>(x), baseY, armour, kArmourBackground, kArmourColor);
        else
            DrawBar(g_render.get(), static_cast<float>(x), baseY, armour, kArmourBackground, kArmourColor);
    }
}

void CPlayerTags::OnLostDevice()
{
    ZoneScoped;
    g_labelBatch.reset();

    if (m_pStates)
    {
        m_pStates->Release();
        m_pStates = nullptr;
    }

    // Original also calls ID3DXSprite::OnLostDevice(). No D3DX object is owned
    // by this implementation.
}

void CPlayerTags::OnResetDevice()
{
    // CRender restores the healthbar state; CFonts owns the text resources.
}

void CPlayerTags::BeginHealthBar()
{
    if (!m_pDevice || !g_render)
        return;

    g_render->BeginRender();
}
