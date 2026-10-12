/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

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
