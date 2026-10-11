#pragma once

#include <memory>
#include <string>

struct IDirect3DDevice9;

extern std::string GetSampUserDirectory();

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

class CGame;
extern std::unique_ptr<CGame> g_game;

class CNetGame;
class CNetStats;
class CSrvNetStats;
extern std::unique_ptr<CNetGame> g_netGame;
extern std::unique_ptr<CNetStats> g_netStats;
extern std::unique_ptr<CSrvNetStats> g_srvNetStats;

class CConfig;
extern CConfig g_config;

class CSettings;
extern CSettings g_settings;

class CChat;
class CInput;
class CFonts;
class CRender;
class CDialog;
class CHelpDialog;
class CScoreboard;
class CDeathWindow;
class CDownloadWindow;
class CLicensePlate;
class CObjectMaterialText;
#if defined(LEGACY_SPAWN_SCREEN)
class CSpawnScreen;
#endif // LEGACY_SPAWN_SCREEN
class CObjectEdit;
class CObjectSelection;
class CLabel;
class CPlayerTags;
class CChatBubble;
class CTextDrawSelection;
extern std::unique_ptr<CChat> g_chat;
extern std::unique_ptr<CInput> g_input;
extern std::unique_ptr<CFonts> g_fonts;
extern std::unique_ptr<CRender> g_render;
extern std::unique_ptr<CDialog> g_dialog;
extern std::unique_ptr<CHelpDialog> g_helpDialog;
extern std::unique_ptr<CScoreboard> g_scoreboard;
extern std::unique_ptr<CDeathWindow> g_deathWindow;
extern std::unique_ptr<CDownloadWindow> g_downloadWindow;
extern std::unique_ptr<CLicensePlate> g_licensePlate;
extern std::unique_ptr<CObjectMaterialText> g_objectMaterialText;
#if defined(LEGACY_SPAWN_SCREEN)
extern std::unique_ptr<CSpawnScreen> g_spawnScreen;
#endif // LEGACY_SPAWN_SCREEN
extern std::unique_ptr<CObjectEdit> g_objectEdit;
extern std::unique_ptr<CObjectSelection> g_objectSelection;
extern std::unique_ptr<CLabel> g_label;
extern std::unique_ptr<CPlayerTags> g_playerTags;
extern std::unique_ptr<CChatBubble> g_chatBubble;
extern std::unique_ptr<CTextDrawSelection> g_textDrawSelection;

class CAudioStream;
extern CAudioStream* g_audioStream;
class CAudio;
extern CAudio* g_audio;
