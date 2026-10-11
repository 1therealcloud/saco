#include <stdafx.h>

#include "main.h"

#include "CGangZonePool.h"
#include "game/CGame.h"

#include <new>
#include <cmath>

namespace
{
    std::uint8_t g_flashState = 0;
    DWORD g_lastFlashTick = 0;
}

CGangZonePool::CGangZonePool() : m_pObject{}, m_bNotEmpty{}
{}

CGangZonePool::~CGangZonePool()
{
    for (std::size_t i = 0; i < MAX_GANGZONES; ++i)
    {
        if (!m_pObject[i])
            continue;

        delete m_pObject[i];
        m_pObject[i] = nullptr;
    }
}

void CGangZonePool::Create(std::uint16_t id, float left, float top, float right, float bottom, std::uint32_t color)
{
    if (id >= MAX_GANGZONES || !std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom))
        return;
    if (m_pObject[id])
    {
        delete m_pObject[id];
        m_pObject[id] = nullptr;
        m_bNotEmpty[id] = FALSE;
    }

    auto* zone = new (std::nothrow) GangZone;
    if (!zone)
        return;

    zone->m_rect.left = left;
    zone->m_rect.bottom = bottom;
    zone->m_rect.right = right;
    zone->m_rect.top = top;
    zone->m_color = color;
    zone->m_altColor = color;

    m_pObject[id] = zone;
    m_bNotEmpty[id] = TRUE;
}

void CGangZonePool::StartFlashing(std::uint16_t id, std::uint32_t color)
{
    if (id >= MAX_GANGZONES)
        return;
    GangZone* zone = m_pObject[id];
    if (zone)
        zone->m_altColor = color;
}

void CGangZonePool::StopFlashing(std::uint16_t id)
{
    if (id >= MAX_GANGZONES)
        return;
    GangZone* zone = m_pObject[id];
    if (zone)
        zone->m_altColor = zone->m_color;
}

void CGangZonePool::Delete(std::uint16_t id)
{
    if (id >= MAX_GANGZONES)
        return;
    GangZone* zone = m_pObject[id];
    if (!zone)
        return;

    delete zone;
    m_pObject[id] = nullptr;
    m_bNotEmpty[id] = FALSE;
}

void CGangZonePool::Draw()
{
    auto* game = g_game.get();
    if (!game)
        return;
    const DWORD tick = GetTickCount();
    if (tick - g_lastFlashTick >= 500)
    {
        g_flashState = static_cast<std::uint8_t>(~g_flashState);
        g_lastFlashTick = tick;
    }

    for (std::size_t i = 0; i < MAX_GANGZONES; ++i)
    {
        if (!m_bNotEmpty[i])
            continue;

        GangZone* zone = m_pObject[i];
        if (!zone)
            continue;
        const std::uint32_t color = g_flashState ? zone->m_altColor : zone->m_color;
        game->DrawGangZone(&zone->m_rect.left, color);
    }
}
