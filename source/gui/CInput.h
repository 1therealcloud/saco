#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>

namespace cgui
{
    class Dialog;
    class EditBox;
}

using CMDPROC = void(__cdecl*)(const char*);

class CInput
{
public:
    enum
    {
        MAX_CLIENT_CMDS = 144,
        MAX_CMD_LENGTH = 32,
        MAX_INPUT_LENGTH = 128,
        RECALL_COUNT = 10
    };

public:
    // SA-MP 0.3.7-R5 layout.
    IDirect3DDevice9* m_pDevice;
    cgui::Dialog* m_pGameUI;
    cgui::EditBox* m_pEditbox;
    CMDPROC m_commandProc[MAX_CLIENT_CMDS];
    char m_szCommandName[MAX_CLIENT_CMDS][MAX_CMD_LENGTH + 1];
    int m_nCommandCount;
    BOOL m_bEnabled;
    char m_szInput[MAX_INPUT_LENGTH + 1];
    char m_szRecallBuffer[RECALL_COUNT][MAX_INPUT_LENGTH + 1];
    char m_szCurrentBuffer[MAX_INPUT_LENGTH + 1];
    int m_nCurrentRecall;
    int m_nTotalRecall;
    CMDPROC m_pDefaultCommand;

public:
    explicit CInput(IDirect3DDevice9* device);

    void GetRect(RECT* rect);
    void Open();
    void Close();
    void AddRecall(const char* text);
    void RecallUp();
    void RecallDown();
    void EnableCursor();
    CMDPROC GetCommandHandler(const char* name);
    void SetDefaultCommand(CMDPROC handler);
    void AddCommand(const char* name, CMDPROC handler);
    BOOL MsgProc(UINT message, WPARAM wParam, LPARAM lParam);
    void ResetDialogControls(cgui::Dialog* gameUI);
    void UpdateLayout();
    void ProcessInput();

    // replacement helpers; not part of the original R5 ABI.
    void SetText(const char* text);
    const char* GetText() const;
    bool IsEnabled() const;

private:
    static void Copy128(char* destination, const char* source);
};
