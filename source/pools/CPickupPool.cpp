/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "main.h"

#include "CPickupPool.h"

#include "game/CGame.h"
#include "game/ModelInfo.h"
#include "net/CNetGame.h"
#include "game/Scripting.h"
#include <cmath>

CPickupPool::CPickupPool()
{
    ZoneScoped;
    // initialize inactive slots too; their metadata must never be indeterminate
    std::memset(m_object, 0, sizeof(m_object));
    std::memset(m_weapon, 0, sizeof(m_weapon));

    m_nCount = 0;
    for (int i = 0; i < MAX_PICKUPS; ++i)
    {
        m_handle[i] = 0;
        m_nTimer[i] = 0;
        m_nId[i] = -1;
    }
}

CPickupPool::~CPickupPool()
{
    ZoneScoped;
    for (int i = 0; i < MAX_PICKUPS; ++i)
    {
        if (m_handle[i])
        {
            // Опкод 0x0215 (remove_pickup): видаляє пікап зі світу гри за його дескриптором
            constexpr scm::OpcodeInfo opRemovePickup{0x0215, "i"};
            scm::ProcessCommand(&opRemovePickup, m_handle[i]);
        }
    }
}

void CPickupPool::Create(Pickup* data, int id)
{
    ZoneScoped;
    if (id < 0 || id >= MAX_PICKUPS || !data || !std::isfinite(data->m_position.x) || !std::isfinite(data->m_position.y) || !std::isfinite(data->m_position.z))
        return;
    auto* game = g_game.get();
    if (!game)
        return;
    const Pickup value = *data;
    Delete(id);
    Queue(id, value);
}

void CPickupPool::CreateWeapon(int model, CVector position, int ammo, ID exOwner)
{
    ZoneScoped;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        return;
    auto* game = g_game.get();
    if (!game)
        return;

    // Server pickup IDs (Create) occupy slots from the start, so dropped weapons
    // take the first free slot from the end to avoid being deleted by Create(id).
    int slot = INVALID_INDEX;
    for (int i = MAX_PICKUPS - 1; i >= 0; --i)
    {
        if (!m_handle[i] && !m_pending.contains(i))
        {
            slot = i;
            break;
        }
    }
    if (slot == INVALID_INDEX)
        return;

    Queue(slot, Pickup{model, 4, position}, WeaponPickup{true, exOwner}, ammo);
}

void CPickupPool::Delete(int id)
{
    ZoneScoped;
    m_pending.erase(id);
    if (m_nCount <= 0)
        return;

    if (id < 0 || id >= MAX_PICKUPS)
        return;

    if (!m_handle[id])
        return;

    // Опкод 0x0215 (remove_pickup): видаляє пікап зі світу гри за його дескриптором
    constexpr scm::OpcodeInfo opRemovePickup{0x0215, "i"};
    scm::ProcessCommand(&opRemovePickup, m_handle[id]);
    m_handle[id] = 0;
    m_nTimer[id] = 0;
    m_nId[id] = -1;
    m_weapon[id] = {};
    m_object[id] = {};
    --m_nCount;
}

void CPickupPool::DeleteWeapon(ID exOwner)
{
    std::erase_if(m_pending, [exOwner](const auto& entry) {
        return entry.second.weapon.m_bExists && entry.second.weapon.m_nExOwner == exOwner;
    });
    // One player can leave several dropped weapons, so do not stop at the first match.
    for (int i = 0; i < MAX_PICKUPS; ++i)
    {
        if (m_handle[i] && m_weapon[i].m_bExists && m_weapon[i].m_nExOwner == exOwner)
            Delete(i);
    }
}

int CPickupPool::GetIndex(int id) const
{
    ZoneScoped;
    if (id < 0)
        return INVALID_INDEX;
    for (int i = 0; i < MAX_PICKUPS; ++i)
    {
        if (m_handle[i] && m_nId[i] == id)
            return i;
    }

    return INVALID_INDEX;
}

void CPickupPool::SendNotification(int id)
{
    ZoneScoped;
    const int index = GetIndex(id);
    if (index == INVALID_INDEX)
        return;

    if (!m_handle[index])
        return;

    if (m_nTimer[index])
        return;

    // The normal notification path is only for ordinary pickups.
    if (m_weapon[index].m_bExists)
        return;
    if (auto* netGame = g_netGame.get())
        netGame->PickedUpPickup(static_cast<std::uint32_t>(index), CNetGame::PickupSendMode::Notification);

    m_nTimer[index] = 15;
}

void CPickupPool::Process()
{
    ZoneScoped;
    const auto now = GetTickCount();
    unsigned budget = 4;
    for (auto it = m_pending.begin(); g_game && it != m_pending.end() && budget;)
    {
        auto& pending = it->second;
        if (now - pending.queued >= 10000)
        {
#if defined(_DEBUG)
            Log("[pickup] model load timed out: model=%d", pending.data.m_nModel);
#endif // _DEBUG
            it = m_pending.erase(it);
            continue;
        }
        if (!ModelInfo::IsReady(pending.data.m_nModel))
        {
            if (now - pending.requested >= 1000)
            {
                g_game->RequestModel(pending.data.m_nModel);
                pending.requested = now;
            }
            ++it;
            continue;
        }
        --budget;
        const int id = it->first;
        const auto value = pending;
        GTAREF nativeIndex = 0;
        const auto handle = value.weapon.m_bExists ? g_game->CreateWeaponPickup(value.data.m_nModel, value.ammo, value.data.m_position)
                                                   : g_game->CreatePickup(value.data.m_nModel, value.data.m_nType, value.data.m_position, &nativeIndex);
        it = m_pending.erase(it);
        if (!handle)
            continue;
        m_object[id] = value.data;
        m_weapon[id] = value.weapon;
        m_handle[id] = handle;
        m_nId[id] = value.weapon.m_bExists ? -1 : static_cast<int>(nativeIndex);
        m_nTimer[id] = 0;
        ++m_nCount;
    }

    if (m_nCount <= 0)
        return;

    for (int i = 0; i < MAX_PICKUPS; ++i)
    {
        const GTAREF handle = m_handle[i];
        if (!handle)
            continue;

        // dropped weapons have no timer; the owner ID is sent instead of the slot,
        // because other clients may keep the pickup in a different slot
        if (m_weapon[i].m_bExists)
        {
            if (!CGame::IsPickupCollected(handle))
                continue;
            if (auto* netGame = g_netGame.get())
                netGame->DestroyWeaponPickup(m_weapon[i].m_nExOwner);
            continue;
        }

        // ordinary slots use the r5 timer
        if (m_object[i].m_nType != 14 && m_nTimer[i] != 0)
        {
            --m_nTimer[i];
            continue;
        }

        if (!CGame::IsPickupCollected(handle))
            continue;
        if (auto* netGame = g_netGame.get())
            netGame->PickedUpPickup(static_cast<std::uint32_t>(i), CNetGame::PickupSendMode::Process);
    }
}

void CPickupPool::Queue(int id, Pickup data, WeaponPickup weapon, int ammo)
{
    if (!g_game || data.m_nModel < 0 || data.m_nModel >= ModelInfo::MAX_MODELS)
        return;
    if (!ModelInfo::GetRelocated(data.m_nModel))
        data.m_nModel = 18631;
    const auto now = GetTickCount();
    m_pending.insert_or_assign(id, PendingPickup{data, weapon, ammo, now, now});
    if (!ModelInfo::IsReady(data.m_nModel))
        g_game->RequestModel(data.m_nModel);
}
