#include <stdafx.h>
#include "game/Profiler.h"

#include "main.h"

#include "CPickupPool.h"

#include "game/CGame.h"
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
    GTAREF nativeIndex = 0;
    const auto handle = game->CreatePickup(value.m_nModel, value.m_nType, value.m_position, &nativeIndex);
    if (!handle)
        return;
    m_object[id] = value;
    m_weapon[id] = {};
    m_handle[id] = handle;
    m_nId[id] = static_cast<int>(nativeIndex);
    m_nTimer[id] = 0;
    ++m_nCount;
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
        if (!m_handle[i])
        {
            slot = i;
            break;
        }
    }
    if (slot == INVALID_INDEX)
        return;

    const auto handle = game->CreateWeaponPickup(model, ammo, position);
    if (!handle)
        return;

    m_object[slot] = {model, 4, position};
    m_weapon[slot] = {true, exOwner};
    m_handle[slot] = handle;
    m_nId[slot] = -1;
    m_nTimer[slot] = 0;
    ++m_nCount;
}

void CPickupPool::Delete(int id)
{
    ZoneScoped;
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
    PROFILE_SCOPE(Pickups);
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
