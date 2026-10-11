#include <stdafx.h>

#include "CPlayerTags.h"

#include "main.h"

#include "gui/CDeathWindow.h"
#include "render/CFonts.h"
#include "render/CRender.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
    std::unique_ptr<CFont::Batch> g_labelBatch;

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

    void DrawBar(CRender* render, float x, float y, float value, D3DCOLOR background, D3DCOLOR fill)
    {
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

    if (m_pStates)
        m_pStates->Apply();
}

void CPlayerTags::BeginLabel()
{
    if (g_labelBatch || !g_fonts || !g_fonts->GetLittleFont())
        return;

    g_labelBatch = std::make_unique<CFont::Batch>(g_fonts->GetLittleFont());
}

void CPlayerTags::EndLabel()
{
    g_labelBatch.reset();
}

void CPlayerTags::DrawLabel(CVector* position, const char* text, D3DCOLOR color, float distanceToCamera, bool drawStatus, int status)
{
    if (!position || !text || !*text || !g_fonts || !g_render)
        return;

    LONG x = 0;
    LONG y = 0;
    if (!ProjectTagPoint(m_pDevice, g_render.get(), *position, distanceToCamera, x, y))
        return;

    SIZE size{};
    g_fonts->GetLittleTextScreenSize(&size, text, 0);

    RECT rect{};
    rect.left = x - size.cx / 2;
    rect.top = y + 17 - size.cy;
    rect.right = x + size.cx / 2;
    rect.bottom = y;

    // Original: format 0x100 (DT_NOCLIP), always with the SA-MP text shadow.
    g_fonts->DrawLittleText(text, rect, DT_NOCLIP, color, true);

    if (!drawStatus || !g_deathWindow)
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

    if (status != 2)
        return;

    RECT iconRect{left, top, left + 1, top + 2};
    constexpr DWORD format = DT_CENTER | DT_VCENTER | DT_NOCLIP;
    aux2->DrawTextA("C", 1, &iconRect, format, 0xFF000000u);

    --iconRect.left;
    iconRect.top -= 2;
    aux1->DrawTextA("E", 1, &iconRect, format, 0xFFE3E1E3u);
}

void CPlayerTags::DrawHealthBar(CVector* position, float health, float armour, float distanceToCamera)
{
    if (!position || !g_render || health <= 0.0f)
        return;

    LONG x = 0;
    LONG y = 0;
    if (!ProjectTagPoint(m_pDevice, g_render.get(), *position, distanceToCamera, x, y))
        return;

    const bool hasArmour = armour > 0.0f;
    const float baseY = static_cast<float>(y + 18);
    const float healthY = baseY + (hasArmour ? 8.0f : 0.0f);

    // R5 draws health first. When armour is present, health is moved down by
    // eight pixels and the armour strip occupies the original health position.
    DrawBar(g_render.get(), static_cast<float>(x), healthY, health, kHealthBackground, kHealthColor);

    if (hasArmour)
        DrawBar(g_render.get(), static_cast<float>(x), baseY, armour, kArmourBackground, kArmourColor);
}

void CPlayerTags::OnLostDevice()
{
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
    // Original calls ID3DXSprite::OnResetDevice(), then recreates the all-state
    // state block when it was released by OnLostDevice().
    if (!m_pStates && m_pDevice)
        m_pDevice->CreateStateBlock(D3DSBT_ALL, &m_pStates);
}

void CPlayerTags::BeginHealthBar()
{
    if (!m_pDevice || !g_render)
        return;

    if (!m_pStates)
        m_pDevice->CreateStateBlock(D3DSBT_ALL, &m_pStates);

    if (m_pStates)
        m_pStates->Capture();

    g_render->BeginRender();
}
