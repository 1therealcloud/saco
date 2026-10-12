#include <stdafx.h>

#include "CLabel.h"

#include "main.h"

#include "game/Scripting.h"
#include "game/CCamera.h"
#include "render/CFonts.h"
#include "render/CRender.h"

#include <cmath>
#include <optional>

namespace
{
    std::optional<CFont::Batch> g_batch;

    // 06BD: is_line_of_sight_clear
    // R5 passes: buildings=1, cars=0, chars=0, objects=1, particles=0.
}

CLabel::CLabel(IDirect3DDevice9* device) : m_pDevice(device), m_pSprite(nullptr)
{
    // R5 creates an ID3DXSprite here. saco is intentionally D3DX-free, so the
    // pointer-sized slot is retained for exact layout while batching is handled
    // by CFont::Batch in Begin()/End().
}

CLabel::~CLabel()
{
    g_batch.reset();
    m_pSprite = nullptr;
}

void CLabel::OnLostDevice()
{
    // The original calls ID3DXSprite::OnLostDevice(). CFonts/CRender own the
    // replacement D3D resources and are reset by the common device handlers.
    g_batch.reset();
}

void CLabel::OnResetDevice()
{
    // Original: ID3DXSprite::OnResetDevice(). Nothing is owned by this adapter.
}

bool CLabel::HasNoObstacles(CVector position)
{
    return CCamera::HasLineOfSight(position);
}

void CLabel::Begin()
{
    if (g_batch || !g_fonts || !g_fonts->GetFont())
        return;

    g_batch.emplace(g_fonts->GetFont());
}

void CLabel::End()
{
    g_batch.reset();
}

void CLabel::Draw(CVector* position, const char* text, D3DCOLOR color, BOOL shadow, bool testLineOfSight)
{
    if (!m_pDevice || !position || !text || !*text || !g_fonts || !g_render)
        return;

    if (testLineOfSight && !HasNoObstacles(*position))
        return;

    float screenX = 0.0f;
    float screenY = 0.0f;
    float depth = 0.0f;
    const CRender::Vector3 world{position->x, position->y, position->z};
    if (!g_render->WorldToScreen(world, screenX, screenY, &depth) || depth >= 1.0f)
        return;

    // R5 projects to integer pixels and renders into a one-pixel-wide centered
    // rectangle with DT_CENTER | DT_NOCLIP (0x101).
    const LONG x = static_cast<LONG>(std::lround(screenX));
    const LONG y = static_cast<LONG>(std::lround(screenY));
    RECT rect{x, y, x + 1, y + 1};

    auto* font = g_fonts->GetFont();
    if (!font)
        return;

    constexpr DWORD format = DT_CENTER | DT_NOCLIP;
    if (shadow)
        font->DrawTextOutlinedA(text, &rect, format, color, 0xFF000000u, 1);
    else
        font->DrawTextA(text, &rect, format, color);
}
