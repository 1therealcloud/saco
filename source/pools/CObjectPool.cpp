/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "main.h"

#include "CObjectPool.h"

#include "game/CCamera.h"
#include "game/CGame.h"

#include <cmath>

namespace
{
    std::uint32_t GetHighResolutionMilliseconds()
    {
        ZoneScoped;
        static LARGE_INTEGER frequency{};
        if (!frequency.QuadPart)
            QueryPerformanceFrequency(&frequency);

        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        if (!frequency.QuadPart)
            return GetTickCount();

        return static_cast<std::uint32_t>((counter.QuadPart / frequency.QuadPart) * 1000 + (counter.QuadPart % frequency.QuadPart) * 1000 / frequency.QuadPart);
    }

    std::uint32_t g_lastProcessTick = 0;
}

CObjectPool::CObjectPool() : m_nLargestId(0), m_bNotEmpty{}, m_pObject{}
{
    g_lastProcessTick = 0;
}

CObjectPool::~CObjectPool()
{
    // R5 walks every slot and lets Delete() decide whether it exists.
    for (ID id = 0; id < MAX_OBJECTS; ++id)
        Delete(id);
}

void CObjectPool::UpdateLargestId()
{
    ZoneScoped;
    // R5 keeps zero as the largest id for an empty pool.
    int largest = 0;
    for (int id = 0; id < MAX_OBJECTS; ++id)
        if (m_bNotEmpty[id])
            largest = id;
    m_nLargestId = largest;
}

int CObjectPool::GetCount()
{
    int count = 0;
    for (int id = 0; id < MAX_OBJECTS; ++id)
        if (m_bNotEmpty[id])
            ++count;
    return count;
}

BOOL CObjectPool::Delete(ID id)
{
    ZoneScoped;
    // The original R5 comparison also accepts id == 1000, which indexes one
    // element past both arrays. Keep the observable pool semantics without
    // reproducing that memory-corruption bug.
    if (id >= MAX_OBJECTS || !m_bNotEmpty[id] || !m_pObject[id])
        return FALSE;

    CObject* object = m_pObject[id];

    if (auto* game = g_game.get())
    {
        if (auto* camera = game->GetCamera(); camera && camera->IsAttachedTo(*object))
            camera->Attach(nullptr);
    }

    m_bNotEmpty[id] = FALSE;
    m_pObject[id] = nullptr;
    delete object;
    UpdateLargestId();
    return TRUE;
}

BOOL CObjectPool::Create(ID id, int model, CVector position, CVector rotation, float drawDistance)
{
    ZoneScoped;
    if (id >= MAX_OBJECTS || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
        !std::isfinite(rotation.z) || !std::isfinite(drawDistance) || drawDistance < 0)
        return FALSE;

    // R5 checks the pointer here rather than m_bNotEmpty.
    if (m_pObject[id])
        Delete(id);

    auto* game = g_game.get();
    if (!game)
        return FALSE;

    // In R5, negative model ids are resolved through ModelCache first; that
    // subsystem is still a stub in this tree. CObject already applies R5's
    // 18631 fallback when a model id cannot be resolved, so hourOn/hourOff stay
    // zero until CustomModels/ModelCache is implemented.
    CObject* object = game->CreateObject(model, position, rotation, drawDistance, 0, 0);
    if (!object || !object->IsValid())
    {
        delete object;
        return FALSE;
    }
    object->SetPoolManaged(true);
    m_pObject[id] = object;
    m_bNotEmpty[id] = TRUE;
    UpdateLargestId();
    return TRUE;
}

CObject* CObjectPool::Find(const void* gameObject)
{
    ZoneScoped;
    // include slot zero so attachments and selection work for every valid id
    if (!gameObject)
        return nullptr;
    for (int id = 0; id <= m_nLargestId && id < MAX_OBJECTS; ++id)
    {
        CObject* object = m_pObject[id];
        if (object && object->GetNativeHandle() == gameObject)
            return object;
    }
    return nullptr;
}

int CObjectPool::GetId(const void* gameObject)
{
    ZoneScoped;
    if (!gameObject)
        return INVALID_ID;
    for (int id = 0; id <= m_nLargestId && id < MAX_OBJECTS; ++id)
    {
        CObject* object = m_pObject[id];
        if (object && object->GetNativeHandle() == gameObject)
            return id;
    }
    return INVALID_ID;
}

void CObjectPool::Process()
{
    ZoneScoped;
    // R5 uses a millisecond high-resolution timer and converts the delta to
    // seconds before passing it to every active CObject.
    if (!g_lastProcessTick)
        g_lastProcessTick = GetHighResolutionMilliseconds();
    const std::uint32_t currentTick = GetHighResolutionMilliseconds();
    const float elapsedSeconds = static_cast<float>(currentTick - g_lastProcessTick) * 0.001f;

    Process(elapsedSeconds);
    g_lastProcessTick = currentTick;
}

void CObjectPool::Process(float elapsedSeconds)
{
    ZoneScoped;

    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0)
        return;
    for (int id = 0; id <= m_nLargestId && id < MAX_OBJECTS; ++id)
        if (m_bNotEmpty[id] && m_pObject[id])
            m_pObject[id]->Process(elapsedSeconds);
}

void CObjectPool::ConstructMaterials()
{
    for (int id = 0; id <= m_nLargestId && id < MAX_OBJECTS; ++id)
    {
        CObject* object = m_pObject[id];
        if (m_bNotEmpty[id] && object && object->m_bHasCustomMaterial)
            object->ConstructMaterialText();
    }
}

void CObjectPool::ShutdownMaterials()
{
    for (int id = 0; id <= m_nLargestId && id < MAX_OBJECTS; ++id)
    {
        CObject* object = m_pObject[id];
        if (m_bNotEmpty[id] && object && object->m_bHasCustomMaterial)
            object->ShutdownMaterialText();
    }
}

void CObjectPool::Draw()
{
    ZoneScoped;
    for (int id = 0; id <= m_nLargestId && id < MAX_OBJECTS; ++id)
    {
        CObject* object = m_pObject[id];
        if (m_bNotEmpty[id] && object && object->m_bReadyToRender && !object->m_bDrawLast)
            object->Draw();
    }
}

void CObjectPool::DrawLast()
{
    ZoneScoped;
    std::array<CObject*, MAX_OBJECTS> object{};
    int count = 0;

    for (int id = 0; id <= m_nLargestId && id < MAX_OBJECTS; ++id)
    {
        CObject* current = m_pObject[id];
        if (!m_bNotEmpty[id] || !current || !current->m_bReadyToRender || !current->m_bDrawLast)
            continue;

        current->m_fDistanceToCamera = current->GetDistanceToCamera();
        object[count] = current;
        ++count;
    }

    // retain r5 back-to-front ordering without the quadratic bubble sort
    std::stable_sort(object.begin(), object.begin() + count, [](CObject* a, CObject* b) {
        return a->m_fDistanceToCamera > b->m_fDistanceToCamera;
    });

    for (int i = 0; i < count; ++i)
        object[i]->Draw();
}

CObject* CObjectPool::Get(ID id)
{
    // R5 itself has an id==1000 off-by-one here; do not reproduce it.
    if (id >= MAX_OBJECTS || !m_bNotEmpty[id])
        return nullptr;
    return m_pObject[id];
}
