/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>

class CSettings
{
public:
    enum
    {
        SETTINGS_STRING_LEN = 256
    };

    BOOL m_bDebugMode{};                             // -d: enable offline debug mode
    BOOL m_bOnlineGame{};                            // -c: enable online multiplayer mode
    char m_szPass[SETTINGS_STRING_LEN + 1]{};        // -z: server password
    char m_szHost[SETTINGS_STRING_LEN + 1]{};        // -h: server hostname or IP address
    char m_szPort[SETTINGS_STRING_LEN + 1]{};        // -p: server port
    char m_szNick[SETTINGS_STRING_LEN + 1]{};        // -n: player nickname
    char m_szDebugScript[SETTINGS_STRING_LEN + 1]{}; // -l: debug script path
    char m_szArtworkProxy[MAX_PATH + 1]{};           // artwork_proxy: proxy url for custom model downloads
    char m_szModelCache[MAX_PATH + 1]{};             // model_cache: custom model cache directory

    void Initialize();
    void Initialize(const char* arguments);
    static void GetFromCommandLine(const char* line, char* string);
    static void GetFromQuotes(const char* line, char* string);
};
