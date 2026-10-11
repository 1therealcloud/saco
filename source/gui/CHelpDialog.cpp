#include <stdafx.h>

#include "main.h"

#include "CHelpDialog.h"
#include "CDialog.h"

namespace
{
    // R5 opens this through CDialog::Show as dialog id 1 / message-box / client-side.
    // The wording is intentionally reconstructed rather than copied verbatim.
    constexpr const char* kHelpCaption = "SA-MP Keys:";
    constexpr const char* kHelpText = "{BB9999}F1\t{BBBBBB}Open help\n"
                                      "{BB9999}Tab\t{BBBBBB}Show scoreboard\n"
                                      "{BB9999}F4\t{BBBBBB}Change class after the next respawn\n"
                                      "{BB9999}F5\t{BBBBBB}Show network statistics\n"
                                      "{BB9999}F7\t{BBBBBB}Toggle chat\n"
                                      "{BB9999}F8\t{BBBBBB}Take screenshot\n"
                                      "{BB9999}F9\t{BBBBBB}Toggle death window\n"
                                      "{BB9999}T/F6\t{BBBBBB}Open chat input\n"
                                      "{BB9999}G\t{BBBBBB}Enter a vehicle as passenger\n";
}

CHelpDialog::CHelpDialog(IDirect3DDevice9* device) : m_pDevice(device)
{}

void CHelpDialog::Show()
{
    CDialog* dialog = g_dialog.get();
    if (!dialog || dialog->IsActive())
        return;

    dialog->Show(1, CDialog::DIALOG_MESSAGEBOX, kHelpCaption, kHelpText, "OK", "", FALSE);
}
