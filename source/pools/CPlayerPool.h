/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "players/CLocalPlayer.h"
#include "players/CPlayerInfo.h"

class CObject;

class CPlayerPool
{
public:
    using ID = std::uint16_t;

    enum
    {
        MAX_PLAYERS = 1004
    };

    static constexpr ID INVALID_ID = 0xFFFF;

    struct LocalInfo
    {
        int m_nScore;            // 0x00
        ID m_nId;                // 0x04
        int __align;             // 0x06, R5 std::string ABI slot
        std::string m_szName;    // 0x0A
        int m_nPing;             // 0x22
        CLocalPlayer* m_pObject; // 0x26
    };

    LocalInfo m_localInfo;                  // 0x0000
    BOOL m_bNotEmpty[MAX_PLAYERS];          // 0x002A
    BOOL m_bPrevCollisionFlag[MAX_PLAYERS]; // 0x0FDA
    CPlayerInfo* m_pObject[MAX_PLAYERS];    // 0x1F8A
    int m_nLargestId;                       // 0x2F3A

    CPlayerPool();
    ~CPlayerPool();

    CPlayerPool(const CPlayerPool&) = delete;
    CPlayerPool& operator=(const CPlayerPool&) = delete;

    void UpdateLargestId();
    void Process();

    // R5 takes GTA's ::CPed*. This tree already uses the global CPed name for
    // the SA-MP wrapper, so the native GTA pointer is represented as void*.
    ID Find(const void* gamePed);

    void Deactivate();
    void ForceCollision();
    void RestoreCollision();

    BOOL Delete(ID id, int reason = 0);
    BOOL Create(ID id, const char* name, BOOL isNPC);

    CPlayerInfo* GetAt(ID id);
    CRemotePlayer* GetPlayer(ID id);
    BOOL IsNPC(ID id);
    const char* GetLocalPlayerName();
    BOOL IsDisconnected(ID id);
    BOOL IsConnected(ID id);
    void SetLocalPlayerName(const char* name);
    void SetName(ID id, const char* name);
    void SetScore(ID id, int value);
    void SetPing(ID id, int value);
    void SetTeam(ID id, std::uint8_t team);
    void SetColor(ID id, D3DCOLOR color);
    void SetNameTagVisible(ID id, BOOL visible);
    // takes ownership of object; replaces and destroys the previous entry
    void SetAt(ID id, CPlayerInfo* object);
    int GetScore(ID id);
    int GetPing(ID id);
    const char* GetName(ID id);
    int GetLocalPlayerPing();
    int GetLocalPlayerScore();
    int GetCount(BOOL includeNPC = FALSE);
    CLocalPlayer* GetLocalPlayer();

    // Same adaptation as Find(): R5's argument is GTA ::CObject* while this
    // project uses CObject for the SA-MP wrapper.
    CObject* FindAccessory(const void* gameObject);
};
