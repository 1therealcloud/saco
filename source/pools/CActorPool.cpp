#include <stdafx.h>

#include "CActorPool.h"

#include <cmath>
#include <memory>
#include <new>

CActorPool::CActorPool() = default;

CActorPool::~CActorPool()
{
    for (ID id = 0; id < MAX_ACTORS; ++id)
        if (DoesExist(id))
            Delete(id);
}

CActor* CActorPool::Get(ID id) const
{
    return DoesExist(id) ? m_pObject[id] : nullptr;
}

BOOL CActorPool::DoesExist(ID id) const
{
    return id < MAX_ACTORS && m_bNotEmpty[id] != FALSE;
}

void CActorPool::UpdateLargestId()
{
    m_nLargestId = 0;
    for (int id = MAX_ACTORS - 1; id >= 0; --id)
    {
        if (m_bNotEmpty[id])
        {
            m_nLargestId = id;
            break;
        }
    }
}

BOOL CActorPool::Delete(ID id)
{
    if (!DoesExist(id))
        return FALSE;

    auto* actor = m_pObject[id];
    m_bNotEmpty[id] = FALSE;
    m_pObject[id] = nullptr;
    m_pGameObject[id] = nullptr;
    pad_2ee4[id] = 0;
    pad_3e84[id] = 0;
    delete actor;
    UpdateLargestId();
    return TRUE;
}

CActorPool::ID CActorPool::Find(const void* gamePed) const
{
    if (!gamePed)
        return INVALID_ID;

    for (int id = 0; id <= m_nLargestId; ++id)
        if (m_bNotEmpty[id] && m_pGameObject[id] == gamePed)
            return static_cast<ID>(id);

    return INVALID_ID;
}

BOOL CActorPool::Create(const ActorInfo* info)
{
    if (!info || info->m_nId >= MAX_ACTORS || !std::isfinite(info->m_position.x) || !std::isfinite(info->m_position.y) || !std::isfinite(info->m_position.z) ||
        !std::isfinite(info->m_fRotation) || !std::isfinite(info->m_fHealth))
        return FALSE;

    const ID id = info->m_nId;
    if (DoesExist(id))
        Delete(id);

    auto actor = std::unique_ptr<CActor>(new (std::nothrow) CActor(info->m_nModel, info->m_position, info->m_fRotation));
    if (!actor || !actor->GetNativePedHandle())
        return FALSE;

    actor->SetHealth(info->m_fHealth);
    actor->SetInvulnerable(info->m_bInvulnerable);
    m_pGameObject[id] = actor->GetNativePedHandle();
    m_pObject[id] = actor.release();
    m_bNotEmpty[id] = TRUE;
    pad_2ee4[id] = 1;
    pad_3e84[id] = 0;
    UpdateLargestId();
    return TRUE;
}
