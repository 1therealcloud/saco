#include <stdafx.h>
#include "game/Profiler.h"

#include "CDeathWindow.h"

#include "main.h"

#include "render/CFonts.h"

namespace
{
    constexpr const char* kLongestNick = "LONGESTNICKNICK_NICKNICK";
    constexpr const char* kWeaponFontFace = "GTAWEAPON3";
    constexpr const char* kAuxFontFace = "SAMPAUX3";

    constexpr D3DCOLOR kOutlineColor = 0xFF000000u;
    constexpr D3DCOLOR kDefaultSpriteColor = 0xFFFFFFFFu;
    constexpr D3DCOLOR kConnectSpriteColor = 0xFF1111AAu;
    constexpr D3DCOLOR kDisconnectSpriteColor = 0xFFAA1111u;

    unsigned g_fontUsers = 0;
    bool g_weaponFontLoaded = false;
    bool g_auxFontLoaded = false;

    bool GetScreenSize(IDirect3DDevice9* device, int& width, int& height)
    {
        width = 0;
        height = 0;

        if (!device)
            return false;

        D3DVIEWPORT9 viewport{};
        if (FAILED(device->GetViewport(&viewport)))
            return false;

        width = static_cast<int>(viewport.Width);
        height = static_cast<int>(viewport.Height);
        return width > 0 && height > 0;
    }

    void CopyName(char (&destination)[25], const char* source)
    {
        destination[0] = '\0';

        if (!source)
            return;

        std::strncpy(destination, source, sizeof(destination) - 1);
        destination[sizeof(destination) - 1] = '\0';
    }

    int MeasureText(CFont* font, const char* text)
    {
        if (!font || !text || !*text)
            return 0;

        return font->GetTextSize(text, DT_CALCRECT | DT_LEFT).cx;
    }

    template <typename T>
    void DeleteObject(T*& object)
    {
        delete object;
        object = nullptr;
    }
}

CDeathWindow::CDeathWindow(IDirect3DDevice9* device)
    : m_bEnabled(TRUE), m_entry{}, m_nLongestNickWidth(0), m_position{0, 0}, m_pFont(nullptr), m_pWeaponFont1(nullptr), m_pWeaponFont2(nullptr), m_pSprite(nullptr),
      m_pDevice(device), m_bAuxFontInitialized(FALSE), m_pAuxFont1(nullptr), m_pAuxFont2(nullptr)
{
    if (g_fontUsers++ == 0)
    {
        g_weaponFontLoaded = AddFontResourceExA("gtaweap3.ttf", FR_PRIVATE, nullptr) != 0;
        g_auxFontLoaded = AddFontResourceExA("sampaux3.ttf", FR_PRIVATE, nullptr) != 0;
        if (!g_weaponFontLoaded)
            Log("[deathwindow] failed to load gtaweap3.ttf from the game directory");
        if (!g_auxFontLoaded)
            Log("[deathwindow] failed to load sampaux3.ttf from the game directory");
    }
    ResetFonts();
    std::memset(m_entry, 0, sizeof(m_entry));
}

CDeathWindow::~CDeathWindow()
{
    DeleteObject(m_pFont);
    DeleteObject(m_pWeaponFont1);
    DeleteObject(m_pWeaponFont2);
    DeleteObject(m_pAuxFont1);
    DeleteObject(m_pAuxFont2);

    m_pSprite = nullptr;
    if (--g_fontUsers == 0)
    {
        if (g_weaponFontLoaded)
            RemoveFontResourceExA("gtaweap3.ttf", FR_PRIVATE, nullptr);
        if (g_auxFontLoaded)
            RemoveFontResourceExA("sampaux3.ttf", FR_PRIVATE, nullptr);
        g_weaponFontLoaded = g_auxFontLoaded = false;
    }
}

void CDeathWindow::InitializeAuxFonts()
{
    if (!m_pDevice || m_bAuxFontInitialized || !g_auxFontLoaded)
        return;

    m_pAuxFont1 = new CFont(m_pDevice, 20, 0, FW_NORMAL, kAuxFontFace, CP_ACP, SYMBOL_CHARSET);
    m_pAuxFont2 = new CFont(m_pDevice, 22, 0, FW_NORMAL, kAuxFontFace, CP_ACP, SYMBOL_CHARSET);

    if (!m_pAuxFont1->IsValid() || !m_pAuxFont2->IsValid())
    {
        DeleteObject(m_pAuxFont1);
        DeleteObject(m_pAuxFont2);
        return;
    }

    m_bAuxFontInitialized = TRUE;
}

void CDeathWindow::PushBack()
{
    std::memmove(&m_entry[0], &m_entry[1], sizeof(Entry) * (MAX_DEATHMESSAGES - 1));
}

void CDeathWindow::DrawText(const char* text, RECT rect, D3DCOLOR color, int format)
{
    if (!m_pFont || !text || !*text)
        return;

    m_pFont->DrawTextOutlinedA(text, &rect, static_cast<DWORD>(format) | DT_NOCLIP, color, kOutlineColor, 1);
}

void CDeathWindow::DrawWeaponSprite(const char* spriteId, RECT rect, D3DCOLOR color)
{
    if (!m_pWeaponFont1 || !m_pWeaponFont2 || !spriteId || !*spriteId)
        return;

    rect.top -= 5;

    m_pWeaponFont2->DrawTextA("G", &rect, DT_NOCLIP, kOutlineColor);

    RECT spriteRect = rect;
    m_pWeaponFont2->DrawTextA("G", &spriteRect, DT_CALCRECT | DT_NOCLIP, kOutlineColor);

    m_pWeaponFont1->DrawTextA(spriteId, &spriteRect, DT_NOCLIP | DT_CENTER | DT_VCENTER, color);
}

void CDeathWindow::GetWeaponSpriteRectSize(void* point)
{
    if (!point)
        return;

    RECT rect{0, 0, 100, 100};

    if (m_pWeaponFont2)
        m_pWeaponFont2->DrawTextA("G", &rect, DT_CALCRECT | DT_NOCLIP | DT_VCENTER, kOutlineColor);

    POINT* size = static_cast<POINT*>(point);
    size->x = rect.right - rect.left;
    size->y = rect.bottom - rect.top;
}

const char* CDeathWindow::GetWeaponSpriteId(char weapon)
{
    switch (static_cast<unsigned char>(weapon))
    {
        case 0:
            return "%";
        case 1:
            return "B";
        case 2:
            return ">";
        case 3:
            return "(";
        case 4:
            return "C";
        case 5:
            return "?";
        case 6:
            return "&";
        case 7:
            return "\"";
        case 8:
            return "!";
        case 9:
            return "1";

        case 10:
        case 11:
        case 12:
        case 13:
            return "E";

        case 14:
            return "$";
        case 15:
            return "#";
        case 16:
            return "@";
        case 17:
            return "D";

        case 22:
            return "6";
        case 23:
            return "2";
        case 24:
            return "3";
        case 25:
            return "=";
        case 26:
            return "0";
        case 27:
            return "+";
        case 28:
            return "I";
        case 29:
            return "8";
        case 30:
            return "H";
        case 31:
            return "5";
        case 32:
            return "7";
        case 33:
            return ".";
        case 34:
            return "A";
        case 35:
            return "4";
        case 36:
            return ")";
        case 37:
            return "P";
        case 38:
            return "F";
        case 39:
            return "<";
        case 40:
            return ";";
        case 41:
            return "/";
        case 42:
            return ",";
        case 46:
            return ":";
        case 49:
            return "L";
        case 50:
            return "R";
        case 51:
            return "Q";
        case 54:
            return "K";

        case SPECIAL_ENTRY_CONNECT:
        case SPECIAL_ENTRY_DISCONNECT:
            return "N";

        default:
            return "J";
    }
}

void CDeathWindow::ResetFonts()
{
    if (!m_pDevice || !g_weaponFontLoaded)
        return;

    DeleteObject(m_pFont);
    DeleteObject(m_pWeaponFont1);
    DeleteObject(m_pWeaponFont2);

    int screenWidth = 0;
    int screenHeight = 0;

    if (!GetScreenSize(m_pDevice, screenWidth, screenHeight))
        return;

    int sizeOffset = 0;
    int weight = FW_BOLD;
    std::string face = "Arial";

    if (g_fonts)
        g_fonts->GetFontSettings(sizeOffset, weight, face);

    // R5: 12 below 1024, 14 from 1024 upward; negative fontsize does not shrink it.
    const int baseHeight = screenWidth >= 1024 ? 14 : 12;
    const int textHeight = std::max(baseHeight, baseHeight + sizeOffset * 2);

    m_pFont = new CFont(m_pDevice, textHeight, 0, weight, face.c_str());
    m_pWeaponFont1 = new CFont(m_pDevice, textHeight + 8, 0, FW_NORMAL, kWeaponFontFace, CP_ACP, SYMBOL_CHARSET);
    m_pWeaponFont2 = new CFont(m_pDevice, textHeight + 12, 0, FW_NORMAL, kWeaponFontFace, CP_ACP, SYMBOL_CHARSET);

    if (!m_pFont->IsValid() || !m_pWeaponFont1->IsValid() || !m_pWeaponFont2->IsValid())
    {
        DeleteObject(m_pFont);
        DeleteObject(m_pWeaponFont1);
        DeleteObject(m_pWeaponFont2);

        m_nLongestNickWidth = 0;
        m_position[0] = 0;
        m_position[1] = 0;
        return;
    }

    m_nLongestNickWidth = MeasureText(m_pFont, kLongestNick);

    POINT spriteSize{};
    GetWeaponSpriteRectSize(&spriteSize);

    m_position[0] = spriteSize.x;
    m_position[1] = spriteSize.y;

    m_pSprite = nullptr;
}

void CDeathWindow::OnLostDevice()
{
    for (CFont* font : {m_pFont, m_pWeaponFont1, m_pWeaponFont2, m_pAuxFont1, m_pAuxFont2})
        if (font)
            font->OnLostDevice();
}

void CDeathWindow::OnResetDevice()
{
    ResetFonts();
    for (CFont* font : {m_pAuxFont1, m_pAuxFont2})
        if (font)
            font->OnResetDevice();
}

void CDeathWindow::Draw()
{
    PROFILE_SCOPE(DeathWindow);
    if (!m_bEnabled || !m_pFont || !m_pWeaponFont1 || !m_pWeaponFont2)
        return;

    int screenWidth = 0;
    int screenHeight = 0;

    if (!GetScreenSize(m_pDevice, screenWidth, screenHeight))
        return;

    int baseY = static_cast<int>(static_cast<float>(screenHeight) * 0.3f);
    int baseX = static_cast<int>(static_cast<float>(screenWidth) * 0.75f);

    const int requiredWidth = m_position[0] + m_nLongestNickWidth * 2;

    if (baseX + requiredWidth > screenWidth)
        baseX = screenWidth - requiredWidth;

    RECT rect{};
    rect.left = baseX;
    rect.top = baseY;
    rect.right = baseX + 60;
    rect.bottom = baseY + 30;

    for (const Entry& entry : m_entry)
    {
        const bool hasKiller = entry.m_szKiller[0] != '\0';
        const bool hasVictim = entry.m_szVictim[0] != '\0';

        if (hasKiller && hasVictim)
        {
            const int killerWidth = MeasureText(m_pFont, entry.m_szKiller);

            RECT killerRect = rect;
            killerRect.left = baseX + m_nLongestNickWidth - killerWidth;

            DrawText(entry.m_szKiller, killerRect, entry.m_killerColor, DT_LEFT);

            const int spriteLeft = baseX + m_nLongestNickWidth + 3;

            RECT spriteRect = rect;
            spriteRect.left = spriteLeft;
            spriteRect.right = spriteLeft + 35;

            DrawWeaponSprite(GetWeaponSpriteId(entry.m_nWeapon), spriteRect, kDefaultSpriteColor);

            RECT victimRect = rect;
            victimRect.left = spriteLeft + m_position[0];
            victimRect.right = spriteRect.right + m_position[0];

            DrawText(entry.m_szVictim, victimRect, entry.m_victimColor, DT_LEFT);
        }
        else if (!hasKiller && hasVictim)
        {
            const int victimWidth = MeasureText(m_pFont, entry.m_szVictim);

            RECT victimRect = rect;
            victimRect.left = baseX + m_nLongestNickWidth - victimWidth;

            DrawText(entry.m_szVictim, victimRect, entry.m_victimColor, DT_LEFT);

            const int spriteLeft = baseX + m_nLongestNickWidth + 3;

            RECT spriteRect = rect;
            spriteRect.left = spriteLeft;
            spriteRect.right = spriteLeft + 35;

            D3DCOLOR spriteColor = kDefaultSpriteColor;

            if (static_cast<unsigned char>(entry.m_nWeapon) == SPECIAL_ENTRY_CONNECT)
                spriteColor = kConnectSpriteColor;
            else if (static_cast<unsigned char>(entry.m_nWeapon) == SPECIAL_ENTRY_DISCONNECT)
                spriteColor = kDisconnectSpriteColor;

            DrawWeaponSprite(GetWeaponSpriteId(entry.m_nWeapon), spriteRect, spriteColor);
        }

        const int rowHeight = m_position[1] + 5;

        baseY += rowHeight;
        rect.top += rowHeight;
        rect.bottom += rowHeight;
        rect.left = baseX;
        rect.right = baseX + 60;
    }
}

void CDeathWindow::AddEntry(const char* killer, const char* victim, D3DCOLOR killerColor, D3DCOLOR victimColor, char weapon)
{
    PushBack();

    Entry& entry = m_entry[MAX_DEATHMESSAGES - 1];

    entry.m_nWeapon = weapon;
    entry.m_killerColor = killerColor;
    entry.m_victimColor = victimColor;

    CopyName(entry.m_szKiller, killer);
    CopyName(entry.m_szVictim, victim);
}

void CDeathWindow::AddMessage(const char* killer, const char* victim, D3DCOLOR killerColor, D3DCOLOR victimColor, char weapon)
{
    AddEntry(killer, victim, killerColor, victimColor, weapon);
}
