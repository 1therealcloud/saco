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
