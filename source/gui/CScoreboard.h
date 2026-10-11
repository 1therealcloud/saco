#pragma once

#include <Windows.h>
#include <d3d9.h>
#include <cstdint>
#include <string>
#include <vector>

namespace cgui
{
    class Dialog;
    class ListBox;
    class Control;
}

class CScoreboard
{
public:
    enum SortMode
    {
        SORT_NONE = 0,
        SORT_NAME = 1,
        SORT_SCORE = 2
    };

    enum
    {
        LISTBOX_ID = 0,
        MAX_PLAYERS = 1004
    };

    struct PlayerInfo
    {
        std::uint16_t id = 0;
        std::string name;
        int score = 0;
        int ping = 0;
        D3DCOLOR color = 0xFFFFFFFFu;
        bool local = false;
        bool connected = true;
        bool npc = false;
    };

public:
    // exact SA-MP 0.3.7-R5 layout
    BOOL m_bIsEnabled;   // 0x00
    int m_nPlayerCount;  // 0x04
    float m_position[2]; // 0x08
    float m_fScalar;     // 0x10
    float m_size[2];     // 0x14

    // SAMP-API exposes these as pad_[5]. Their use is recovered from R5.
    float m_fHeaderHeight;     // 0x1C = 60.0f
    float m_fUnknown;          // 0x20 = 0.0f
    float m_fIdColumnWidth;    // 0x24 = 0.085
    float m_fScoreColumnWidth; // 0x28 = 0.265625
    float m_fNameColumnWidth;  // 0x2C = 0.4375

    IDirect3DDevice9* m_pDevice; // 0x30
    cgui::Dialog* m_pDialog;     // 0x34
    cgui::ListBox* m_pListbox;   // 0x38
    int m_nCurrentOffset;        // 0x3C
    BOOL m_bIsSorted;            // 0x40; actually 0/1/2 sort mode

public:
    explicit CScoreboard(IDirect3DDevice9* device);
    ~CScoreboard();

    CScoreboard(const CScoreboard&) = delete;
    CScoreboard& operator=(const CScoreboard&) = delete;

    void Recalc();
    void GetRect(RECT* rect) const;
    void Close(bool hideCursor);
    void ResetDialogControls(cgui::Dialog* dialog);
    void SendNotification();
    void UpdateList();
    void Draw();
    void Enable();

    bool MsgProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void SetSortMode(SortMode mode);
    SortMode GetSortMode() const;

private:
    static void GuiEvent(unsigned int event, int controlId, cgui::Control* control, void* context);

    void DrawHeader();
};
