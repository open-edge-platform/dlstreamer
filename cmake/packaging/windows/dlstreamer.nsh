# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

!ifndef DLSTREAMER_NSH
!define DLSTREAMER_NSH

!include 'LogicLib.nsh'
!include 'Sections.nsh'
!include 'FileFunc.nsh'
!include 'WordFunc.nsh'
!include 'WinVer.nsh'
!include 'x64.nsh'

; Define registry keys
!define UNINSTALL_REGISTRY_KEY 'Software\Microsoft\Windows\CurrentVersion\Uninstall\dlstreamer'
!define DLSTREAMER_REGISTRY_KEY 'Software\Intel\dlstreamer'
InstallDirRegKey HKLM '${DLSTREAMER_REGISTRY_KEY}' 'InstallDir'

; Remember installer language
!define MUI_LANGDLL_REGISTRY_ROOT 'HKLM'
!define MUI_LANGDLL_REGISTRY_KEY '${UNINSTALL_REGISTRY_KEY}'
!define MUI_LANGDLL_REGISTRY_VALUENAME 'InstallerLanguage'

; Installer UI settings
; DPI awareness
ManifestDPIAware System
ManifestDPIAwareness 'PerMonitorV2,System'
; Set installer font
SetFont 'Segoe UI' 8
; Do not automatically jump to the finish page, to allow the user to check the install log
!define MUI_FINISHPAGE_NOAUTOCLOSE
; Use small description in Components page
!define MUI_COMPONENTSPAGE_SMALLDESC
; Finish page checkbox to documentation
!define MUI_FINISHPAGE_SHOWREADME 'https://docs.openedgeplatform.intel.com/${VERSION_MAJOR}.${VERSION_MINOR}/edge-ai-libraries/dlstreamer/index.html'
!define MUI_FINISHPAGE_SHOWREADME_TEXT 'Show Documentation'
; Finish page link to Intel Driver & Support Assistant
!define MUI_FINISHPAGE_LINK 'Launch Intel® Driver && Support Assistant'
!define MUI_FINISHPAGE_LINK_LOCATION 'https://www.intel.com/content/www/us/en/support/detect.html'

; Reserve plugins
!insertmacro MUI_RESERVEFILE_LANGDLL

# ============================================================================
# Init functions
# ============================================================================

Function CheckInstallerAlreadyRunning
  ; Create a mutex to check if another instance of the installer is running
  System::Call 'kernel32::CreateMutex(i 0, i 0, t "${PACKAGE_FILE_NAME}") i .r1 ?e'
  Pop $R0
  ${If} $R0 != 0
    MessageBox MB_OK|MB_ICONEXCLAMATION 'The installer is already running.' /SD IDYES
    Abort
  ${EndIf}
FunctionEnd

Function CheckOSVersion
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONEXCLAMATION 'The installer requires a 64-bit version of Windows.' /SD IDYES
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin11}
    MessageBox MB_OK|MB_ICONEXCLAMATION 'The installer requires Windows 11 or later.' /SD IDYES
    Abort
  ${EndIf}
FunctionEnd

!macro _SilentSetComponent KEYWORD SECTION
  ClearErrors
  ${WordFind} $R1 ',${KEYWORD},' 'E+1' $R2
  ${If} ${Errors}
    !insertmacro UnselectSection ${${SECTION}}
  ${Else}
    !insertmacro SelectSection ${${SECTION}}
  ${EndIf}
!macroend

!macro SilentComponentSelection
  ${If} ${Silent}
    ${GetParameters} $R0

    ; Handle /TYPE= parameter
    ClearErrors
    ${GetOptions} $R0 '/TYPE=' $R1
    ${IfNot} ${Errors}
      ${If} $R1 == 'Full'
        SetCurInstType 0
      ${ElseIf} $R1 == 'Typical'
        SetCurInstType 1
      ${ElseIf} $R1 == 'Minimal'
        SetCurInstType 2
      ${EndIf}
    ${EndIf}

    ; Handle /COMPONENTS= parameter (overrides /TYPE= if both specified)
    ClearErrors
    ${GetOptions} $R0 '/COMPONENTS=' $R1
    ${IfNot} ${Errors}
      ; Wrap in commas for exact keyword matching
      StrCpy $R1 ',$R1,'

      !insertmacro _SilentSetComponent 'python' 'c02_python'
      !insertmacro _SilentSetComponent 'env' 'c03_env'
      !insertmacro _SilentSetComponent 'samples' 'c04_samples'
      !insertmacro _SilentSetComponent 'development' 'c05_development'
    ${EndIf}
  ${EndIf}
!macroend

!macro OnInit
  SetRegView 64
  SetShellVarContext all

  Call CheckOSVersion
  Call CheckInstallerAlreadyRunning

  ; Reads components status from registry
  !insertmacro SectionList 'InitSection'
  ; Set component selection based on installer command parameters
  !insertmacro SilentComponentSelection

  ; Show language selection dialog
  !insertmacro MUI_LANGDLL_DISPLAY
!macroend

!define DLSTREAMER_INSTALLER_ONINIT '!insertmacro OnInit'

!macro UnOnInit
  SetRegView 64
  SetShellVarContext all

  ; Read the selected language
  !insertmacro MUI_UNGETLANGUAGE
!macroend

!define DLSTREAMER_UNINSTALLER_ONINIT '!insertmacro UnOnInit'

# ============================================================================
# Components Page functions
# ============================================================================

Function ShowHiDpiComponentCheckboxes
  SysCompImg::SetThemed
FunctionEnd

!define CUSTOMFUNCTION_COMPONENTS_SHOW ShowHiDpiComponentCheckboxes

Function CheckGStreamerInstallation
  ReadRegStr $R1 HKLM 'SOFTWARE\GStreamer1.0\x86_64' 'Version'
  ${IfThen} $R1 == '' ${|} Return ${|}

  ; Check for conflicting architectures
  StrCpy $R2 ''

  ReadEnvStr $R3 'GSTREAMER_1_0_ROOT_MSVC_X86'
  ${If} $R3 != ''
    StrCpy $R2 'msvc_x86'
  ${EndIf}

  ReadEnvStr $R3 'GSTREAMER_1_0_ROOT_MINGW_X86_64'
  ${If} $R3 != ''
    ${If} $R2 == ''
      StrCpy $R2 'mingw_x86_64'
    ${Else}
      StrCpy $R2 '$R2, mingw_x86_64'
    ${EndIf}
  ${EndIf}

  ReadEnvStr $R3 'GSTREAMER_1_0_ROOT_MINGW_X86'
  ${If} $R3 != ''
    ${If} $R2 == ''
      StrCpy $R2 'mingw_x86'
    ${Else}
      StrCpy $R2 '$R2, mingw_x86'
    ${EndIf}
  ${EndIf}

  ${If} $R2 != ''
    MessageBox MB_OK|MB_ICONEXCLAMATION 'Conflicting GStreamer architectures detected: $R2$\r$\n$\r$\nOnly msvc_x86_64 is supported.$\r$\n$\r$\nPlease uninstall the conflicting architectures before proceeding.' /SD IDYES
    Abort
  ${EndIf}
FunctionEnd

!define CUSTOMFUNCTION_COMPONENTS_LEAVE CheckGStreamerInstallation

# ============================================================================
# Cleanup functions
# ============================================================================

Function UninstallGStreamerMSI
  ; $R0 is UpgradeCode
  Pop $R0
  StrCpy $R1 0
  loop:
  System::Call 'msi::MsiEnumRelatedProducts(t "$R0", i0, i r11, t .r12) i .r13'
  ${If} $R3 = 0
    # Now $R2 contains the product code
    DetailPrint 'Execute GStreamer uninstaller with product code: $R2'
    nsExec::ExecToLog '"msiexec.exe" /x $R2 /qb /quiet /norestart'
    Pop $R4
    ${If} $R4 != 0
      DetailPrint 'Failed to uninstall GStreamer. Error code: $R4'
    ${EndIf}
    IntOp $R1 $R1 + 1
    Goto loop
  ${EndIf}
FunctionEnd

!macro UninstallGStreamerInnoFunc un
Function ${un}UninstallGStreamerInno
  ; Check for Inno Setup uninstall registry entry (GStreamer 1.28+)
  ReadRegStr $R0 HKLM 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\c20a66dc-b249-4e6d-a68a-d0f836b2b3cf_is1' 'UninstallString'
  ${If} $R0 == ''
    DetailPrint 'GStreamer uninstall entry not found'
    Return
  ${EndIf}

  DetailPrint 'Execute GStreamer uninstaller: $R0'
  nsExec::ExecToLog '$R0 /VERYSILENT'
  Pop $R1
  ${If} $R1 != 0
    DetailPrint 'Failed to uninstall GStreamer. Error code: $R1'
  ${EndIf}
FunctionEnd
!macroend

!insertmacro UninstallGStreamerInnoFunc ''
!insertmacro UninstallGStreamerInnoFunc 'un.'

Function LegacyCleanUp
  ; Uninstall previous version if present
  ReadRegStr $R0 HKLM '${UNINSTALL_REGISTRY_KEY}' 'UninstallString'
  ${If} $R0 != ''
    DetailPrint 'Execute uninstaller of previous version: $R0'
    ClearErrors
    nsExec::ExecToLog '$R0 /S'
    Pop $R1
    ${If} $R1 != 0
      DetailPrint 'Failed to uninstall previous version. Error code: $R1'
    ${EndIf}
  ${EndIf}

  DetailPrint 'Check for legacy installations'

  ; Read LIBVA_DRIVERS_PATH value to delete from PATH
  ReadRegStr $R2 HKCU 'Environment' 'LIBVA_DRIVERS_PATH'

  ; Delete legacy environment variables
  DeleteRegValue HKCU 'Environment' 'LIBVA_DRIVER_NAME'
  DeleteRegValue HKCU 'Environment' 'LIBVA_DRIVERS_PATH'
  DeleteRegValue HKCU 'Environment' 'GST_PLUGIN_SCANNER'
  DeleteRegValue HKCU 'Environment' 'OpenVINO_DIR'
  DeleteRegValue HKCU 'Environment' 'OpenVINOGenAI_DIR'
  DeleteRegValue HKCU 'Environment' 'OPENVINO_LIB_PATHS'

  ; Delete legacy PATH entries
  EnVar::SetHKCU
  EnVar::DeleteValue 'Path' 'C:\gstreamer\1.0\msvc_x86_64\bin'
  Pop $0
  EnVar::DeleteValue 'Path' 'C:\openvino\runtime\3rdparty\tbb\bin'
  Pop $0
  EnVar::DeleteValue 'Path' 'C:\openvino\runtime\bin\intel64\Release'
  Pop $0

  ; Delete LIBVA_DRIVERS_PATH value from PATH
  ${If} $R2 != ''
    EnVar::DeleteValue 'Path' $R2
    Pop $0
  ${EndIf}

  ${If} ${FileExists} 'C:\dlstreamer_dlls'
    DetailPrint 'Delete legacy DL Streamer DLLs'
    RMDir /r 'C:\dlstreamer_dlls'
  ${EndIf}

  ${If} ${FileExists} 'C:\openvino'
    DetailPrint 'Delete legacy OpenVINO'
    RMDir /r 'C:\openvino'
  ${EndIf}

  ; Clean temporary folders
  RMDir /r 'C:\dlstreamer_tmp'

  ; Clean GStreamer cache
  SetShellVarContext current
  RMDir /r '$LOCALAPPDATA\Microsoft\Windows\INetCache\gstreamer-1.0'
  SetShellVarContext all

  ; Uninstall GStreamer MSI (GStreamer < 1.28)
  Push '{C20A66DC-B249-4E6D-A68A-D0F836B2B3CF}'  ; GStreamer runtime
  Call UninstallGStreamerMSI
  Push '{49C4A3AA-249F-453C-B82E-ECD05FAC0693}'  ; GStreamer devel
  Call UninstallGStreamerMSI

  DetailPrint 'Legacy cleanup completed'
FunctionEnd

Section '-Cleanup'
  w7tbp::Start /NOUNLOAD
  Call LegacyCleanUp
SectionEnd

# ============================================================================
# Installation functions
# ============================================================================

Function VerifyFileHash
  ; Input stack: expected_hash, filepath
  ; Output stack: "OK" if hash matches, "FAIL" otherwise
  Pop $R5  ; filepath
  Pop $R6  ; expected hash

  ClearErrors
  Crypto::HashFile 'SHA2' '$R5'
  Pop $R7
  ${If} ${Errors}
    Push 'FAIL'
    Return
  ${EndIf}

  ${If} $R7 == $R6
    Push 'OK'
  ${Else}
    DetailPrint 'Integrity check failed for $R5! Expected: $R6, Got: $R7'
    MessageBox MB_OK|MB_ICONEXCLAMATION 'Integrity check failed for $R5! Expected: $R6, Got: $R7' /SD IDYES
    Push 'FAIL'
  ${EndIf}
FunctionEnd

!macro InstallGStreamer
  ReadRegStr $R1 HKLM 'SOFTWARE\GStreamer1.0\x86_64' 'Version'
  ReadRegStr $R2 HKLM 'SOFTWARE\GStreamer1.0\x86_64' 'InstallDir'

  DetailPrint 'Installed GStreamer version: $R1'
  ; If no version found, proceed with installation
  ${IfThen} $R1 == '' ${|} Goto InvokeGStreamerInstaller ${|}

  ; Result: 0=equal, 1=installed newer, 2=bundled newer
  ${VersionCompare} '$R1' '${GSTREAMER_VERSION}' $R4
  ${If} $R4 = 0
    ; Version matches, verify directory exists
    ${IfThen} $R2 == '' ${|} Goto InvokeGStreamerInstaller ${|}
    IfFileExists '$R2\*.*' SkipGStreamer InvokeGStreamerInstaller
  ${Else}
    ; Version mismatch, uninstall then install
    DetailPrint 'GStreamer version mismatch: installed $R1, bundled ${GSTREAMER_VERSION}'
    Call UninstallGStreamerInno
    Goto InvokeGStreamerInstaller
  ${EndIf}

  InvokeGStreamerInstaller:
  SetOutPath '$INSTDIR\deps'
  File '${INSTALLER_DEPS_DIR}\gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.exe'

  Push '${GSTREAMER_INSTALLER_HASH}'
  Push '$INSTDIR\deps\gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.exe'
  Call VerifyFileHash
  Pop $R5
  ${If} $R5 == 'FAIL'
    Goto SkipGStreamer
  ${EndIf}

  DetailPrint 'Execute GStreamer ${GSTREAMER_VERSION} installer'
  nsExec::ExecToLog '$INSTDIR\deps\gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.exe /VERYSILENT /LOG /TYPE=runtime /ALLUSERS'
  Pop $R3
  ${If} $R3 != 0
    DetailPrint 'GStreamer installation failed. Error code: $R3'
    MessageBox MB_OK|MB_ICONEXCLAMATION 'Failed to install GStreamer.' /SD IDYES
  ${EndIf}

  ; Result: 0=equal, 1=installed newer, 2=bundled newer
  ${VersionCompare} '1.28.2' '${GSTREAMER_VERSION}' $R4
  ${If} $R4 = 0
    ReadRegStr $R2 HKLM 'SOFTWARE\GStreamer1.0\x86_64' 'InstallDir'
    SetOutPath $R2\bin
    File "${INST_DIR}\c00_gstreamer\deps\windows\gstanalytics-1.0-0.dll"
  ${EndIf}

  SetOutPath '$INSTDIR'

  SkipGStreamer:
!macroend

Function SetupEnvironmentVariables
  DetailPrint 'Setup environment variables'
  nsExec::ExecToLog 'powershell.exe -ExecutionPolicy Bypass -File "$INSTDIR\scripts\setup_dls_env.ps1" -Persist'
  Pop $0
  ${If} $0 != 0
    DetailPrint 'Environment setup failed. Error code: $0'
  ${EndIf}
FunctionEnd

Function InstallFinalize
  ; Notify system of environment change
  SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 STR:Environment /TIMEOUT=5000

  ; Remove bundled deps
  RMDir /r '$INSTDIR\deps'

  ; Write info to registry
  WriteRegStr HKLM '${DLSTREAMER_REGISTRY_KEY}' 'Version' '${VERSION}'

  ; Write uninstall info to registry
  ; Install date
  ${GetTime} '' 'L' $0 $1 $2 $3 $4 $5 $6
  WriteRegStr HKLM '${UNINSTALL_REGISTRY_KEY}' 'InstallDate' '$2$1$0'
  ; Estimated size
  ${GetSize} '$INSTDIR' '/S=0K' $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM '${UNINSTALL_REGISTRY_KEY}' 'EstimatedSize' '$0'
FunctionEnd

!macro InstallExtras
  SetCompress off

  !insertmacro SectionFlagIsSet ${c00_gstreamer} ${SF_SELECTED} DoInstallGStreamer SkipInstallGStreamer
  DoInstallGStreamer:
  !insertmacro InstallGStreamer
  SkipInstallGStreamer:

  SetCompress auto

  !insertmacro SectionFlagIsSet ${c03_env} ${SF_SELECTED} DoSetupEnv SkipSetupEnv
  DoSetupEnv:
  Call SetupEnvironmentVariables
  SkipSetupEnv:

  Call InstallFinalize
!macroend

!define DLSTREAMER_EXTRA_INSTALL_COMMANDS '!insertmacro InstallExtras'

# ============================================================================
# Uninstallation functions
# ============================================================================

Var UninstGstCheckbox
Var UninstGst

Function un.ConfirmShow
  FindWindow $1 '#32770' '' $HWNDPARENT
  ; WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP = 0x50010003
  System::Call 'user32::CreateWindowExW(i 0, w "BUTTON", w "Uninstall GStreamer", i 0x50010003, i 10, i 280, i 240, i 40, p $1, p 0, p 0, p 0) p .s'
  Pop $UninstGstCheckbox
  SendMessage $1 ${WM_GETFONT} 0 0 $0
  SendMessage $UninstGstCheckbox ${WM_SETFONT} $0 1
FunctionEnd

Function un.ConfirmLeave
  SendMessage $UninstGstCheckbox ${BM_GETCHECK} 0 0 $UninstGst
FunctionEnd

!define CUSTOMFUNCTION_CONFIRM_SHOW un.ConfirmShow
!define CUSTOMFUNCTION_CONFIRM_LEAVE un.ConfirmLeave

Function un.DeleteEnvironmentVariables
  DetailPrint 'Delete environment variables'

  ; Delete environment variables
  DeleteRegValue HKCU 'Environment' 'DLSTREAMER_DIR'
  DeleteRegValue HKCU 'Environment' 'GST_PLUGIN_PATH'

  ; Delete from user PATH
  ReadRegStr $R2 HKLM 'SOFTWARE\GStreamer1.0\x86_64' 'InstallDir'
  EnVar::SetHKCU
  EnVar::DeleteValue 'Path' '$R2\bin'
  Pop $0
  EnVar::DeleteValue 'Path' '$INSTDIR\bin'
  Pop $0

  ; Notify system of environment change
  SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 STR:Environment /TIMEOUT=5000
FunctionEnd

!macro UninstallExtras
  Call un.DeleteEnvironmentVariables

  ${If} $UninstGst == ${BST_CHECKED}
    Call un.UninstallGStreamerInno
  ${EndIf}
!macroend

!define DLSTREAMER_EXTRA_UNINSTALL_COMMANDS '!insertmacro UninstallExtras'

!endif ; DLSTREAMER_NSH
