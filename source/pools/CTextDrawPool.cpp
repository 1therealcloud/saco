#include <stdafx.h>

#include "CTextDrawPool.h"

#include <new>

namespace
{
    CTextDrawPool* g_activePool = nullptr;
    const std::uint32_t* g_drawBlockState = nullptr;
}

CTextDrawPool::CTextDrawPool()
{
    for (std::size_t i = 0; i < MAX_SLOTS; ++i)
    {
        m_pObject[i] = nullptr;
        m_bNotEmpty[i] = FALSE;
    }

    g_activePool = this;
}

CTextDrawPool::~CTextDrawPool()
{
    for (std::size_t i = 0; i < MAX_SLOTS; ++i)
    {
        CTextDraw* object = m_pObject[i];
        if (!object)
            continue;

        delete object;
        m_pObject[i] = nullptr;
        m_bNotEmpty[i] = FALSE;
    }

    if (g_activePool == this)
        g_activePool = nullptr;
}

void CTextDrawPool::Delete(std::uint16_t id)
{
    if (id >= MAX_SLOTS)
        return;
    CTextDraw* object = m_pObject[id];
    if (!object)
        return;

    delete object;

    m_pObject[id] = nullptr;
    m_bNotEmpty[id] = FALSE;
    const auto it = std::lower_bound(m_activeIds.begin(), m_activeIds.end(), id);
    if (it != m_activeIds.end() && *it == id)
        m_activeIds.erase(it);
}

void CTextDrawPool::Draw()
{
    for (const auto i : m_activeIds)
        if (m_pObject[i])
            m_pObject[i]->m_data.m_bDrawnThisFrame = 0;
    if (g_drawBlockState && *g_drawBlockState != 0)
        return;

    CTextDraw::BeginDrawPass();
    for (const auto i : m_activeIds)
    {
        if (m_bNotEmpty[i])
            m_pObject[i]->Draw();
    }
    CTextDraw::EndDrawPass();
}

CTextDraw* CTextDrawPool::Create(int id, const CTextDraw::Transmit* transmit, const char* text)
{
    if (id < 0 || id >= MAX_SLOTS || !transmit)
        return nullptr;
    const std::uint16_t index = static_cast<std::uint16_t>(id);

    if (m_pObject[index])
        Delete(index);

    auto* object = new CTextDraw(transmit, text);

    m_pObject[index] = object;
    m_bNotEmpty[index] = TRUE;
    m_activeIds.insert(std::lower_bound(m_activeIds.begin(), m_activeIds.end(), index), index);
    return object;
}

void CTextDrawPool::LoadPreviews()
{
    for (const auto i : m_activeIds)
    {
        if (m_bNotEmpty[i])
            m_pObject[i]->LoadPreview();
    }
}

void CTextDrawPool::UnloadPreviews()
{
    for (const auto i : m_activeIds)
    {
        if (m_bNotEmpty[i])
            m_pObject[i]->UnloadPreview();
    }
}

CTextDraw* CTextDrawPool::Get(std::uint16_t id) const
{
    if (id >= MAX_SLOTS || !m_bNotEmpty[id])
        return nullptr;

    return m_pObject[id];
}

CTextDrawPool* CTextDrawPool::GetActivePool()
{
    return g_activePool;
}

void CTextDrawPool::SetDrawBlockState(const std::uint32_t* state)
{
    g_drawBlockState = state;
}
