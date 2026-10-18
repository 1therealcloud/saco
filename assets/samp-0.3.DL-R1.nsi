!include "MUI2.nsh"

!ifndef PAYLOAD_DIR
    !define PAYLOAD_DIR "payload"
!endif
!ifndef OUTPUT_FILE
    !define OUTPUT_FILE "sa-mp-0.3.DL-R1-install.exe"
!endif

ManifestDPIAware true
ManifestDPIAwareness "PerMonitorV2,System"

; SA-MP 0.3.DL-R1 installer.
; Adapted from the reconstructed NSIS installer; payload files must be supplied separately.
; This is editable source, not the original author's .nsi file.

Name "San Andreas Multiplayer 0.3.DL-R1"
OutFile "${OUTPUT_FILE}"

; GTA:SA 1.0 US should already be installed in the selected directory.
InstallDir "$PROGRAMFILES\Rockstar Games\GTA San Andreas"
InstallDirRegKey HKLM "Software\Rockstar Games\GTA San Andreas\Installation" "ExePath"

; Required to register the samp:// URL protocol under HKCR.
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetOverwrite on
ShowInstDetails show
ShowUninstDetails show

; MUI finish-page artwork (164x314).
!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP "modern-wizard.bmp"
!define MUI_FINISHPAGE_TEXT "San Andreas Multiplayer 0.3.DL-R1 has been installed on your computer.$\r$\n$\r$\nClick Finish to close this wizard."

!insertmacro MUI_PAGE_LICENSE "${PAYLOAD_DIR}\samp-license.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; Validate that the target directory contains GTA San Andreas.
Function .onVerifyInstDir
    IfFileExists "$INSTDIR\gta_sa.exe" +2
    Abort
FunctionEnd

Section "-SA-MP"
    ; Also enforce this in silent installs, which skip the directory page.
    IfFileExists "$INSTDIR\gta_sa.exe" game_found
    MessageBox MB_OK|MB_ICONSTOP "Select the directory containing gta_sa.exe."
    Abort

game_found:
    SetOutPath "$INSTDIR"
    File "${PAYLOAD_DIR}\samp.exe"
    File "${PAYLOAD_DIR}\samp.dll"
    File "${PAYLOAD_DIR}\bass.dll"
    File "${PAYLOAD_DIR}\samp.saa"
    File "${PAYLOAD_DIR}\rcon.exe"
    File "${PAYLOAD_DIR}\samp_debug.exe"
    File "${PAYLOAD_DIR}\sampgui.png"
    File "${PAYLOAD_DIR}\mouse.png"
    File "${PAYLOAD_DIR}\gtaweap3.ttf"
    File "${PAYLOAD_DIR}\sampaux3.ttf"
    File "${PAYLOAD_DIR}\samp-license.txt"

    ; Clean up files used by older installations of SA-MP.
    Delete "$INSTDIR\models\samp.img"
    Delete "$INSTDIR\models\sampcol.img"
    Delete "$INSTDIR\models\samp.ide"
    Delete "$INSTDIR\models\samp.ipl"

    SetOutPath "$INSTDIR\SAMP"
    File "${PAYLOAD_DIR}\SAMP\SAMP.img"
    File "${PAYLOAD_DIR}\SAMP\SAMPCOL.img"
    File "${PAYLOAD_DIR}\SAMP\SAMP.ide"
    File "${PAYLOAD_DIR}\SAMP\SAMP.ipl"
    File "${PAYLOAD_DIR}\SAMP\samaps.txd"
    File "${PAYLOAD_DIR}\SAMP\blanktex.txd"
    File "${PAYLOAD_DIR}\SAMP\custom.img"
    File "${PAYLOAD_DIR}\SAMP\CUSTOM.ide"

    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\SAMPUninstall.exe"

    CreateDirectory "$SMPROGRAMS\San Andreas Multiplayer"
    CreateShortCut "$SMPROGRAMS\San Andreas Multiplayer\San Andreas Multiplayer.lnk" "$INSTDIR\samp.exe"
    CreateShortCut "$SMPROGRAMS\San Andreas Multiplayer\Uninstall.lnk" "$INSTDIR\SAMPUninstall.exe"

    ; Register the SA-MP protocol and GTA executable location.
    WriteRegStr HKCR "samp" "" "San Andreas Multiplayer"
    WriteRegStr HKCR "samp" "Url Protocol" ""
    WriteRegStr HKCR "samp\shell\open\command" "" '$\"$INSTDIR\samp.exe$\" $\"%1$\"'
    WriteRegStr HKCU "Software\SAMP" "gta_sa_exe" "$INSTDIR\gta_sa.exe"

    CreateDirectory "$DOCUMENTS\GTA San Andreas User Files"
    CreateDirectory "$DOCUMENTS\GTA San Andreas User Files\SAMP"
    IfFileExists "$INSTDIR\userdata.dat" 0 userdata_done
    CopyFiles /SILENT "$INSTDIR\userdata.dat" "$DOCUMENTS\GTA San Andreas User Files\SAMP"
    Delete "$INSTDIR\userdata.dat"
userdata_done:
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\samp.exe"
    Delete "$INSTDIR\samp.dll"
    Delete "$INSTDIR\bass.dll"
    Delete "$INSTDIR\samp.saa"
    Delete "$INSTDIR\rcon.exe"
    Delete "$INSTDIR\samp_debug.exe"
    Delete "$INSTDIR\SAMPUninstall.exe"
    Delete "$INSTDIR\sampgui.png"
    Delete "$INSTDIR\mouse.png"
    Delete "$INSTDIR\gtaweap3.ttf"
    Delete "$INSTDIR\sampaux3.ttf"
    Delete "$INSTDIR\samp-license.txt"

    ; Recursively remove the SAMP subdirectory.
    RMDir /r "$INSTDIR\SAMP"

    Delete "$SMPROGRAMS\San Andreas Multiplayer\San Andreas Multiplayer.lnk"
    Delete "$SMPROGRAMS\San Andreas Multiplayer\Uninstall.lnk"
    RMDir /r "$SMPROGRAMS\San Andreas Multiplayer"

    ; Registry values and user documents are kept.
SectionEnd
