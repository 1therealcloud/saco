/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>

namespace cgui
{
    class Dialog;
    class ListBox;
    class EditBox;
    class Control;
}

class CDialog
{
public:
    enum DialogType
    {
        DIALOG_MESSAGEBOX,
        DIALOG_INPUT,
        DIALOG_LIST,
        DIALOG_PASSWORD,
        DIALOG_TABLIST,
        DIALOG_HEADERSLIST
    };

    enum
    {
        LISTBOX_ID = 18,
        EDITBOX_ID = 19,
        LEFT_BUTTON_ID = 20,
        RIGHT_BUTTON_ID = 21,
        CAPTION_ID = 22,

        MAX_COLUMNS = 4,
        MAX_COLUMN_TEXT = 128,
        MAX_INPUT_TEXT = 128
    };

public:
    // exact SA-MP 0.3.7-R5 layout
    IDirect3DDevice9* m_pDevice;
    unsigned long m_position[2];
    unsigned long m_size[2];
    unsigned long m_buttonOffset[2];
    cgui::Dialog* m_pDialog;
    cgui::ListBox* m_pListbox;
    cgui::EditBox* m_pEditbox;
    BOOL m_bIsActive;
    int m_nType;
    unsigned int m_nId;
    char* m_szText;
    unsigned long m_textSize[2];
    char m_szCaption[65];
    BOOL m_bServerside;

    // recovered R5 fields hidden as pad[536] in SAMP-API
    int m_nColumns;
    char m_szColumnHeaders[MAX_COLUMNS][MAX_COLUMN_TEXT + 1];
    int m_columnWidths[MAX_COLUMNS];

public:
    explicit CDialog(IDirect3DDevice9* device);
    ~CDialog();

    CDialog(const CDialog&) = delete;
    CDialog& operator=(const CDialog&) = delete;

    void GetScreenRect(RECT* rect) const;
    int GetTextScreenLength(const char* text) const;
    int GetFontHeight() const;

    void Hide();
    void ResetDialogControls(cgui::Dialog* dialog);
    void Show(int id, int type, const char* caption, const char* text, const char* leftButton, const char* rightButton, BOOL serverSide);
    void Close(char processButton);
    void Draw();
    void UpdateRects();

    bool MsgProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    bool IsActive() const;

private:
    static void GuiEvent(unsigned int event, int controlId, cgui::Control* control, void* context);

    void EnsureControls();
    void SetControlState(bool editVisible, bool listVisible);
    void SetButtons(const char* leftButton, const char* rightButton);
    void SetCaption(const char* caption);

    bool BuildList(const char* text, SIZE* size);
    bool BuildTabList(const char* text, bool headers, SIZE* size);
    void RebuildListItems();

    void DrawDialogText();
    void DrawColumnHeaders();
};
