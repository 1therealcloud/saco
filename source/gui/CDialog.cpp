/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#include <stdafx.h>

#include "CDialog.h"
#include "utils/TextEncoding.inl"

#include "CGui.h"
#include "render/CFonts.h"
#include "CInput.h"
#include "render/render.h"
#include "main.h"
#include "net/CNetGame.h"

#include <cstdlib>

namespace
{
    constexpr D3DCOLOR kDialogTextColor = 0xFFA9C4E4u;
    constexpr D3DCOLOR kEditTextColor = 0xFFFFFFFFu;
    constexpr D3DCOLOR kEditCaretColor = 0xFF969696u;
    constexpr D3DCOLOR kEditSelectionColor = 0xFFB92228u;
    constexpr D3DCOLOR kDialogBackground = 0xDC050505u;

    constexpr int kDialogDefaultWidth = 600;
    constexpr int kDialogDefaultHeight = 300;
    constexpr int kButtonWidth = 100;
    constexpr int kButtonHeight = 30;
    constexpr int kDialogMinListWidth = 400;
    constexpr int kDialogMaxListWidth = 800;
    constexpr int kListMinHeight = 200;
    constexpr int kListMaxHeight = 400;

    struct RuntimeState
    {
        cgui::Static* caption = nullptr;
        std::vector<std::string> rows;
        std::vector<std::string> listRows;
        int captionHeight = 0;
        bool password = false;
    };

    std::unordered_map<CDialog*, RuntimeState> g_runtime;
    RuntimeState& State(CDialog* dialog)
    {
        return g_runtime[dialog];
    }

    const RuntimeState* FindState(const CDialog* dialog)
    {
        const auto it = g_runtime.find(const_cast<CDialog*>(dialog));
        return it == g_runtime.end() ? nullptr : &it->second;
    }

    void CopyString(char* destination, std::size_t destinationSize, const char* source)
    {
        ZoneScoped;
        if (!destination || destinationSize == 0)
            return;

        destination[0] = '\0';
        if (!source)
            return;

        const auto length = text::Boundary(source, destinationSize - 1);
        std::memcpy(destination, source, length);
        destination[length] = '\0';
    }

    std::string StripColors(const char* text)
    {
        std::string result = text ? text : "";
        CFont::StripColorTags(result);
        return result;
    }

    std::vector<std::string> SplitLines(const char* text)
    {
        ZoneScoped;
        std::vector<std::string> lines;
        if (!text)
            return lines;

        const char* start = text;
        const char* current = text;

        while (*current)
        {
            if (*current == '\n')
            {
                std::string line(start, current);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty())
                    lines.emplace_back(std::move(line));
                start = current + 1;
            }
            ++current;
        }

        if (current != start)
        {
            std::string line(start, current);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
                lines.emplace_back(std::move(line));
        }

        return lines;
    }

    std::vector<std::string> SplitColumns(const std::string& line)
    {
        ZoneScoped;
        std::vector<std::string> columns;
        std::size_t start = 0;

        while (start <= line.size() && columns.size() < CDialog::MAX_COLUMNS)
        {
            const std::size_t tab = line.find('\t', start);
            if (tab == std::string::npos)
            {
                columns.emplace_back(line.substr(start));
                break;
            }

            columns.emplace_back(line.substr(start, tab - start));
            start = tab + 1;
        }

        if (columns.empty())
            columns.emplace_back();

        return columns;
    }

    int MeasureLittle(const char* text)
    {
        ZoneScoped;
        CFonts* fonts = g_fonts.get();
        if (!fonts || !text || !*text)
            return 0;

        SIZE size{};
        fonts->GetLittleTextScreenSize(&size, text, DT_CALCRECT | DT_SINGLELINE | DT_LEFT);
        return size.cx;
    }

    int GetLittleHeight()
    {
        CFonts* fonts = g_fonts.get();
        if (!fonts)
            return std::max(1, CGui::GetFontHeight());

        return std::max(1, fonts->GetLittleCharHeight());
    }

    int GetNormalHeight()
    {
        CFonts* fonts = g_fonts.get();
        if (!fonts)
            return std::max(1, CGui::GetFontHeight());

        return std::max(1, fonts->GetCharHeight());
    }

    std::string FormatColumns(CDialog* dialog, const std::vector<std::string>& columns)
    {
        ZoneScoped;
        std::string result;

        for (int i = 0; i < dialog->m_nColumns; ++i)
        {
            const std::string cell = i < static_cast<int>(columns.size()) ? StripColors(columns[i].c_str()) : "";
            result += cell;

            if (i + 1 >= dialog->m_nColumns)
                continue;

            const int width = MeasureLittle(cell.c_str());
            const int target = std::max(width, dialog->m_columnWidths[i]);
            const int spaceWidth = std::max(1, MeasureLittle(" "));
            const int count = std::max(1, (target - width + spaceWidth - 1) / spaceWidth);
            result.append(static_cast<std::size_t>(count), ' ');
        }

        return result;
    }
}

CDialog::CDialog(IDirect3DDevice9* device)
    : m_pDevice(device), m_position{0, 0}, m_size{kDialogDefaultWidth, kDialogDefaultHeight}, m_buttonOffset{kButtonWidth, kButtonHeight}, m_pDialog(nullptr), m_pListbox(nullptr),
      m_pEditbox(nullptr), m_bIsActive(FALSE), m_nType(DIALOG_MESSAGEBOX), m_nId(0), m_szText(nullptr), m_textSize{0, 0}, m_szCaption{}, m_bServerside(FALSE), m_nColumns(0),
      m_szColumnHeaders{}, m_columnWidths{}
{
    State(this);
    if (CGui::IsInitialized())
    {
        if (cgui::Dialog* dialog = CGui::GetDialog(CGui::DialogId::Dialog))
            ResetDialogControls(dialog);
    }
}

CDialog::~CDialog()
{
    ZoneScoped;
    if (m_pDialog)
    {
        if (m_bIsActive)
            Hide();
        m_pDialog->SetCallback(nullptr);
        m_pDialog->RemoveAllControls();
    }
    if (m_szText)
    {
        std::free(m_szText);
        m_szText = nullptr;
    }

    g_runtime.erase(this);
}

void CDialog::GetScreenRect(RECT* rect) const
{
    ZoneScoped;
    if (!rect)
        return;

    rect->left = static_cast<LONG>(m_position[0]);
    rect->top = static_cast<LONG>(m_position[1]);
    rect->right = rect->left + static_cast<LONG>(m_size[0]);
    rect->bottom = rect->top + static_cast<LONG>(m_size[1]);
}

int CDialog::GetTextScreenLength(const char* text) const
{
    ZoneScoped;
    if (!text || !*text)
        return -1;

    CFonts* fonts = g_fonts.get();
    if (!fonts)
        return -1;

    SIZE size{};
    fonts->GetLittleTextScreenSize(&size, text, DT_CALCRECT | DT_EXPANDTABS | DT_NOCLIP);
    return size.cx;
}

int CDialog::GetFontHeight() const
{
    return GetNormalHeight();
}

void CDialog::Hide()
{
    ZoneScoped;
    if (m_pDialog)
    {
        m_pDialog->SetVisible(false);
        m_pDialog->RequestFocus(nullptr);
    }

    const bool wasActive = m_bIsActive != FALSE;
    m_bIsActive = FALSE;
    if (wasActive)
        render::SetCursorMode(g_input && g_input->IsEnabled() ? 2 : 0, false);
}

void CDialog::ResetDialogControls(cgui::Dialog* dialog)
{
    ZoneScoped;
    m_pDialog = dialog;
    m_pListbox = nullptr;
    m_pEditbox = nullptr;

    if (!m_pDialog)
        return;

    m_pDialog->RemoveAllControls();
    m_pDialog->SetCallback(&CDialog::GuiEvent, this);
    m_pDialog->SetLocation(0, 0);
    m_pDialog->SetSize(kDialogDefaultWidth, kDialogDefaultHeight);
    m_pDialog->EnableMouseInput(true);
    m_pDialog->EnableKeyboardInput(true);
    m_pDialog->SetBackgroundColors(kDialogBackground);
    m_pDialog->SetVisible(false);

    RuntimeState& state = State(this);
    state.caption = m_pDialog->AddStatic(CAPTION_ID, "", 10, 0, kDialogDefaultWidth - 20, GetNormalHeight() + 4);
    state.caption->SetVisible(false);

    m_pListbox = m_pDialog->AddListBox(LISTBOX_ID, 10, 10, static_cast<int>(m_size[0]), static_cast<int>(m_size[1]) - 100);
    m_pListbox->SetEnabled(false);
    m_pListbox->SetVisible(false);

    m_pDialog->AddButton(LEFT_BUTTON_ID, "BUTTON1", 10, 5, static_cast<int>(m_buttonOffset[0]), static_cast<int>(m_buttonOffset[1]));
    m_pDialog->AddButton(RIGHT_BUTTON_ID, "BUTTON2", 110, 5, static_cast<int>(m_buttonOffset[0]), static_cast<int>(m_buttonOffset[1]));

    m_pEditbox = m_pDialog->AddEditBox(EDITBOX_ID, "", 10, 175, 570, 40);
    m_pEditbox->SetMaxLength(MAX_INPUT_TEXT);
    m_pEditbox->SetTextColor(kEditTextColor);
    m_pEditbox->SetCaretColor(kEditCaretColor);
    m_pEditbox->SetSelectionColor(kEditSelectionColor);
    m_pEditbox->SetEnabled(false);
    m_pEditbox->SetVisible(false);
}

void CDialog::Show(int id, int type, const char* caption, const char* text, const char* leftButton, const char* rightButton, BOOL serverSide)
{
    ZoneScoped;
    if (id < 0)
    {
        if (m_bIsActive)
            Hide();
        return;
    }
    if (type < DIALOG_MESSAGEBOX || type > DIALOG_HEADERSLIST || id > 0xFFFF)
        return;

    EnsureControls();
    if (!m_pDialog)
        return;

    m_nId = static_cast<unsigned int>(id);
    m_nType = type;
    m_bServerside = serverSide;

    std::memset(m_szCaption, 0, sizeof(m_szCaption));
    CopyString(m_szCaption, sizeof(m_szCaption), caption ? caption : "");

    if (m_szText)
    {
        std::free(m_szText);
        m_szText = nullptr;
    }

    const char* sourceText = text ? text : "";
    const std::size_t length = std::strlen(sourceText);
    m_szText = static_cast<char*>(std::calloc(1, length + 64));
    if (!m_szText)
        return;
    std::memcpy(m_szText, sourceText, length + 1);

    CFonts* fonts = g_fonts.get();
    SIZE textSize{};
    if (fonts)
        fonts->GetLittleTextScreenSize(&textSize, m_szText, DT_EXPANDTABS);
    m_textSize[0] = static_cast<unsigned long>(std::max<LONG>(0, textSize.cx));
    m_textSize[1] = static_cast<unsigned long>(std::max<LONG>(0, textSize.cy));

    SetCaption(m_szCaption);

    RuntimeState& state = State(this);
    state.rows.clear();
    state.listRows.clear();
    state.password = false;
    m_nColumns = 0;
    std::memset(m_szColumnHeaders, 0, sizeof(m_szColumnHeaders));
    std::memset(m_columnWidths, 0, sizeof(m_columnWidths));

    const int captionHeight = state.captionHeight;
    const int fontHeight = GetFontHeight();
    const int listRowHeight = std::max(18, GetLittleHeight());
    SIZE listSize{};

    switch (m_nType)
    {
        case DIALOG_MESSAGEBOX:
            m_size[0] = m_textSize[0] + 40;
            m_size[1] = m_textSize[1] + captionHeight + m_buttonOffset[1] + 25;
            SetControlState(false, false);
            break;

        case DIALOG_INPUT:
        case DIALOG_PASSWORD:
        {
            m_size[0] = m_textSize[0] + 40;
            const int editArea = static_cast<int>(static_cast<float>(fontHeight) * 1.6f + 14.0f);
            m_size[1] = editArea + m_textSize[1] + m_buttonOffset[1] + captionHeight + 25;
            SetControlState(true, false);

            state.password = m_nType == DIALOG_PASSWORD;
            m_pEditbox->SetText("");
            m_pEditbox->SetPassword(state.password);
            m_pEditbox->SetTextColor(kEditTextColor);
            m_pEditbox->SetCaretColor(kEditCaretColor);
            m_pEditbox->SetSelectionColor(kEditSelectionColor);
            break;
        }

        case DIALOG_LIST:
            BuildList(m_szText, &listSize);
            m_size[0] = static_cast<unsigned long>(std::clamp<int>(listSize.cx + 40, kDialogMinListWidth, kDialogMaxListWidth));
            listSize.cy = std::clamp<int>(listSize.cy + listRowHeight * 2, kListMinHeight, kListMaxHeight);
            m_size[1] = static_cast<unsigned long>(captionHeight + m_buttonOffset[1] + listSize.cy + 15);
            SetControlState(false, true);
            m_pListbox->SetLocation(5, 5);
            m_pListbox->SetSize(static_cast<int>(m_size[0]) - 10, listSize.cy);
            break;

        case DIALOG_TABLIST:
        case DIALOG_HEADERSLIST:
        {
            const bool headers = m_nType == DIALOG_HEADERSLIST;
            BuildTabList(m_szText, headers, &listSize);

            int width = listSize.cx + 40;
            if (width < kDialogMinListWidth)
            {
                const int oldContent = listSize.cx;
                width = kDialogMinListWidth;

                if (m_nColumns > 0)
                {
                    const int add = std::max(0, (360 - oldContent) / m_nColumns);
                    for (int i = 0; i < m_nColumns; ++i)
                        m_columnWidths[i] += add;
                    RebuildListItems();
                }
            }
            width = std::min(width, kDialogMaxListWidth);
            m_size[0] = static_cast<unsigned long>(width);

            listSize.cy = std::clamp<int>(listSize.cy + listRowHeight * 2, kListMinHeight, kListMaxHeight);
            const int headerOffset = headers ? GetLittleHeight() + 5 : 5;
            m_size[1] = static_cast<unsigned long>(captionHeight + m_buttonOffset[1] + listSize.cy + headerOffset + 10);

            SetControlState(false, true);
            m_pListbox->SetLocation(5, headerOffset);
            m_pListbox->SetSize(static_cast<int>(m_size[0]) - 10, listSize.cy);
            break;
        }

        default:
            m_size[0] = kDialogDefaultWidth;
            m_size[1] = kDialogDefaultHeight;
            SetControlState(false, false);
            break;
    }

    SetButtons(leftButton, rightButton);
    UpdateRects();

    m_pDialog->SetVisible(true);
    m_pDialog->RestartAppearance();

    if ((m_nType == DIALOG_INPUT || m_nType == DIALOG_PASSWORD) && m_pEditbox)
    {
        m_pDialog->RequestFocus(m_pEditbox);
        m_pEditbox->SetCaret(m_pEditbox->GetText() ? std::strlen(m_pEditbox->GetText()) : 0);
    }
    else if ((m_nType == DIALOG_LIST || m_nType == DIALOG_TABLIST || m_nType == DIALOG_HEADERSLIST) && m_pListbox)
    {
        m_pDialog->RequestFocus(m_pListbox);
        if (!state.rows.empty())
            m_pListbox->SetSelectedIndex(0);
    }

    m_bIsActive = TRUE;
    if (g_input && g_input->IsEnabled())
        g_input->Close();
    render::SetCursorMode(2, false);
}

void CDialog::Close(char processButton)
{
    ZoneScoped;
    if (!m_bIsActive)
        return;

    std::int16_t listItem = -1;
    std::string inputText;

    RuntimeState& state = State(this);

    switch (m_nType)
    {
        case DIALOG_INPUT:
        case DIALOG_PASSWORD:
            if (m_pEditbox)
                inputText = m_pEditbox->GetText();
            break;

        case DIALOG_LIST:
        case DIALOG_TABLIST:
        case DIALOG_HEADERSLIST:
            if (m_pListbox)
            {
                const int selected = m_pListbox->GetSelectedIndex();
                if (selected >= 0 && selected < static_cast<int>(state.rows.size()))
                {
                    listItem = static_cast<std::int16_t>(selected);
                    inputText = state.rows[static_cast<std::size_t>(selected)];
                    if (m_nType == DIALOG_TABLIST || m_nType == DIALOG_HEADERSLIST)
                    {
                        const auto tab = inputText.find('\t');
                        if (tab != std::string::npos)
                            inputText.resize(tab);
                    }
                    CFont::StripColorTags(inputText);
                }
            }
            break;

        default:
            break;
    }

    inputText.resize(text::Prefix(inputText, MAX_INPUT_TEXT));

    const bool serverSide = m_bServerside != FALSE;
    const auto dialogId = static_cast<std::uint16_t>(m_nId);
    Hide();

    if (serverSide && g_netGame)
        g_netGame->DialogResponse(dialogId, processButton != 0, listItem, inputText.c_str());
}

void CDialog::Draw()
{
    ZoneScoped;

    if (!m_bIsActive || !m_pDialog || !m_pDialog->IsVisible())
        return;
    ui::OpacityScope fade(m_pDialog->GetOpacity());

    // CGui::Render() owns the actual dialog/control render in SACO. This method
    // only draws the extra SA-MP text layer that DXUT did not store as controls.
    DrawDialogText();

    if (m_nType == DIALOG_HEADERSLIST)
        DrawColumnHeaders();
}

void CDialog::UpdateRects()
{
    ZoneScoped;
    EnsureControls();
    if (!m_pDialog)
        return;

    const int minimumWidth = static_cast<int>(m_buttonOffset[0] * 2 + 30);
    if (static_cast<int>(m_size[0]) < minimumWidth)
        m_size[0] = static_cast<unsigned long>(minimumWidth);

    m_pDialog->SetSize(static_cast<int>(m_size[0]), static_cast<int>(m_size[1]));

    RECT client{0, 0, 640, 480};
    if (m_pDevice)
    {
        D3DDEVICE_CREATION_PARAMETERS parameters{};
        if (SUCCEEDED(m_pDevice->GetCreationParameters(&parameters)) && parameters.hFocusWindow)
            GetClientRect(parameters.hFocusWindow, &client);
        else
        {
            D3DVIEWPORT9 viewport{};
            if (SUCCEEDED(m_pDevice->GetViewport(&viewport)))
            {
                client.right = static_cast<LONG>(viewport.Width);
                client.bottom = static_cast<LONG>(viewport.Height);
            }
        }
    }

    m_position[0] = static_cast<unsigned long>((client.right - static_cast<LONG>(m_size[0])) / 2);
    m_position[1] = static_cast<unsigned long>((client.bottom - static_cast<LONG>(m_size[1])) / 2);
    m_pDialog->SetLocation(static_cast<int>(m_position[0]), static_cast<int>(m_position[1]));

    RuntimeState& state = State(this);
    const int formHeight = static_cast<int>(m_size[1]);
    if (m_pListbox)
    {
        const int headerHeight = m_nType == DIALOG_HEADERSLIST ? GetLittleHeight() : 0;
        m_pListbox->SetLocation(5, state.captionHeight + headerHeight + 5);
    }

    auto* left = m_pDialog->GetControl<cgui::Button>(LEFT_BUTTON_ID);
    auto* right = m_pDialog->GetControl<cgui::Button>(RIGHT_BUTTON_ID);

    if (left && right && right->IsVisible())
    {
        left->SetLocation(static_cast<int>(m_size[0] / 2) - (static_cast<int>(m_buttonOffset[0]) - 10), formHeight - static_cast<int>(m_buttonOffset[1]) - 5);
        right->SetLocation(static_cast<int>(m_size[0] / 2) + 10, formHeight - static_cast<int>(m_buttonOffset[1]) - 5);
    }
    else if (left)
    {
        left->SetLocation(static_cast<int>(m_size[0] / 2) - static_cast<int>(m_buttonOffset[0] / 2), formHeight - static_cast<int>(m_buttonOffset[1]) - 5);
    }

    if (m_pEditbox)
    {
        const int fontHeight = GetFontHeight();
        const long negativeHeight = static_cast<long>(static_cast<float>(fontHeight) * -1.5f);
        const int editHeight = 14 - static_cast<int>(negativeHeight);
        const int editY = formHeight - static_cast<int>(static_cast<float>(fontHeight) * 1.5f) - static_cast<int>(m_buttonOffset[1]) - 24;

        m_pEditbox->SetSize(static_cast<int>(m_size[0]) - 10, editHeight);
        m_pEditbox->SetLocation(5, editY);
    }

    if (state.caption)
    {
        state.caption->SetLocation(10, 0);
        state.caption->SetSize(static_cast<int>(m_size[0]) - 20, state.captionHeight);
    }
}

bool CDialog::MsgProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    ZoneScoped;
    if (!m_bIsActive)
        return false;

    if (message == WM_KEYUP && wParam == VK_ESCAPE)
    {
        Close(0);
        return true;
    }
    if (message == WM_KEYUP && wParam == VK_RETURN)
    {
        Close(1);
        return true;
    }

    return CGui::MsgProc(CGui::DialogId::Dialog, window, message, wParam, lParam);
}

bool CDialog::IsActive() const
{
    return m_bIsActive != FALSE;
}

void CDialog::GuiEvent(unsigned int event, int controlId, cgui::Control*, void* context)
{
    ZoneScoped;
    auto* dialog = static_cast<CDialog*>(context);
    if (!dialog || !dialog->m_bIsActive)
        return;

    if (event == CGui::EVENT_BUTTON_CLICKED)
    {
        if (controlId == LEFT_BUTTON_ID)
            dialog->Close(1);
        else if (controlId == RIGHT_BUTTON_ID)
            dialog->Close(0);
        return;
    }

    if (event == CGui::EVENT_EDITBOX_STRING && controlId == EDITBOX_ID)
    {
        dialog->Close(1);
        return;
    }

    if (event == CGui::EVENT_LISTBOX_ITEM_DBLCLK && controlId == LISTBOX_ID)
        dialog->Close(1);
}

void CDialog::EnsureControls()
{
    ZoneScoped;
    if (!CGui::IsInitialized())
        return;

    cgui::Dialog* dialog = CGui::GetDialog(CGui::DialogId::Dialog);
    if (!dialog)
        return;

    if (m_pDialog == dialog && m_pListbox && m_pEditbox)
        return;

    ResetDialogControls(dialog);
}

void CDialog::SetControlState(bool editVisible, bool listVisible)
{
    ZoneScoped;
    if (m_pEditbox)
    {
        m_pEditbox->SetVisible(editVisible);
        m_pEditbox->SetEnabled(editVisible);
    }

    if (m_pListbox)
    {
        m_pListbox->SetVisible(listVisible);
        m_pListbox->SetEnabled(listVisible);
    }
}

void CDialog::SetButtons(const char* leftButton, const char* rightButton)
{
    ZoneScoped;
    if (!m_pDialog)
        return;

    auto* left = m_pDialog->GetControl<cgui::Button>(LEFT_BUTTON_ID);
    auto* right = m_pDialog->GetControl<cgui::Button>(RIGHT_BUTTON_ID);

    if (left)
    {
        left->SetText(leftButton ? leftButton : "");
        left->SetVisible(true);
        left->SetEnabled(true);
    }

    const bool hasRight = rightButton && *rightButton;
    if (right)
    {
        right->SetText(hasRight ? rightButton : "");
        right->SetVisible(hasRight);
        right->SetEnabled(hasRight);
    }
}

void CDialog::SetCaption(const char* caption)
{
    ZoneScoped;
    RuntimeState& state = State(this);
    state.captionHeight = 0;

    if (!caption || !*caption)
    {
        if (state.caption)
            state.caption->SetVisible(false);
        return;
    }

    CFonts* fonts = g_fonts.get();
    SIZE size{};
    if (fonts)
        fonts->GetTextScreenSize(&size, "Y", 0);

    state.captionHeight = std::max(0L, size.cy) + 4;

    if (state.caption)
    {
        state.caption->SetText(caption);
        state.caption->SetVisible(true);
    }
}

bool CDialog::BuildList(const char* text, SIZE* size)
{
    ZoneScoped;
    if (size)
        *size = {};

    RuntimeState& state = State(this);
    state.rows.clear();
    state.listRows.clear();

    if (m_pListbox)
        m_pListbox->RemoveAllItems();

    const std::vector<std::string> lines = SplitLines(text);
    int maxWidth = 0;
    int height = 0;
    const int rowHeight = GetLittleHeight();

    for (const std::string& line : lines)
    {
        if (line.empty())
            continue;

        state.rows.push_back(line);
        std::string visible = StripColors(line.c_str());
        state.listRows.push_back(visible);
        maxWidth = std::max(maxWidth, GetTextScreenLength(visible.c_str()));
        height += rowHeight;
    }

    if (m_pListbox)
    {
        for (const std::string& row : state.listRows)
            m_pListbox->AddItem(row.c_str());
    }

    if (size)
    {
        size->cx = maxWidth + 20;
        size->cy = height;
    }

    return true;
}

bool CDialog::BuildTabList(const char* text, bool headers, SIZE* size)
{
    ZoneScoped;
    if (size)
        *size = {};

    RuntimeState& state = State(this);
    state.rows.clear();
    state.listRows.clear();

    if (m_pListbox)
        m_pListbox->RemoveAllItems();

    std::memset(m_szColumnHeaders, 0, sizeof(m_szColumnHeaders));
    std::memset(m_columnWidths, 0, sizeof(m_columnWidths));
    m_nColumns = 0;

    const std::vector<std::string> lines = SplitLines(text);
    if (lines.empty())
        return false;

    std::size_t firstDataLine = 0;
    const std::vector<std::string> firstColumns = SplitColumns(lines.front());
    m_nColumns = std::min<int>(MAX_COLUMNS, static_cast<int>(firstColumns.size()));

    if (headers)
    {
        for (int column = 0; column < m_nColumns; ++column)
        {
            CopyString(m_szColumnHeaders[column], sizeof(m_szColumnHeaders[column]), firstColumns[column].c_str());
            m_columnWidths[column] = MeasureLittle(firstColumns[column].c_str());
        }
        firstDataLine = 1;
    }

    for (std::size_t row = firstDataLine; row < lines.size(); ++row)
    {
        if (lines[row].empty())
            continue;

        const std::vector<std::string> columns = SplitColumns(lines[row]);
        m_nColumns = std::max(m_nColumns, std::min<int>(MAX_COLUMNS, static_cast<int>(columns.size())));

        for (int column = 0; column < std::min<int>(MAX_COLUMNS, static_cast<int>(columns.size())); ++column)
            m_columnWidths[column] = std::max(m_columnWidths[column], MeasureLittle(columns[column].c_str()));

        state.rows.push_back(lines[row]);
    }

    int totalWidth = 0;
    for (int column = 0; column < m_nColumns; ++column)
    {
        m_columnWidths[column] += 20;
        totalWidth += m_columnWidths[column];
    }

    RebuildListItems();

    if (size)
    {
        size->cx = totalWidth;
        size->cy = static_cast<LONG>(state.rows.size() * static_cast<std::size_t>(GetLittleHeight()));
    }

    return true;
}

void CDialog::RebuildListItems()
{
    ZoneScoped;
    RuntimeState& state = State(this);
    state.listRows.clear();

    if (m_pListbox)
        m_pListbox->RemoveAllItems();

    for (const std::string& row : state.rows)
    {
        const std::vector<std::string> columns = SplitColumns(row);
        state.listRows.emplace_back(FormatColumns(this, columns));
    }

    if (m_pListbox)
    {
        for (const std::string& row : state.listRows)
            m_pListbox->AddItem(row.c_str());
    }
}

void CDialog::DrawDialogText()
{
    ZoneScoped;
    if (!m_szText || !*m_szText)
        return;

    if (m_nType != DIALOG_MESSAGEBOX && m_nType != DIALOG_INPUT && m_nType != DIALOG_PASSWORD)
        return;

    CFonts* fonts = g_fonts.get();
    if (!fonts)
        return;

    RECT rect{};
    GetScreenRect(&rect);

    const RuntimeState& state = State(this);
    rect.left += 20;
    rect.top += state.captionHeight + 5;

    fonts->DrawLittleText(m_szText, rect, DT_EXPANDTABS | DT_NOCLIP, kDialogTextColor, true);
}

void CDialog::DrawColumnHeaders()
{
    ZoneScoped;
    if (m_nColumns <= 0)
        return;

    CFonts* fonts = g_fonts.get();
    if (!fonts)
        return;

    RECT screen{};
    GetScreenRect(&screen);

    const RuntimeState& state = State(this);
    int x = screen.left + 16;
    const int y = screen.top + state.captionHeight + 3;

    for (int column = 0; column < m_nColumns; ++column)
    {
        if (m_szColumnHeaders[column][0])
        {
            RECT rect{x, y, screen.right, screen.bottom};
            fonts->DrawLittleText(m_szColumnHeaders[column], rect, DT_SINGLELINE | DT_NOCLIP, kDialogTextColor, true);
        }
        x += m_columnWidths[column];
    }
}
