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
}

struct DownloadedFileInfo
{
    int m_nFileNumber;
    char m_nFileType;
    int m_nFileId;
    char m_nFileState;
    int m_nFileDownloaded;
    int m_nFileSize;
};

class CDownloadWindow
{
public:
    enum
    {
        LISTBOX_ID = 25,
        CAPTION_ID = 26,
        MAX_COLUMN_TEXT = 128
    };

public:
    // exact SA-MP 0.3.DL-1 layout; DXUT pointers are replaced by cgui equivalents.
    DownloadedFileInfo** m_pFilesPool;
    int m_nDownloadedFilesCount;
    IDirect3DDevice9* m_pDevice;
    unsigned int m_position[2];
    int m_nWindowWidth;
    int m_nWindowHeight;
    int field_1C;
    int field_20;
    int field_24;
    cgui::Dialog* m_pDialog;
    cgui::ListBox* m_pListbox;
    int m_bIsWindowActive;
    int m_nRowCount;
    char m_szType[MAX_COLUMN_TEXT + 1];
    char m_szId[MAX_COLUMN_TEXT + 1];
    char m_szProcess[MAX_COLUMN_TEXT + 1];
    char m_szUnused[MAX_COLUMN_TEXT + 1];
    int m_nSpaceSizeForIdText;
    int m_nSpaceSizeForProgressText;
    int field_244;
    int field_248;
    int m_nDownloadedFilesCount_1;
    int m_nFilesSum;
    int field_254;
    int field_258;
    float m_fDownloadSpeed;

public:
    explicit CDownloadWindow(IDirect3DDevice9* device);
    ~CDownloadWindow();

    CDownloadWindow(const CDownloadWindow&) = delete;
    CDownloadWindow& operator=(const CDownloadWindow&) = delete;

    void GetScreenRect(RECT* rect) const;
    int DoesExist(int fileId) const;

    // GUI and progress updates run on the game thread
    void Show(int filesSum);
    void Hide();
    void AddEntry(char fileType, int fileId, char fileState, int fileDownloaded, int fileSize, int errorCode = -1, const char* errorText = nullptr);
    void Draw();
    void ResetDialogControl(cgui::Dialog* dialog);

    void FreePool();
    int PushBack(DownloadedFileInfo* info);
    bool DeleteFile(unsigned int id);
    bool ResizePool(unsigned int count);
    int GetFilesCount() const;
    DownloadedFileInfo* GetFileInfo(unsigned int id) const;

    bool IsActive() const;

private:
    void EnsureControls();
    void UpdateRects();
    void SetCaption(const char* text);
};
