#include <stdafx.h>

#include "main.h"

#include "CInput.h"
#include "render/render.h"
#include "CChat.h"
#include "CGui.h"

#include "net/CNetGame.h"
#ifdef _DEBUG
#   include "game/Debug.h"
#   include "config/CSettings.h"
#endif // _DEBUG

namespace
{
    RECT g_inputRect{};

}

CInput::CInput(IDirect3DDevice9* device)
{
    m_pDevice = device;
    m_pGameUI = nullptr;
    m_pEditbox = nullptr;

    std::memset(m_commandProc, 0, sizeof(m_commandProc));
    std::memset(m_szCommandName, 0, sizeof(m_szCommandName));

    m_nCommandCount = 0;
    m_bEnabled = FALSE;
    std::memset(m_szInput, 0, sizeof(m_szInput));
    std::memset(m_szRecallBuffer, 0, sizeof(m_szRecallBuffer));
    std::memset(m_szCurrentBuffer, 0, sizeof(m_szCurrentBuffer));
    m_nCurrentRecall = -1;
    m_nTotalRecall = 0;
    m_pDefaultCommand = nullptr;
}

void CInput::GetRect(RECT* rect)
{
    if (!rect)
        return;

    *rect = g_inputRect;
}

void CInput::Open()
{
    if (m_bEnabled)
        return;

    UpdateLayout();
    if (m_pEditbox)
    {
        m_pEditbox->SetText(m_szInput);
        m_pEditbox->SetVisible(true);
        m_pEditbox->SetEnabled(true);
        m_pGameUI->RequestFocus(m_pEditbox);
    }
    m_bEnabled = TRUE;
    EnableCursor();
}

void CInput::UpdateLayout()
{
    const RECT client = CGui::GetClientRect();

    long width = static_cast<long>(static_cast<float>(client.right) * 0.6f);
    width = std::min<long>(width, 800);

    const long chatBottom = g_chat ? g_chat->m_nWindowBottom : 0;

    if (m_pEditbox)
    {
        const RECT current = m_pEditbox->GetRect();
        if (current.left == 40 && current.top == chatBottom && current.right == 40 + width && current.bottom == chatBottom + 40 && g_inputRect.right == width + 100 &&
            g_inputRect.bottom == chatBottom + 50)
            return;
    }

    // Original editbox: x=40, y=chat bottom, width=min(clientWidth*0.6, 800).
    // GetRect in R5 exposes the dialog extent, not the editbox rectangle itself.
    g_inputRect.left = 0;
    g_inputRect.top = 0;
    g_inputRect.right = width + 100;
    g_inputRect.bottom = chatBottom + 50;

    if (m_pEditbox)
    {
        m_pEditbox->SetLocation(40, chatBottom);
        m_pEditbox->SetSize(width, 40);
        m_pGameUI->SetSize(width + 100, chatBottom + 50);
    }
}

void CInput::Close()
{
    if (!m_bEnabled)
        return;

    if (m_pEditbox)
    {
        Copy128(m_szInput, m_pEditbox->GetText());
        m_pEditbox->SetVisible(false);
        m_pEditbox->SetEnabled(false);
        if (m_pGameUI->GetFocus() == m_pEditbox)
            m_pGameUI->RequestFocus(nullptr);
    }
    m_bEnabled = FALSE;
    render::SetCursorMode(0, true);
}

void CInput::AddRecall(const char* text)
{
    if (!text)
        return;

    // R5 shifts recall[0..8] to recall[1..9] and stores the new line in slot 0.
    for (int i = RECALL_COUNT - 1; i > 0; --i)
        std::strcpy(m_szRecallBuffer[i], m_szRecallBuffer[i - 1]);

    Copy128(m_szRecallBuffer[0], text);

    if (m_nTotalRecall < RECALL_COUNT)
        ++m_nTotalRecall;
}

void CInput::RecallUp()
{
    if (m_nCurrentRecall >= m_nTotalRecall - 1)
        return;

    if (m_nCurrentRecall == -1)
        Copy128(m_szCurrentBuffer, GetText());

    ++m_nCurrentRecall;
    SetText(m_szRecallBuffer[m_nCurrentRecall]);
}

void CInput::RecallDown()
{
    --m_nCurrentRecall;

    if (m_nCurrentRecall >= 0)
    {
        SetText(m_szRecallBuffer[m_nCurrentRecall]);
        return;
    }

    if (m_nCurrentRecall == -1)
        SetText(m_szCurrentBuffer);

    m_nCurrentRecall = -1;
}

void CInput::EnableCursor()
{
    if (m_bEnabled)
        render::SetCursorMode(2, false);
}

CMDPROC CInput::GetCommandHandler(const char* name)
{
    if (!name)
        return nullptr;

    for (int i = 0; i < m_nCommandCount; ++i)
    {
        if (std::strcmp(name, m_szCommandName[i]) == 0)
            return m_commandProc[i];
    }

    return nullptr;
}

void CInput::SetDefaultCommand(CMDPROC handler)
{
    m_pDefaultCommand = handler;
}

void CInput::AddCommand(const char* name, CMDPROC handler)
{
    if (!name || m_nCommandCount >= MAX_CLIENT_CMDS)
        return;

    // R5 rejects strlen(name) >= 32 instead of truncating it.
    if (std::strlen(name) >= MAX_CMD_LENGTH)
        return;

    m_commandProc[m_nCommandCount] = handler;
    std::strcpy(m_szCommandName[m_nCommandCount], name);
    ++m_nCommandCount;
}

BOOL CInput::MsgProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!m_bEnabled)
        return FALSE;

    if (m_pEditbox)
    {
        D3DDEVICE_CREATION_PARAMETERS parameters{};
        if (m_pDevice)
            m_pDevice->GetCreationParameters(&parameters);
        return m_pGameUI->MsgProc(parameters.hFocusWindow, message, wParam, lParam) ? TRUE : FALSE;
    }

    // R5 delegates this to the DXUT IME editbox path. This replacement keeps the
    // same CInput state but performs the minimal Win32 editing directly.
    if (message == WM_CHAR)
    {
        if (wParam == '\r')
        {
            ProcessInput();
            return TRUE;
        }

        if (wParam == '\b')
        {
            const std::size_t length = std::strlen(m_szInput);
            if (length)
                m_szInput[length - 1] = '\0';
            return TRUE;
        }

        if (wParam >= 0x20 && wParam <= 0xFF)
        {
            const std::size_t length = std::strlen(m_szInput);
            if (length < MAX_INPUT_LENGTH)
            {
                m_szInput[length] = static_cast<char>(wParam);
                m_szInput[length + 1] = '\0';
            }
            return TRUE;
        }
    }

    return FALSE;
}

void CInput::ResetDialogControls(cgui::Dialog* gameUI)
{
    if (m_pEditbox)
        Copy128(m_szInput, m_pEditbox->GetText());
    if (m_pGameUI)
    {
        m_pGameUI->RemoveControl(1);
        m_pGameUI->SetCallback(nullptr);
    }
    m_pGameUI = gameUI;
    m_pEditbox = nullptr;
    if (!gameUI)
        return;
    m_pEditbox = gameUI->AddEditBox(1, m_szInput, 10, 175, 570, 40);
    m_pEditbox->SetMaxLength(MAX_INPUT_LENGTH);
    m_pEditbox->SetTextColor(0xFFFFFFFFu);
    m_pEditbox->SetSelectionColor(0xFFB92228u);
    m_pEditbox->SetCaretColor(0xFF969696u);
    m_pEditbox->SetVisible(IsEnabled());
    m_pEditbox->SetEnabled(IsEnabled());
    gameUI->SetCallback(
        [](unsigned int event, int id, cgui::Control*, void* context) {
            if (event == cgui::EVENT_EDITBOX_STRING && id == 1)
                static_cast<CInput*>(context)->ProcessInput();
        },
        this);
    if (IsEnabled())
    {
        UpdateLayout();
        gameUI->RequestFocus(m_pEditbox);
    }
}

void CInput::ProcessInput()
{
    if (m_pEditbox)
        Copy128(m_szInput, m_pEditbox->GetText());
    // Original obtains up to 128 bytes from CDXUTEditBox into m_szInput first.
    m_szInput[MAX_INPUT_LENGTH] = '\0';

    if (m_szInput[0] != '\0')
    {
        AddRecall(m_szInput);
        m_nCurrentRecall = -1;

        if (m_szInput[0] != '/')
        {
            if (m_pDefaultCommand)
                m_pDefaultCommand(m_szInput);
        }
        else
        {
            char commandLine[MAX_INPUT_LENGTH + 1]{};
            Copy128(commandLine, m_szInput);

            char* name = commandLine + 1;
            char* separator = name;

            while (*separator && *separator != ' ')
                ++separator;

            if (*separator == '\0')
            {
                if (CMDPROC command = GetCommandHandler(name))
                {
                    command("");
                }
                else
                {
                    if (g_netGame)
                        g_netGame->SendCommand(m_szInput);
                }
            }
            else
            {
                *separator = '\0';

                if (CMDPROC command = GetCommandHandler(name))
                {
                    command(separator + 1);
                }
                else
                {
                    if (g_netGame)
                        g_netGame->SendCommand(m_szInput);
                }
            }
        }

        m_szInput[0] = '\0';
    }

    if (m_pEditbox)
        m_pEditbox->SetText("");
    if (m_bEnabled)
    {
        if (g_chat)
            g_chat->ScrollToBottom();

        Close();
    }
}

void CInput::SetText(const char* text)
{
    Copy128(m_szInput, text ? text : "");
    if (m_pEditbox)
        m_pEditbox->SetText(m_szInput);
}

const char* CInput::GetText() const
{
    return m_pEditbox ? m_pEditbox->GetText() : m_szInput;
}

bool CInput::IsEnabled() const
{
    return m_bEnabled != FALSE;
}

void CInput::Copy128(char* destination, const char* source)
{
    if (!destination)
        return;

    if (!source)
    {
        destination[0] = '\0';
        return;
    }

    std::strncpy(destination, source, MAX_INPUT_LENGTH);
    destination[MAX_INPUT_LENGTH] = '\0';
}
