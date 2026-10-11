#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>

class CFont;

class CDeathWindow
{
public:
    enum
    {
        MAX_DEATHMESSAGES = 5,
        SPECIAL_ENTRY_CONNECT = 200,
        SPECIAL_ENTRY_DISCONNECT = 201
    };

    struct Entry
    {
        char m_szKiller[25];
        char m_szVictim[25];
        D3DCOLOR m_killerColor;
        D3DCOLOR m_victimColor;
        char m_nWeapon;
    };

public:
    BOOL m_bEnabled;                  // 0x000
    Entry m_entry[MAX_DEATHMESSAGES]; // 0x004
    int m_nLongestNickWidth;          // 0x12B
    int m_position[2];                // 0x12F
    CFont* m_pFont;                   // 0x137
    CFont* m_pWeaponFont1;            // 0x13B
    CFont* m_pWeaponFont2;            // 0x13F
    void* m_pSprite;                  // 0x143, unused without D3DX
    IDirect3DDevice9* m_pDevice;      // 0x147
    BOOL m_bAuxFontInitialized;       // 0x14B
    CFont* m_pAuxFont1;               // 0x14F
    CFont* m_pAuxFont2;               // 0x153

public:
    explicit CDeathWindow(IDirect3DDevice9* device);
    ~CDeathWindow();

    CDeathWindow(const CDeathWindow&) = delete;
    CDeathWindow& operator=(const CDeathWindow&) = delete;

    void InitializeAuxFonts();
    void PushBack();

    void DrawText(const char* text, RECT rect, D3DCOLOR color, int format);
    void DrawWeaponSprite(const char* spriteId, RECT rect, D3DCOLOR color);
    void GetWeaponSpriteRectSize(void* point);
    const char* GetWeaponSpriteId(char weapon);

    void ResetFonts();
    void OnLostDevice();
    void OnResetDevice();
    void Draw();

    void AddEntry(const char* killer, const char* victim, D3DCOLOR killerColor, D3DCOLOR victimColor, char weapon);
    void AddMessage(const char* killer, const char* victim, D3DCOLOR killerColor, D3DCOLOR victimColor, char weapon);
};
