#include <stdafx.h>

#include "CPlayerPool.h"
#include "players/CRemotePlayer.h"

#include "game/entities/CObject.h"
#include "game/entities/CPed.h"

#include <new>

CPlayerPool::CPlayerPool() : m_localInfo{}, m_bNotEmpty{}, m_bPrevCollisionFlag{}, m_pObject{}, m_nLargestId(0)
{
    m_localInfo.m_nScore = 0;
    m_localInfo.m_nId = INVALID_ID;
    m_localInfo.__align = 0;
    m_localInfo.m_nPing = 0;
    m_localInfo.m_pObject = new CLocalPlayer();
}

CPlayerPool::~CPlayerPool()
{
    for (ID id = 0; id < MAX_PLAYERS; ++id)
        Delete(id);

    delete m_localInfo.m_pObject;
    m_localInfo.m_pObject = nullptr;
}

void CPlayerPool::UpdateLargestId()
{
    ZoneScoped;
    int largest = 0;
    for (int id = 0; id < MAX_PLAYERS; ++id)
    {
        if (m_bNotEmpty[id])
            largest = id;
    }
    m_nLargestId = largest;
}

void CPlayerPool::Process()
{
    ZoneScoped;
    for (int id = 0; id <= m_nLargestId && id < MAX_PLAYERS; ++id)
    {
        if (!m_bNotEmpty[id])
            continue;

        CPlayerInfo* info = m_pObject[id];
        if (info && info->m_pPlayer)
            info->m_pPlayer->Process();
    }

    if (m_localInfo.m_pObject)
        m_localInfo.m_pObject->Process();
}

CPlayerPool::ID CPlayerPool::Find(const void* gamePed)
{
    ZoneScoped;
    if (!gamePed)
        return INVALID_ID;

    if (m_localInfo.m_pObject)
    {
        CPed* localPed = m_localInfo.m_pObject->GetPed();
        if (localPed && localPed->GetNativePedHandle() == gamePed)
            return m_localInfo.m_nId;
    }

    for (int id = 0; id <= m_nLargestId && id < MAX_PLAYERS; ++id)
    {
        if (!m_bNotEmpty[id])
            continue;

        CRemotePlayer* player = GetPlayer(static_cast<ID>(id));
        if (player && player->m_pPed && player->m_pPed->GetNativePedHandle() == gamePed)
            return static_cast<ID>(id);
    }

    return INVALID_ID;
}

void CPlayerPool::Deactivate()
{
    ZoneScoped;
    if (m_localInfo.m_pObject)
    {
        m_localInfo.m_pObject->m_bIsActive = FALSE;
        m_localInfo.m_pObject->m_classSelection.m_nSelected = 0;
    }

    for (int id = 0; id <= m_nLargestId && id < MAX_PLAYERS; ++id)
    {
        CRemotePlayer* player = GetPlayer(static_cast<ID>(id));
        if (player)
            player->Remove();
    }
}

void CPlayerPool::ForceCollision()
{
    ZoneScoped;
    for (int id = 0; id <= m_nLargestId && id < MAX_PLAYERS; ++id)
    {
        m_bPrevCollisionFlag[id] = FALSE;

        CRemotePlayer* player = GetPlayer(static_cast<ID>(id));
        if (!player || !player->m_pPed || !player->m_pPed->DoesExist() || (player->m_pPed->GetFlags() & 0x100) != 0)
            continue;

        m_bPrevCollisionFlag[id] = player->m_pPed->GetCollisionFlag();
        player->m_pPed->SetCollisionFlag(TRUE);
    }
}

void CPlayerPool::RestoreCollision()
{
    ZoneScoped;
    for (int id = 0; id <= m_nLargestId && id < MAX_PLAYERS; ++id)
    {
        CRemotePlayer* player = GetPlayer(static_cast<ID>(id));
        if (!player || !player->m_pPed || !player->m_pPed->DoesExist() || (player->m_pPed->GetFlags() & 0x100) != 0)
            continue;

        player->m_pPed->SetCollisionFlag(m_bPrevCollisionFlag[id]);
    }
}

BOOL CPlayerPool::Delete(ID id, int reason)
{
    ZoneScoped;
    (void)reason;

    if (id >= MAX_PLAYERS || (!m_bNotEmpty[id] && !m_pObject[id]))
        return FALSE;

    if (m_localInfo.m_pObject && m_localInfo.m_pObject->m_bDoesSpectating && m_localInfo.m_pObject->m_spectating.m_nType == SPEC_TYPE_PLAYER &&
        m_localInfo.m_pObject->m_spectating.m_nObject == static_cast<int>(id))
    {
        m_localInfo.m_pObject->EnableSpectating(FALSE);
    }

    m_bNotEmpty[id] = FALSE;
    m_bPrevCollisionFlag[id] = FALSE;

    delete m_pObject[id];
    m_pObject[id] = nullptr;

    UpdateLargestId();
    return TRUE;
}

BOOL CPlayerPool::Create(ID id, const char* name, BOOL isNPC)
{
    ZoneScoped;
    if (id >= MAX_PLAYERS || id == m_localInfo.m_nId || !name)
        return FALSE;

    if (m_bNotEmpty[id] || m_pObject[id])
        Delete(id);

    CPlayerInfo* info = new (std::nothrow) CPlayerInfo(name, isNPC);
    if (!info || !info->m_pPlayer)
    {
        delete info;
        return FALSE;
    }

    info->m_pPlayer->m_nId = id;
    m_pObject[id] = info;
    m_bNotEmpty[id] = TRUE;
    m_bPrevCollisionFlag[id] = FALSE;

    UpdateLargestId();
    return TRUE;
}

CPlayerInfo* CPlayerPool::GetAt(ID id)
{
    if (id >= MAX_PLAYERS || !m_bNotEmpty[id])
        return nullptr;
    return m_pObject[id];
}

CRemotePlayer* CPlayerPool::GetPlayer(ID id)
{
    CPlayerInfo* info = GetAt(id);
    return info ? info->m_pPlayer : nullptr;
}

BOOL CPlayerPool::IsNPC(ID id)
{
    CPlayerInfo* info = GetAt(id);
    return info && info->m_bIsNPC ? TRUE : FALSE;
}

const char* CPlayerPool::GetLocalPlayerName()
{
    return m_localInfo.m_szName.c_str();
}

BOOL CPlayerPool::IsDisconnected(ID id)
{
    return IsConnected(id) ? FALSE : TRUE;
}

BOOL CPlayerPool::IsConnected(ID id)
{
    if (id == m_localInfo.m_nId && id != INVALID_ID)
        return TRUE;
    return id < MAX_PLAYERS && m_bNotEmpty[id] && m_pObject[id] ? TRUE : FALSE;
}

void CPlayerPool::SetLocalPlayerName(const char* name)
{
    m_localInfo.m_szName = name ? name : "";
}

void CPlayerPool::SetName(ID id, const char* name)
{
    ZoneScoped;
    if (!name)
        return;
    if (id == m_localInfo.m_nId && id != INVALID_ID)
    {
        SetLocalPlayerName(name);
        return;
    }
    if (CPlayerInfo* info = GetAt(id))
        info->m_szNick = name;
}

void CPlayerPool::SetScore(ID id, int value)
{
    ZoneScoped;
    if (id == m_localInfo.m_nId && id != INVALID_ID)
    {
        m_localInfo.m_nScore = value;
        return;
    }
    if (CPlayerInfo* info = GetAt(id))
        info->m_nScore = value;
}

void CPlayerPool::SetPing(ID id, int value)
{
    ZoneScoped;
    if (id == m_localInfo.m_nId && id != INVALID_ID)
    {
        m_localInfo.m_nPing = value;
        return;
    }
    if (CPlayerInfo* info = GetAt(id))
        info->m_nPing = value;
}

void CPlayerPool::SetTeam(ID id, std::uint8_t team)
{
    ZoneScoped;
    if (id == m_localInfo.m_nId && id != INVALID_ID)
    {
        if (m_localInfo.m_pObject)
            m_localInfo.m_pObject->m_nTeam = team;
        return;
    }
    if (CRemotePlayer* player = GetPlayer(id))
        player->m_nTeam = team;
}

void CPlayerPool::SetColor(ID id, D3DCOLOR color)
{
    ZoneScoped;
    if (id == m_localInfo.m_nId && id != INVALID_ID)
    {
        if (m_localInfo.m_pObject)
            m_localInfo.m_pObject->SetColor(color);
        return;
    }
    if (CRemotePlayer* player = GetPlayer(id))
        player->SetColor(color);
}

void CPlayerPool::SetNameTagVisible(ID id, BOOL visible)
{
    if (CRemotePlayer* player = GetPlayer(id))
        player->m_bDrawLabels = visible ? TRUE : FALSE;
}

void CPlayerPool::SetAt(ID id, CPlayerInfo* object)
{
    ZoneScoped;
    if (id >= MAX_PLAYERS || id == m_localInfo.m_nId)
        return;
    if (m_pObject[id] == object)
        return;
    for (int existing = 0; existing <= m_nLargestId; ++existing)
        if (object && m_pObject[existing] == object)
            return;
    Delete(id);

    m_pObject[id] = object;
    m_bNotEmpty[id] = object ? TRUE : FALSE;
    m_bPrevCollisionFlag[id] = FALSE;

    if (object && object->m_pPlayer)
        object->m_pPlayer->m_nId = id;

    UpdateLargestId();
}

int CPlayerPool::GetScore(ID id)
{
    if (id != INVALID_ID && id == m_localInfo.m_nId)
        return m_localInfo.m_nScore;
    if (id >= MAX_PLAYERS || !m_bNotEmpty[id] || !m_pObject[id])
        return 0;
    return m_pObject[id]->m_nScore;
}

int CPlayerPool::GetPing(ID id)
{
    if (id != INVALID_ID && id == m_localInfo.m_nId)
        return m_localInfo.m_nPing;
    if (id >= MAX_PLAYERS || !m_bNotEmpty[id] || !m_pObject[id])
        return 0;
    return m_pObject[id]->m_nPing;
}

const char* CPlayerPool::GetName(ID id)
{
    if (id != INVALID_ID && id == m_localInfo.m_nId)
        return m_localInfo.m_szName.c_str();
    if (id >= MAX_PLAYERS || !m_bNotEmpty[id] || !m_pObject[id])
        return nullptr;
    return m_pObject[id]->m_szNick.c_str();
}

int CPlayerPool::GetLocalPlayerPing()
{
    return m_localInfo.m_nPing;
}

int CPlayerPool::GetLocalPlayerScore()
{
    return m_localInfo.m_nScore;
}

int CPlayerPool::GetCount(BOOL includeNPC)
{
    ZoneScoped;
    int count = 0;
    for (int id = 0; id <= m_nLargestId && id < MAX_PLAYERS; ++id)
    {
        if (!m_bNotEmpty[id] || !m_pObject[id])
            continue;
        if (!includeNPC && m_pObject[id]->m_bIsNPC)
            continue;
        ++count;
    }
    return count;
}

CLocalPlayer* CPlayerPool::GetLocalPlayer()
{
    return m_localInfo.m_pObject;
}

CObject* CPlayerPool::FindAccessory(const void* gameObject)
{
    ZoneScoped;
    if (!gameObject)
        return nullptr;

    const auto findInPed = [gameObject](CPed* ped) -> CObject* {
        if (!ped)
            return nullptr;

        for (std::size_t slot = 0; slot < CPed::MAX_ACCESSORIES; ++slot)
        {
            CObject* object = ped->GetAccessory(static_cast<int>(slot));
            if (object && object->GetNativeHandle() == gameObject)
                return object;
        }
        return nullptr;
    };

    if (m_localInfo.m_pObject)
    {
        if (CObject* object = findInPed(m_localInfo.m_pObject->GetPed()))
            return object;
    }

    for (int id = 0; id <= m_nLargestId && id < MAX_PLAYERS; ++id)
    {
        CRemotePlayer* player = GetPlayer(static_cast<ID>(id));
        if (!player)
            continue;

        if (CObject* object = findInPed(player->m_pPed))
            return object;
    }

    return nullptr;
}
