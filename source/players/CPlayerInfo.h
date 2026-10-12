/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>
#include <string>

class CRemotePlayer;

class CPlayerInfo
{
public:
    int pad_0;
    int m_nScore;
    BOOL m_bIsNPC;
    int m_nPing;
    CRemotePlayer* m_pPlayer;
    int __align; // r5 string alignment slot
    std::string m_szNick;

    CPlayerInfo(const char* name, BOOL isNPC);
    ~CPlayerInfo();

    CPlayerInfo(const CPlayerInfo&) = delete;
    CPlayerInfo& operator=(const CPlayerInfo&) = delete;
};
