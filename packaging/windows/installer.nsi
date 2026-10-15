; Builds KDiskMark-<version>-Setup.exe from an already deployed folder (windeployqt output).
;   makensis /DVERSION=3.3.0 /DSOURCE_DIR=C:\path\to\dist /DOUTFILE=C:\path\KDiskMark-Setup.exe installer.nsi
Unicode True

!define APP_NAME "KDiskMark"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

!ifndef VERSION
    !define VERSION "0.0.0"
!endif
!ifndef OUTFILE
    !define OUTFILE "KDiskMark-${VERSION}-Setup.exe"
!endif
!ifndef SOURCE_DIR
    !error "SOURCE_DIR must point at the deployed application folder"
!endif

Name "${APP_NAME} ${VERSION}"
OutFile "${OUTFILE}"
SetCompressor /SOLID lzma
RequestExecutionLevel admin

; The location is fixed on purpose: the uninstaller deletes the whole folder, so the user
; must not be able to point it at a folder that holds other things.
InstallDir "$PROGRAMFILES64\${APP_NAME}"

!include "MUI2.nsh"
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"
    File /r "${SOURCE_DIR}\*.*"

    WriteUninstaller "$INSTDIR\Uninstall.exe"

    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\kdiskmark.exe"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk" "$INSTDIR\Uninstall.exe"

    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\kdiskmark.exe"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
    ; Only delete a folder that really is an installation of this program
    IfFileExists "$INSTDIR\kdiskmark.exe" 0 +2
        RMDir /r "$INSTDIR"

    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk"
    RMDir "$SMPROGRAMS\${APP_NAME}"

    DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd
