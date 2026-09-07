Unicode true
RequestExecutionLevel user
SetCompressor zlib
SetCompressorDictSize 32

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "Prerequisites.nsh"

!ifndef GraphVisStage
!define GraphVisStage "..\..\build\stage"
!endif
!ifndef GraphVisOutputDir
!define GraphVisOutputDir "..\..\dist\end-user"
!endif

!define PRODUCT_NAME "GraphVis"
!define PRODUCT_VERSION "18.4.0"
!define PRODUCT_KEY "Software\GraphVis\18.4"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\GraphVis18.4"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "${GraphVisOutputDir}\Install.exe"
InstallDir "$LOCALAPPDATA\Programs\GraphVis 18.4"
InstallDirRegKey HKCU "${PRODUCT_KEY}" "InstallDir"
ShowInstDetails show
ShowUninstDetails show
BrandingText "GraphVis 18.4 — simple offline installer"

!define MUI_ABORTWARNING
!define MUI_ICON "..\..\assets\branding\graphvis.ico"
!define MUI_UNICON "..\..\assets\branding\graphvis.ico"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var RepairMode

Function .onInit
  StrCpy $RepairMode "0"
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/REPAIR" $R1
  ${IfNot} ${Errors}
    StrCpy $RepairMode "1"
  ${EndIf}
FunctionEnd

Section "GraphVis" SEC_MAIN
  Call CheckPrerequisites
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /r "${GraphVisStage}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "${PRODUCT_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${PRODUCT_KEY}" "Version" "${PRODUCT_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "GraphVis ${PRODUCT_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "GraphVis"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\graphvis.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1

  CreateDirectory "$SMPROGRAMS\GraphVis 18.4"
  CreateShortcut "$SMPROGRAMS\GraphVis 18.4\GraphVis.lnk" "$INSTDIR\graphvis.exe"
  CreateShortcut "$DESKTOP\GraphVis.lnk" "$INSTDIR\graphvis.exe"

  ${If} $RepairMode == "1"
    DetailPrint "Repair mode: restoring payload, dependencies, environment and setup state..."
    ExecWait '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\internal\Runtime-Maintenance.ps1" -Mode Repair -InstallDir "$INSTDIR"' $0
  ${Else}
    ExecWait '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\internal\Runtime-Maintenance.ps1" -Mode Install -InstallDir "$INSTDIR"' $0
  ${EndIf}
  ${If} $0 != 0
    MessageBox MB_ICONSTOP|MB_OK "GraphVis files were copied, but runtime verification failed.$\r$\n$\r$\nThe setup-complete marker was NOT written.$\r$\nOpen Support\Repair.bat from the extracted GraphVis release folder and review the maintenance log under LocalAppData\GraphVis\18.4\logs."
    SetErrorLevel 1
    Abort
  ${EndIf}
  SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  ExecWait '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\internal\Runtime-Maintenance.ps1" -Mode Uninstall -InstallDir "$INSTDIR"' $0
  SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000

  Delete "$DESKTOP\GraphVis.lnk"
  RMDir /r "$SMPROGRAMS\GraphVis 18.4"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${PRODUCT_KEY}"
  DeleteRegKey HKCU "Software\GraphVis\GraphVis 18.4"
  Delete "$TEMP\graphvis18-query.arrow"
  Delete "$TMP\graphvis18-query.arrow"
  RMDir /r "$LOCALAPPDATA\GraphVis"
  RMDir /r "$APPDATA\GraphVis"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR"
SectionEnd
