#include <stdafx.h>

#include "CPlayerInfo.h"
#include "CRemotePlayer.h"

CPlayerInfo::CPlayerInfo(const char* name, BOOL isNPC) : pad_0(0), m_nScore(0), m_bIsNPC(isNPC), m_nPing(0), m_pPlayer(nullptr), __align(0), m_szNick(name ? name : "")
{
    m_pPlayer = new CRemotePlayer();
}

CPlayerInfo::~CPlayerInfo()
{
    delete m_pPlayer;
}
