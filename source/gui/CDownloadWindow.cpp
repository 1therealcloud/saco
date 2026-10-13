/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "CDownloadWindow.h"

#include "main.h"

#include "CGui.h"
#include "render/CFonts.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    constexpr D3DCOLOR kDialogBackground = 0xDC050505u;
    constexpr D3DCOLOR kHeaderColor = 0xFFA9C4E4u;

    constexpr int kWindowWidth = 640;
    constexpr int kWindowHeight = 300;
    constexpr int kTypeColumnWidth = 180;
    constexpr int kIdColumnWidth = 220;

    constexpr const char* kCaption = "Updating models..";
    constexpr const char* kCaptionFormat = "Updating models.. [%d/%d] (%.2f KB/s)";

    struct RuntimeState
    {
        cgui::Static* caption = nullptr;
        int captionHeight = 0;
        int nextRow = 0;
        std::array<int, 10> layout{};
        bool layoutValid = false;
    };

    std::unordered_map<CDownloadWindow*, RuntimeState> g_runtime;
    RuntimeState& State(CDownloadWindow* window)
    {
        return g_runtime[window];
    }

    int GetLittleHeight()
    {
        CFonts* fonts = g_fonts.get();
        if (!fonts)
            return std::max(1, CGui::GetFontHeight());

        return std::max(1, fonts->GetLittleCharHeight());
    }

    int GetCaptionHeight()
    {
        CFonts* fonts = g_fonts.get();
        if (!fonts)
            return std::max(1, CGui::GetFontHeight()) + 4;

        return std::max(1, fonts->GetCharHeight()) + 4;
    }

    void CopyString(char* destination, std::size_t size, const char* source)
    {
        ZoneScoped;
        if (!destination || size == 0)
            return;

        destination[0] = '\0';
        if (!source)
            return;

        std::strncpy(destination, source, size - 1);
        destination[size - 1] = '\0';
    }
}

CDownloadWindow::CDownloadWindow(IDirect3DDevice9* device)
    : m_pFilesPool(nullptr), m_nDownloadedFilesCount(0), m_pDevice(device), m_position{}, m_nWindowWidth(kWindowWidth), m_nWindowHeight(kWindowHeight), field_1C(210), field_20(30),
      field_24(0), m_pDialog(nullptr), m_pListbox(nullptr), m_bIsWindowActive(FALSE), m_nRowCount(3), m_szType{}, m_szId{}, m_szProcess{}, m_szUnused{},
      m_nSpaceSizeForIdText(kTypeColumnWidth), m_nSpaceSizeForProgressText(kIdColumnWidth), field_244(0), field_248(0), m_nDownloadedFilesCount_1(0), m_nFilesSum(-1),
      field_254(static_cast<int>(GetTickCount())), field_258(0), m_fDownloadSpeed(0.0f)
{
    CopyString(m_szType, sizeof(m_szType), "Type");
    CopyString(m_szId, sizeof(m_szId), "ID");
    CopyString(m_szProcess, sizeof(m_szProcess), "Progress");
}

CDownloadWindow::~CDownloadWindow()
{
    ZoneScoped;
    FreePool();

    if (m_pDialog)
    {
        m_pDialog->SetVisible(false);
        m_pDialog->RequestFocus(nullptr);
        m_pDialog->RemoveAllControls();
    }

    m_pListbox = nullptr;
    m_pDialog = nullptr;
    g_runtime.erase(this);
}

void CDownloadWindow::GetScreenRect(RECT* rect) const
{
    ZoneScoped;
    if (!rect)
        return;

    rect->left = static_cast<LONG>(m_position[0]);
    rect->top = static_cast<LONG>(m_position[1]);
    rect->right = rect->left + m_nWindowWidth;
    rect->bottom = rect->top + m_nWindowHeight;
}

int CDownloadWindow::DoesExist(int fileId) const
{
    ZoneScoped;
    for (int i = 0; i < m_nDownloadedFilesCount; ++i)
    {
        DownloadedFileInfo* info = GetFileInfo(static_cast<unsigned int>(i));
        if (info && info->m_nFileId == fileId)
            return i;
    }

    return -1;
}

int CDownloadWindow::DoesExist(char fileType, int fileId) const
{
    for (int i = 0; i < GetFilesCount(); ++i)
        if (auto* info = GetFileInfo(static_cast<unsigned int>(i)); info && info->m_nFileType == fileType && info->m_nFileId == fileId)
            return i;
    return -1;
}

void CDownloadWindow::Show(int filesSum)
{
    ZoneScoped;
    EnsureControls();

    field_254 = static_cast<int>(GetTickCount());
    field_258 = 0;
    m_fDownloadSpeed = 0;
    m_nFilesSum = filesSum;
    SetCaption(kCaption);

    if (m_pDialog)
        m_pDialog->SetVisible(true);

    if (RuntimeState& state = State(this); state.caption)
        state.caption->SetVisible(true);

    if (m_pListbox)
    {
        m_pListbox->SetEnabled(true);
        m_pListbox->SetVisible(true);
    }

    UpdateRects();
    m_bIsWindowActive = TRUE;
}

void CDownloadWindow::Hide()
{
    ZoneScoped;
    if (m_pDialog)
    {
        m_pDialog->SetVisible(false);
        m_pDialog->RequestFocus(nullptr);
    }

    m_bIsWindowActive = FALSE;
    FreePool();
    m_nDownloadedFilesCount_1 = 0;
    m_nFilesSum = -1;
}

void CDownloadWindow::AddEntry(char fileType, int fileId, char fileState, int fileDownloaded, int fileSize, int errorCode, const char* errorText)
{
    ZoneScoped;
    EnsureControls();

    fileDownloaded = std::max(0, fileDownloaded);
    fileSize = std::max(0, fileSize);
    int index = DoesExist(fileType, fileId);
    const auto* oldInfo = index < 0 ? nullptr : GetFileInfo(static_cast<unsigned>(index));
    const bool wasComplete = oldInfo && oldInfo->m_nFileState == 4;
    DownloadedFileInfo* info = nullptr;
    std::uint32_t downloadedDelta = 0;

    if (index == -1)
    {
        info = static_cast<DownloadedFileInfo*>(std::calloc(1, sizeof(DownloadedFileInfo)));
        if (!info)
            return;

        info->m_nFileNumber = State(this).nextRow;
        info->m_nFileType = fileType;
        info->m_nFileId = fileId;
        info->m_nFileState = fileState;
        info->m_nFileDownloaded = fileDownloaded;
        info->m_nFileSize = fileSize;
        downloadedDelta = static_cast<std::uint32_t>(fileDownloaded);

        if (PushBack(info) == -1)
        {
            std::free(info);
            return;
        }

        if (m_pListbox)
        {
            m_pListbox->AddItem(fileType == 1 ? "Model" : "Texture");
            ++State(this).nextRow;
            char id[64]{};
            std::snprintf(id, sizeof(id), "0x%X", fileId);
            m_pListbox->SetItemText(info->m_nFileNumber, 1, id);
            m_pListbox->SetSelectedIndex(info->m_nFileNumber);
        }
    }
    else
    {
        info = GetFileInfo(static_cast<unsigned int>(index));
        if (!info)
            return;

        downloadedDelta = static_cast<std::uint32_t>(std::max(0, fileDownloaded - info->m_nFileDownloaded));
        info->m_nFileSize = fileSize;
        info->m_nFileState = fileState;
        info->m_nFileDownloaded = fileDownloaded;
    }

    if (wasComplete && fileState != 4)
        --m_nDownloadedFilesCount_1;
    float progress = 0.0f;
    if (fileDownloaded != 0 && info->m_nFileSize != 0)
    {
        const std::uint32_t downloaded = static_cast<std::uint32_t>(fileDownloaded);
        const std::uint32_t fileSize = static_cast<std::uint32_t>(info->m_nFileSize);
        progress = std::min(100.0f, static_cast<float>(downloaded) * 100.0f / static_cast<float>(fileSize));
    }

    field_258 = static_cast<int>(static_cast<std::uint32_t>(field_258) + downloadedDelta);

    const DWORD now = GetTickCount();
    const DWORD elapsed = now - static_cast<DWORD>(field_254);
    if (elapsed >= 1000)
    {
        field_254 = static_cast<int>(now);
        const std::uint32_t downloadedBytes = static_cast<std::uint32_t>(field_258);
        m_fDownloadSpeed = downloadedBytes != 0 ? static_cast<float>(downloadedBytes) * (1000.0f / (1024.0f * elapsed)) : 0.0f;
        field_258 = 0;
    }

    char process[64]{};
    bool updateProcess = true;

    switch (fileState)
    {
        case 0:
            CopyString(process, sizeof(process), "0%");
            break;

        case 1:
            std::snprintf(process, sizeof(process), "%.0f%%", progress);
            break;

        case 3:
            if (errorText)
                std::snprintf(process, sizeof(process), "{CC8888}(%d) %.16s..", errorCode, errorText);
            else
                std::snprintf(process, sizeof(process), "{CC8888}Error(%d) %d/%d", errorCode, info->m_nFileDownloaded, info->m_nFileSize);
            break;

        case 4:
            if (!wasComplete)
                ++m_nDownloadedFilesCount_1;
            std::snprintf(process, sizeof(process), "{77AA77}%.0f%%", progress);
            break;

        default:
            updateProcess = false;
            break;
    }

    if (updateProcess && m_pListbox)
        m_pListbox->SetItemText(info->m_nFileNumber, 2, process);

    if (m_nFilesSum > 0)
    {
        char caption[64]{};
        std::snprintf(caption, sizeof(caption), kCaptionFormat, m_nDownloadedFilesCount_1, m_nFilesSum, m_fDownloadSpeed);
        SetCaption(caption);
    }
}

void CDownloadWindow::Draw()
{
    ZoneScoped;
    if (!m_bIsWindowActive)
        return;

    EnsureControls();
    UpdateRects();

    CFonts* fonts = g_fonts.get();
    if (!fonts)
        return;

    RECT screen{};
    GetScreenRect(&screen);

    const RuntimeState& state = State(this);
    int x = screen.left + 16;
    const int y = screen.top + state.captionHeight + 3;

    const char* columns[] = {m_szType, m_szId, m_szProcess};
    const int widths[] = {m_nSpaceSizeForIdText, m_nSpaceSizeForProgressText, field_244};

    const int count = std::min(m_nRowCount, 3);
    for (int i = 0; i < count; ++i)
    {
        fonts->DrawLittleText(columns[i], RECT{x, y, screen.right, screen.bottom}, DT_SINGLELINE | DT_NOCLIP, kHeaderColor, true);
        x += widths[i];
    }
}

void CDownloadWindow::ResetDialogControl(cgui::Dialog* dialog)
{
    ZoneScoped;
    m_pDialog = dialog;
    m_pListbox = nullptr;
    State(this).layoutValid = false;

    if (!m_pDialog)
        return;

    m_pDialog->RemoveAllControls();
    m_pDialog->SetLocation(0, 0);
    m_pDialog->SetSize(m_nWindowWidth, m_nWindowHeight);
    m_pDialog->EnableMouseInput(true);
    m_pDialog->EnableKeyboardInput(false);
    m_pDialog->SetBackgroundColors(kDialogBackground);
    m_pDialog->SetVisible(false);

    RuntimeState& state = State(this);
    state.captionHeight = GetCaptionHeight();
    state.caption = m_pDialog->AddStatic(CAPTION_ID, "", 10, 0, m_nWindowWidth - 20, state.captionHeight);
    state.caption->SetVisible(false);

    m_pListbox = m_pDialog->AddListBox(LISTBOX_ID, 2, 2, m_nWindowWidth - 4, m_nWindowHeight - 35);
    m_pListbox->SetColumnWidth(0, m_nSpaceSizeForIdText);
    m_pListbox->SetColumnWidth(1, m_nSpaceSizeForProgressText);
    m_pListbox->SetColumnWidth(2, m_nWindowWidth - m_nSpaceSizeForIdText - m_nSpaceSizeForProgressText - 32);
    m_pListbox->SetTextPadding(14);
    m_pListbox->SetEnabled(false);
    m_pListbox->SetVisible(false);
}

void CDownloadWindow::FreePool()
{
    ZoneScoped;
    for (int i = 0; i < m_nDownloadedFilesCount; ++i)
        std::free(m_pFilesPool[i]);
    std::free(m_pFilesPool);
    m_pFilesPool = nullptr;
    m_nDownloadedFilesCount = 0;
    m_nDownloadedFilesCount_1 = 0;
    State(this).nextRow = 0;

    if (m_pListbox)
        m_pListbox->RemoveAllItems();
}

int CDownloadWindow::PushBack(DownloadedFileInfo* info)
{
    ZoneScoped;
    if (!info)
        return -1;

    for (int i = 0; i < m_nDownloadedFilesCount; ++i)
    {
        if (!m_pFilesPool[i])
        {
            m_pFilesPool[i] = info;
            return i;
        }
    }

    if (m_nDownloadedFilesCount == std::numeric_limits<int>::max())
        return -1;
    const int index = m_nDownloadedFilesCount;
    if (!ResizePool(static_cast<unsigned int>(m_nDownloadedFilesCount + 1)))
        return -1;

    m_pFilesPool[index] = info;
    return index;
}

bool CDownloadWindow::DeleteFile(unsigned int id)
{
    ZoneScoped;
    if (id >= static_cast<unsigned int>(m_nDownloadedFilesCount))
        return false;

    if (!m_pFilesPool[id])
        return false;
    const int row = m_pFilesPool[id]->m_nFileNumber;
    if (m_pListbox)
        m_pListbox->RemoveItem(row);
    for (int i = 0; i < m_nDownloadedFilesCount; ++i)
        if (m_pFilesPool[i] && m_pFilesPool[i]->m_nFileNumber > row)
            --m_pFilesPool[i]->m_nFileNumber;
    if (State(this).nextRow > 0)
        --State(this).nextRow;
    if (m_pFilesPool[id]->m_nFileState == 4 && m_nDownloadedFilesCount_1 > 0)
        --m_nDownloadedFilesCount_1;
    std::free(m_pFilesPool[id]);
    m_pFilesPool[id] = nullptr;

    int last = -1;
    for (int i = 0; i < m_nDownloadedFilesCount; ++i)
    {
        if (m_pFilesPool[i])
            last = i;
    }

    ResizePool(last == -1 ? 0u : static_cast<unsigned int>(last + 1));
    return true;
}

bool CDownloadWindow::ResizePool(unsigned int count)
{
    ZoneScoped;
    if (count == 0)
    {
        FreePool();
        return true;
    }

    if (count > static_cast<unsigned>(std::numeric_limits<int>::max()) || count > std::numeric_limits<std::size_t>::max() / sizeof(DownloadedFileInfo*))
        return false;
    const unsigned int oldCount = static_cast<unsigned int>(m_nDownloadedFilesCount);
    // shrinking must not silently discard owned file records
    for (unsigned int i = count; i < oldCount; ++i)
        if (m_pFilesPool[i])
            return false;
    void* memory = std::realloc(m_pFilesPool, sizeof(DownloadedFileInfo*) * count);
    if (!memory)
        return false;

    m_pFilesPool = static_cast<DownloadedFileInfo**>(memory);

    if (oldCount < count)
        std::memset(m_pFilesPool + oldCount, 0, sizeof(DownloadedFileInfo*) * (count - oldCount));

    m_nDownloadedFilesCount = static_cast<int>(count);
    return true;
}

int CDownloadWindow::GetFilesCount() const
{
    return m_nDownloadedFilesCount;
}

CDownloadWindow::DownloadedFileInfo* CDownloadWindow::GetFileInfo(unsigned int id) const
{
    if (id >= static_cast<unsigned int>(m_nDownloadedFilesCount))
        return nullptr;

    return m_pFilesPool[id];
}

bool CDownloadWindow::IsActive() const
{
    return m_bIsWindowActive != FALSE;
}

void CDownloadWindow::EnsureControls()
{
    ZoneScoped;
    if (m_pDialog && m_pListbox)
        return;

    if (!CGui::IsInitialized())
        return;

    ResetDialogControl(CGui::GetDialog(CGui::DialogId::Auxiliary));
}

void CDownloadWindow::UpdateRects()
{
    ZoneScoped;
    EnsureControls();
    if (!m_pDialog)
        return;

    const RECT client = CGui::GetClientRect();

    RuntimeState& state = State(this);
    const std::array<int, 10> layout{client.left,     client.top,         client.right,      client.bottom,         m_nWindowWidth,
                                     m_nWindowHeight, GetCaptionHeight(), GetLittleHeight(), m_nSpaceSizeForIdText, m_nSpaceSizeForProgressText};
    if (state.layoutValid && state.layout == layout)
        return;
    state.layout = layout;
    state.layoutValid = true;
    m_pDialog->SetSize(m_nWindowWidth, m_nWindowHeight);
    m_position[0] = static_cast<unsigned int>(client.left + (client.right - client.left - m_nWindowWidth) / 2);
    m_position[1] = static_cast<unsigned int>(client.top + (client.bottom - client.top - m_nWindowHeight) / 2);
    m_pDialog->SetLocation(static_cast<int>(m_position[0]), static_cast<int>(m_position[1]));

    state.captionHeight = GetCaptionHeight();
    field_24 = m_nWindowHeight - state.captionHeight;

    if (state.caption)
    {
        state.caption->SetLocation(10, 0);
        state.caption->SetSize(m_nWindowWidth - 20, state.captionHeight);
    }

    if (m_pListbox)
    {
        const int headerHeight = GetLittleHeight();
        m_pListbox->SetLocation(2, state.captionHeight + headerHeight + 2);
        m_pListbox->SetSize(m_nWindowWidth - 4, std::max(0, field_24 - headerHeight - 4));
        m_pListbox->SetColumnWidth(0, m_nSpaceSizeForIdText);
        m_pListbox->SetColumnWidth(1, m_nSpaceSizeForProgressText);
        m_pListbox->SetColumnWidth(2, std::max(0, m_nWindowWidth - m_nSpaceSizeForIdText - m_nSpaceSizeForProgressText - 32));
    }
}

void CDownloadWindow::SetCaption(const char* text)
{
    RuntimeState& state = State(this);
    if (state.caption)
        state.caption->SetText(text ? text : "");
}
