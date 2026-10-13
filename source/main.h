/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <memory>
#include <string>

struct IDirect3DDevice9;

extern void Frame(IDirect3DDevice9* device); // used by d3d9hook.cpp
extern void Process();

namespace exceptions
{
    extern void install();
}

namespace saa
{
    class Archive;
}
extern saa::Archive g_archive;

// client version
extern bool g_DLClient;

// game
class CGame;
extern std::unique_ptr<CGame> g_game;



// audio
class CAudio;
extern CAudio* g_audio;
class CAudioStream;
extern CAudioStream* g_audioStream;



// config
class CConfig;
extern CConfig g_config;

class CSettings;
extern CSettings g_settings;

// samp user directory
extern std::string g_sampUserDirectory;


// render
class CRender;
extern std::unique_ptr<CRender> g_render;

class CLicensePlate;
extern std::unique_ptr<CLicensePlate> g_licensePlate;

class CObjectMaterialText;
extern std::unique_ptr<CObjectMaterialText> g_objectMaterialText;



// network
class CNetGame;
extern std::unique_ptr<CNetGame> g_netGame;
class CNetStats;
extern std::unique_ptr<CNetStats> g_netStats;
class CSrvNetStats;
extern std::unique_ptr<CSrvNetStats> g_srvNetStats;



// GUI
class CChat;
extern std::unique_ptr<CChat> g_chat;
class CInput;
extern std::unique_ptr<CInput> g_input;
class CFonts;
extern std::unique_ptr<CFonts> g_fonts;
class CDialog;
extern std::unique_ptr<CDialog> g_dialog;
class CHelpDialog;
extern std::unique_ptr<CHelpDialog> g_helpDialog;
class CScoreboard;
extern std::unique_ptr<CScoreboard> g_scoreboard;
class CDeathWindow;
extern std::unique_ptr<CDeathWindow> g_deathWindow;
class CDownloadWindow;
extern std::unique_ptr<CDownloadWindow> g_downloadWindow;
#if defined(LEGACY_SPAWN_SCREEN)
class CSpawnScreen;
extern std::unique_ptr<CSpawnScreen> g_spawnScreen;
#endif // LEGACY_SPAWN_SCREEN
class CObjectEdit;
extern std::unique_ptr<CObjectEdit> g_objectEdit;
class CObjectSelection;
extern std::unique_ptr<CObjectSelection> g_objectSelection;
class CLabel;
extern std::unique_ptr<CLabel> g_label;
class CPlayerTags;
extern std::unique_ptr<CPlayerTags> g_playerTags;
class CChatBubble;
extern std::unique_ptr<CChatBubble> g_chatBubble;
class CTextDrawSelection;
extern std::unique_ptr<CTextDrawSelection> g_textDrawSelection;
