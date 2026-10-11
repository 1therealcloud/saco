#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

class CMenu
{
public:
    enum
    {
        MAX_MENU_ITEMS = 12,
        MAX_COLUMNS = 2,
        MAX_MENU_LINE = 33
    };

    struct Interaction
    {
        BOOL m_bMenu;
        BOOL m_bRow[MAX_MENU_ITEMS];
        BOOL m_bPadding[8 - ((MAX_MENU_ITEMS + 1) % 8)];
    };

public:
    unsigned char m_nId;                                        // 0x000
    char m_szTitle[MAX_MENU_LINE];                              // 0x001
    char m_szItems[MAX_MENU_ITEMS][MAX_COLUMNS][MAX_MENU_LINE]; // 0x022
    char m_szHeader[MAX_COLUMNS][MAX_MENU_LINE];                // 0x33A
    float m_fPosX;                                              // 0x37C
    float m_fPosY;                                              // 0x380
    float m_fFirstColumnWidth;                                  // 0x384
    float m_fSecondColumnWidth;                                 // 0x388
    unsigned char m_nColumns;                                   // 0x38C
    Interaction m_interaction;                                  // 0x38D
    unsigned char m_nColumnCount[MAX_COLUMNS];                  // 0x3CD
    std::uint32_t m_panel;                                      // 0x3CF

public:
    CMenu(float x, float y, char columns, float firstColumnWidth, float secondColumnWidth, const Interaction* interaction);

    CMenu();
    ~CMenu();

    CMenu(const CMenu&) = delete;
    CMenu& operator=(const CMenu&) = delete;

    bool IsVisible() const;
    static bool InstallTextHook();

    void SetTitle(const char* text);
    void AddItem(unsigned char column, unsigned char row, const char* text);
    void SetColumnTitle(unsigned char column, const char* text);

    void Hide();

    char* GetItem(unsigned char column, unsigned char row);
    char* GetTitle();
    char* GetColumnTitle(unsigned char column);

    // R5 helper: row first, column second.
    char* MS(unsigned char row, unsigned char column);

    char GetActiveRow();
    void Show();

    // Hook this from the GTA GetText path.
    // Returns nullptr when the key is not owned by CMenu.
    static const char* ResolveTextKey(const char* key);

private:
    static bool IsValidText(const char* text);
    static const char* GetItemKey(unsigned char row, unsigned char column);
};
