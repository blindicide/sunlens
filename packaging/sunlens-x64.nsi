Unicode true
!include "LogicLib.nsh"
!include "x64.nsh"

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Sunlens"

Name "SUNLENS ${SUNLENS_VERSION}"
OutFile "${OUTPUT}"
InstallDir "$PROGRAMFILES64\Sunlens"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Function .onInit
    ${IfNot} ${IsNativeAMD64}
        Abort "This installer requires Windows x64."
    ${EndIf}
    ; Keep both setup and its uninstaller within a fixed, protected location.
    StrCpy $INSTDIR "$PROGRAMFILES64\Sunlens"
    SetRegView 64
    SetShellVarContext all
FunctionEnd

Section "SUNLENS"
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=vc_redist.x64.exe "${VCREDIST}"
    File "${__FILEDIR__}\install-core.ps1"
    StrCpy $1 "Could not start the Microsoft Visual C++ Redistributable."
    ClearErrors
    ExecWait '"$PLUGINSDIR\vc_redist.x64.exe" /install /quiet /norestart' $0
    IfErrors setupFailed
    ${If} $0 == 3010
        SetRebootFlag true
    ${ElseIf} $0 != 0
    ${AndIf} $0 != 1638 ; a newer runtime is already installed
    ${AndIf} $0 != -2147023258 ; the same error wrapped as HRESULT 0x80070666
        StrCpy $1 "Microsoft Visual C++ Redistributable returned $0."
        Goto setupFailed
    ${EndIf}

    DetailPrint "Downloading and verifying sing-box and Wintun (internet required)"
    nsExec::ExecToStack /TIMEOUT=300000 '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -File "$PLUGINSDIR\install-core.ps1" -Destination "$PLUGINSDIR\core"'
    Pop $0
    Pop $1
    ${If} $0 != "0"
        StrCpy $1 "Could not download verified networking components ($0): $1"
        Goto setupFailed
    ${EndIf}

    SetOutPath "$INSTDIR\app"
    File /r /x cores "${STAGE}\*"
    SetOutPath "$INSTDIR\app\cores\sing-box"
    StrCpy $1 "Could not install the downloaded networking components."
    ClearErrors
    CopyFiles /SILENT "$PLUGINSDIR\core\*.*" "$OUTDIR"
    IfErrors setupFailed
    SetOutPath "$INSTDIR\app"
    WriteUninstaller "$INSTDIR\uninstall.exe"
    CreateShortCut "$SMPROGRAMS\SUNLENS.lnk" "$INSTDIR\app\sunlens.exe"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "SUNLENS"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${SUNLENS_VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
    Goto done
    setupFailed:
        ; Fixed protected path so silent setup failures can be diagnosed too.
        CreateDirectory "$INSTDIR"
        FileOpen $2 "$INSTDIR\setup-error.log" w
        FileWrite $2 "$1$\r$\n"
        FileClose $2
        SetErrorLevel 1
        Abort "$1"
    done:
        Delete "$INSTDIR\setup-error.log"
SectionEnd

Function un.onInit
    ${If} $INSTDIR != "$PROGRAMFILES64\Sunlens"
        Abort "Unexpected SUNLENS install directory."
    ${EndIf}
    SetRegView 64
    SetShellVarContext all
FunctionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\SUNLENS.lnk"
    DeleteRegKey HKLM "${UNINSTALL_KEY}"
    RMDir /r "$INSTDIR\app"
    Delete "$INSTDIR\setup-error.log"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
