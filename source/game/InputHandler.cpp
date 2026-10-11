#include <stdafx.h>

#include "InputHandler.h"

#include "main.h"

#include "gui/CChat.h"
#include "gui/CInput.h"
#include "gui/CDialog.h"
#include "gui/CScoreboard.h"
#include "gui/CDownloadWindow.h"
#include "gui/CHelpDialog.h"
#include "gui/CDeathWindow.h"
#include "gui/CObjectEdit.h"
#include "gui/CObjectSelection.h"
#include "gui/CTextDrawSelection.h"
#include "gui/CGui.h"
#include "game/CGame.h"
#include "render/render.h"
#include "render/screenshot.h"
#include "gui/ime.h"

#include <windowsx.h>

namespace game::inputhandler
{
    namespace
    {
        constexpr uintptr_t kGtaWindow = 0xC97C1C;
        constexpr uintptr_t kReadKeyboard = 0x531140;
        constexpr char kWindowTitle[] = "GTA:SA:MP";

        WNDPROC g_originalWndProc = nullptr;
        HWND g_window = nullptr;

        bool g_cursorWasHidden = false;
        bool g_inputOutsideClick = false;
        bool g_scoreboardOutsideClick = false;
        bool g_installed = false;
        std::array<bool, 256> g_consumedKeys{};
        std::array<bool, 256> g_pendingGameKeys{};
        using ReadKeyboard = void(__thiscall*)(void*);
        ReadKeyboard g_readKeyboard = nullptr;

        void __fastcall ReadKeyboardHook(void* manager, void*)
        {
            for (const uintptr_t address : {0xB72CB0u, 0xB72F20u, 0xB73190u})
                filter_game_keyboard(reinterpret_cast<std::uint16_t*>(address));
            g_pendingGameKeys.fill(false);
            g_readKeyboard(manager);
        }

        bool InstallKeyboardHook()
        {
            return hook::install(kReadKeyboard, reinterpret_cast<ReadKeyboard>(&ReadKeyboardHook), &g_readKeyboard);
        }

        bool IsInputAvailable()
        {
            return g_input != nullptr;
        }
        bool IsInputActive()
        {
            return g_input && g_input->IsEnabled();
        }
        bool IsScoreboardAvailable()
        {
            return g_scoreboard != nullptr;
        }
        bool IsScoreboardActive()
        {
            return g_scoreboard && g_scoreboard->m_bIsEnabled != FALSE;
        }
        bool IsDialogAvailable()
        {
            return g_dialog != nullptr;
        }
        bool IsDialogActive()
        {
            return g_dialog && g_dialog->IsActive();
        }
        bool IsMenuVisible()
        {
            return CGame::IsPaused();
        }

        void OpenInput()
        {
            if (!g_input)
                return;

            g_input->Open();
            if (g_input->IsEnabled() && g_dialog && g_dialog->m_pDialog)
                g_dialog->m_pDialog->SetVisible(false);
        }

        void CloseInput()
        {
            if (!g_input)
                return;

            g_input->Close();
            if (g_dialog && g_dialog->m_pDialog)
                g_dialog->m_pDialog->SetVisible(g_dialog->IsActive());
        }

        bool IsPointOutsideRect(LPARAM lParam, const RECT& rect)
        {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);

            return x < rect.left || x > rect.right || y < rect.top || y > rect.bottom;
        }

        LRESULT CallOriginal(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (!g_originalWndProc)
                return DefWindowProcA(window, message, wParam, lParam);

            return CallWindowProcA(g_originalWndProc, window, message, wParam, lParam);
        }

        bool InstallWndProc(HWND window)
        {
            if (!window)
                return false;

            const auto current = reinterpret_cast<WNDPROC>(GetWindowLongA(window, GWL_WNDPROC));
            if (!current)
                return false;

            // another subclass can still call us; leave its wndproc chain intact
            if (g_window)
                return g_window == window && g_originalWndProc;
            if (current == wnd_proc)
                return false;

            SetLastError(ERROR_SUCCESS);
            if (!SetWindowLongA(window, GWL_WNDPROC, reinterpret_cast<LONG>(wnd_proc)) && GetLastError() != ERROR_SUCCESS)
                return false;

            g_originalWndProc = current;
            g_window = window;
            return true;
        }

        bool ClassSelectionActive()
        {
            auto* dialog = CGui::GetDialog(CGui::DialogId::ClassSelection);
            return render::g_classSelectionVisible && dialog && dialog->IsVisible();
        }

        bool AnySampUiActive()
        {
            return IsInputActive() || IsScoreboardActive() || IsDialogActive() || ClassSelectionActive() || (g_textDrawSelection && g_textDrawSelection->m_bIsActive) ||
                   (g_objectEdit && g_objectEdit->m_bEnabled) || (g_objectSelection && g_objectSelection->m_bIsActive) || (g_downloadWindow && g_downloadWindow->IsActive());
        }

        unsigned UiState()
        {
            return static_cast<unsigned>(IsInputActive()) | (static_cast<unsigned>(IsDialogActive()) << 1) | (static_cast<unsigned>(IsScoreboardActive()) << 2) |
                   (static_cast<unsigned>(ClassSelectionActive()) << 3) | (static_cast<unsigned>(g_textDrawSelection && g_textDrawSelection->m_bIsActive) << 4) |
                   (static_cast<unsigned>(g_objectEdit && g_objectEdit->m_bEnabled) << 5) | (static_cast<unsigned>(g_objectSelection && g_objectSelection->m_bIsActive) << 6) |
                   (static_cast<unsigned>(IsMenuVisible()) << 7) | (static_cast<unsigned>(g_downloadWindow && g_downloadWindow->IsActive()) << 8);
        }

        UINT MessageKey(UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
                return wParam < g_consumedKeys.size() ? static_cast<UINT>(wParam) : 0;
            if (message != WM_CHAR && message != WM_SYSCHAR)
                return 0;
            const UINT scanCode = (static_cast<UINT_PTR>(lParam) >> 16) & 0xFF;
            if (scanCode)
                return MapVirtualKeyA(scanCode, MAPVK_VSC_TO_VK);
            if (wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_BACK || wParam == VK_TAB)
                return static_cast<UINT>(wParam);
            const SHORT key = VkKeyScanA(static_cast<char>(wParam));
            return key == -1 ? 0 : key & 0xFF;
        }

        struct UiKeyGuard
        {
            UINT key;
            unsigned state = UiState();

            bool Changed() const
            {
                return state != UiState();
            }

            ~UiKeyGuard()
            {
                // a key that changes the ui belongs to the old ui until its release
                if (key && key < g_consumedKeys.size() && Changed())
                    g_consumedKeys[key] = g_pendingGameKeys[key] = true;
            }
        };

        bool RouteScoreboard(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (!g_scoreboard || !g_scoreboard->m_pDialog || IsMenuVisible() || IsInputActive())
                return false;
            if (message == WM_KEYUP && wParam == VK_TAB)
            {
                if (IsScoreboardActive())
                    g_scoreboard->Close(true);
                else
                    g_scoreboard->Enable();
            }
            return IsScoreboardActive() && g_scoreboard->MsgProc(window, message, wParam, lParam);
        }

        bool RouteClassSelection(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (!ClassSelectionActive() || IsMenuVisible() || IsInputActive() || IsDialogActive() || IsScoreboardActive())
                return false;
            auto* dialog = CGui::GetDialog(CGui::DialogId::ClassSelection);
            return dialog && dialog->MsgProc(window, message, wParam, lParam);
        }

        bool RouteDialog(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (!g_dialog || !g_dialog->m_pDialog || !IsDialogActive() || IsScoreboardActive() || IsInputActive() || IsMenuVisible())
                return false;
            if (!g_dialog->MsgProc(window, message, wParam, lParam))
                return false;
            if (message == WM_CHAR && wParam == VK_ESCAPE)
                g_dialog->Close(0);
            return true;
        }

        bool RouteTextDrawSelection(UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (!g_textDrawSelection)
                return false;
            if (!IsInputActive() && !IsDialogActive() && !IsScoreboardActive() && g_textDrawSelection->MsgProc(message, static_cast<int>(wParam), static_cast<int>(lParam)))
                return true;
            if (g_textDrawSelection->m_bIsActive && message == WM_CHAR && wParam == VK_ESCAPE)
            {
                g_textDrawSelection->Disable();
                return true;
            }
            return false;
        }

        bool RouteObjectEdit(UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (!g_objectEdit || IsInputActive() || IsDialogActive() || IsScoreboardActive() || IsMenuVisible())
                return false;
            if (g_objectEdit->MsgProc(message, static_cast<int>(wParam), static_cast<int>(lParam)))
                return true;
            if (g_objectEdit->m_bEnabled && message == WM_CHAR && wParam == VK_ESCAPE)
            {
                g_objectEdit->Disable(0);
                return true;
            }
            return false;
        }

        bool RouteObjectSelection(UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (!g_objectSelection || IsInputActive() || IsDialogActive() || IsScoreboardActive() || IsMenuVisible())
                return false;
            if (g_objectSelection->MsgProc(message, static_cast<int>(wParam), static_cast<int>(lParam)))
                return true;
            if (g_objectSelection->m_bIsActive && message == WM_CHAR && wParam == VK_ESCAPE)
            {
                g_objectSelection->Enable(FALSE);
                return true;
            }
            return false;
        }
    }

    bool game_input_blocked()
    {
        return AnySampUiActive() || IsMenuVisible();
    }

    int handle_key_up(WPARAM key)
    {
        switch (key)
        {
            case VK_F1:
                if (!IsInputActive() && !IsDialogActive() && g_helpDialog)
                    g_helpDialog->Show();
                return 0;
            case VK_RETURN:
                if (IsDialogAvailable() && IsDialogActive())
                    g_dialog->Close(1);
                return 0;

            case VK_F6:
                if (!IsInputAvailable())
                    return 0;

                if (IsInputActive())
                {
                    CloseInput();
                }
                else
                {
                    OpenInput();
                }
                return 0;

            case VK_F7:
                if (g_chat)
                {
                    // g_chat->SwitchMode(); // R5 also cycles through DISPLAY_MODE_NOSHADOW.
                    g_chat->m_nMode = g_chat->GetMode() == CChat::DISPLAY_MODE_OFF ? CChat::DISPLAY_MODE_NORMAL : CChat::DISPLAY_MODE_OFF;
                    g_chat->m_bRedraw = TRUE;
                }
                return 0;

            case VK_F8:
                screenshot::Request();
                return 0;

            case VK_F9:
                if (g_deathWindow)
                    g_deathWindow->m_bEnabled = !g_deathWindow->m_bEnabled;
                return 0;

            case VK_PRIOR:
                if (!g_chat)
                    return 0;

                g_chat->PageUp();
                return 0;

            case VK_NEXT:
                if (!g_chat)
                    return 0;

                g_chat->PageDown();
                return 0;

            case VK_ESCAPE:
                if (IsInputAvailable() && IsInputActive())
                {
                    CloseInput();
                    return 0;
                }
                return 0;

            case VK_UP:
                if (IsInputAvailable() && IsInputActive())
                {
                    g_input->RecallUp();
                    return 0;
                }
                return 0;

            case VK_DOWN:
                if (IsInputAvailable() && IsInputActive())
                {
                    g_input->RecallDown();
                    return 0;
                }
                return 0;

            default:
                return 0;
        }
    }

    bool handle_char(WPARAM character)
    {
        if (IsInputAvailable() && IsInputActive())
        {
            if (character == VK_ESCAPE)
            {
                CloseInput();
                return true;
            }

            return false;
        }

        if (IsScoreboardAvailable() && IsScoreboardActive())
        {
            if (character == VK_ESCAPE)
            {
                g_scoreboard->Close(true);
                return true;
            }

            return false;
        }

        if (IsDialogAvailable() && IsDialogActive())
        {
            if (character == VK_ESCAPE)
            {
                g_dialog->Close(0);
                return true;
            }

            return false;
        }

        if (character != 'T' && character != 't' && character != '`')
            return false;

        if (!IsInputAvailable() || IsDialogActive() || IsMenuVisible())
            return false;

        if (g_input)
            OpenInput();

        return true;
    }

    LRESULT CALLBACK wnd_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && wParam == FALSE))
        {
            g_consumedKeys.fill(false);
            g_pendingGameKeys.fill(true);
            g_inputOutsideClick = false;
            g_scoreboardOutsideClick = false;
            if (IsScoreboardActive())
                g_scoreboard->Close(true);
            if (IsInputActive())
                CloseInput();
            if (ClassSelectionActive())
                render::SetCursorMode(0, true);

            cgui::ime::MsgProc(window, message, wParam, lParam);
            return CallOriginal(window, message, wParam, lParam);
        }
        if (cgui::ime::MsgProc(window, message, wParam, lParam))
            return 0;
        const UINT key = MessageKey(message, wParam, lParam);
        const bool keyUp = message == WM_KEYUP || message == WM_SYSKEYUP;
        // a ui can consume the release of a key that gta received before it opened
        if (keyUp && key && key < g_pendingGameKeys.size())
            g_pendingGameKeys[key] = true;
        if (key && key < g_consumedKeys.size() && g_consumedKeys[key])
        {
            if (keyUp)
                g_consumedKeys[key] = false;
            return 0;
        }
        if (key && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN))
            g_pendingGameKeys[key] = false;
        const UiKeyGuard keyGuard{keyUp ? 0 : key};
        if (g_input)
        {
            if (g_input->MsgProc(message, wParam, lParam) || keyGuard.Changed())
                return 0;
        }
        if (!IsDialogActive() && !IsScoreboardActive() && g_downloadWindow && g_downloadWindow->IsActive() && g_downloadWindow->m_pDialog &&
            g_downloadWindow->m_pDialog->MsgProc(window, message, wParam, lParam))
            return 0;

        if (RouteScoreboard(window, message, wParam, lParam) || keyGuard.Changed())
            return 0;
        if (RouteClassSelection(window, message, wParam, lParam) || keyGuard.Changed())
            return 0;

        if (RouteDialog(window, message, wParam, lParam))
        {
            // a response may replace the dialog while keeping its active state
            if (message == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE) && key && key < g_consumedKeys.size())
                g_consumedKeys[key] = g_pendingGameKeys[key] = true;
            return 0;
        }

        if (RouteTextDrawSelection(message, wParam, lParam))
            return 0;

        if (RouteObjectEdit(message, wParam, lParam))
            return 0;

        if (RouteObjectSelection(message, wParam, lParam))
            return 0;

        switch (message)
        {
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE && AnySampUiActive())
                    return 0;
                break;

            case WM_KEYUP:
                if (!IsMenuVisible())
                {
                    const bool inputWasActive = IsInputActive();
                    handle_key_up(wParam);
                    if (inputWasActive || IsInputActive())
                        return 0;
                }
                break;

            case WM_CHAR:
                if (!IsMenuVisible() && handle_char(wParam))
                    return 0;
                if (IsInputActive())
                    return 0;
                break;

            case WM_SETCURSOR:
                if (IsMenuVisible() || LOWORD(lParam) != HTCLIENT)
                    break;
                if (!g_cursorWasHidden || GetCursor() != nullptr)
                {
                    SetCursor(nullptr);
                    g_cursorWasHidden = true;
                }
                return 1;

            case WM_LBUTTONDOWN:
                if (IsScoreboardAvailable() && IsScoreboardActive())
                {
                    if (([&] {
                            RECT r{};
                            g_scoreboard->GetRect(&r);
                            return IsPointOutsideRect(lParam, r);
                        })())
                    {
                        g_scoreboardOutsideClick = true;
                        return 0;
                    }
                    break;
                }

                if (IsInputAvailable() && IsInputActive() && !(g_textDrawSelection && g_textDrawSelection->m_bIsActive) && !(g_objectEdit && g_objectEdit->m_bEnabled))
                {
                    if (([&] {
                            RECT r{};
                            g_input->GetRect(&r);
                            return IsPointOutsideRect(lParam, r);
                        })())
                    {
                        g_inputOutsideClick = true;
                        return 0;
                    }
                }
                break;

            case WM_LBUTTONUP:
                if (g_inputOutsideClick)
                {
                    if (IsInputAvailable() && IsInputActive())
                        CloseInput();

                    g_inputOutsideClick = false;
                    return 0;
                }

                if (g_scoreboardOutsideClick)
                {
                    if (IsScoreboardAvailable() && IsScoreboardActive())
                        g_scoreboard->Close(true);

                    g_scoreboardOutsideClick = false;
                    return 0;
                }
                break;

            case WM_MOUSEWHEEL:
                if (IsInputAvailable() && IsInputActive() && g_chat && g_chat->GetMode() != CChat::DISPLAY_MODE_OFF)
                {
                    const int delta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
                    g_chat->Scroll(delta);
                    return 0;
                }
                break;

            default:
                break;
        }

        return CallOriginal(window, message, wParam, lParam);
    }

    void filter_game_keyboard(std::uint16_t* state)
    {
        // gta stores function keys, ascii keys, then the RenderWare special keys
        for (UINT key = 1; key < g_consumedKeys.size(); ++key)
        {
            if (!g_consumedKeys[key] && !g_pendingGameKeys[key])
                continue;
            state[12 + key] = 0;
            if (key >= VK_F1 && key <= VK_F12)
                state[key - VK_F1] = 0;
            const struct
            {
                UINT key;
                unsigned offset;
            } special[] = {{VK_ESCAPE, 0x218},   {VK_INSERT, 0x21A},   {VK_DELETE, 0x21C},   {VK_HOME, 0x21E},   {VK_END, 0x220},    {VK_PRIOR, 0x222},   {VK_NEXT, 0x224},
                           {VK_UP, 0x226},       {VK_DOWN, 0x228},     {VK_LEFT, 0x22A},     {VK_RIGHT, 0x22C},  {VK_SCROLL, 0x22E}, {VK_PAUSE, 0x230},   {VK_NUMLOCK, 0x232},
                           {VK_DIVIDE, 0x234},   {VK_MULTIPLY, 0x236}, {VK_SUBTRACT, 0x238}, {VK_ADD, 0x23A},    {VK_RETURN, 0x23C}, {VK_DECIMAL, 0x23E}, {VK_BACK, 0x254},
                           {VK_TAB, 0x256},      {VK_CAPITAL, 0x258},  {VK_RETURN, 0x25A},   {VK_LSHIFT, 0x25C}, {VK_RSHIFT, 0x25E}, {VK_SHIFT, 0x260},   {VK_LCONTROL, 0x262},
                           {VK_RCONTROL, 0x264}, {VK_LMENU, 0x266},    {VK_RMENU, 0x268},    {VK_LWIN, 0x26A},   {VK_RWIN, 0x26C},   {VK_APPS, 0x26E}};
            for (const auto& entry : special)
                if (entry.key == key || (key == VK_SHIFT && (entry.key == VK_LSHIFT || entry.key == VK_RSHIFT)) ||
                    (key == VK_CONTROL && (entry.key == VK_LCONTROL || entry.key == VK_RCONTROL)) || (key == VK_MENU && (entry.key == VK_LMENU || entry.key == VK_RMENU)))
                    state[entry.offset / 2] = 0;
            if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
                state[0x240 / 2 + key - VK_NUMPAD0] = 0;
        }
    }

    bool install(HWND window)
    {
        if (!window)
            window = *reinterpret_cast<HWND*>(kGtaWindow);
        if (!window)
            return false;
        if (!g_readKeyboard && !InstallKeyboardHook())
            return false;

        SetLastError(ERROR_SUCCESS);
        const LONG classStyle = GetClassLongA(window, GCL_STYLE);
        if (classStyle == 0 && GetLastError() != ERROR_SUCCESS)
            return false;

        SetLastError(ERROR_SUCCESS);
        if (!SetClassLongA(window, GCL_STYLE, classStyle | CS_DBLCLKS) && GetLastError() != ERROR_SUCCESS)
            return false;

        if (!InstallWndProc(window))
            return false;

        SetWindowTextA(window, kWindowTitle);

        g_installed = true;
        return true;
    }

    void rehook()
    {
        if (g_installed && g_window)
            InstallWndProc(g_window);
    }

    void uninstall()
    {
        if (g_readKeyboard)
        {
            MH_DisableHook(reinterpret_cast<void*>(kReadKeyboard));
            MH_RemoveHook(reinterpret_cast<void*>(kReadKeyboard));
            g_readKeyboard = nullptr;
        }
        g_consumedKeys.fill(false);
        g_pendingGameKeys.fill(false);
        if (g_window && g_originalWndProc)
        {
            const auto current = reinterpret_cast<WNDPROC>(GetWindowLongA(g_window, GWL_WNDPROC));
            if (current == wnd_proc)
                SetWindowLongA(g_window, GWL_WNDPROC, reinterpret_cast<LONG>(g_originalWndProc));
        }

        g_originalWndProc = nullptr;
        g_window = nullptr;
        g_cursorWasHidden = false;
        g_inputOutsideClick = false;
        g_scoreboardOutsideClick = false;
        g_installed = false;
    }

    bool installed()
    {
        return g_installed;
    }
}
