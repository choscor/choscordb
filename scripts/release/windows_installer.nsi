!include "FileFunc.nsh"
!include "LogicLib.nsh"

!ifndef PAYLOAD
  !error "PAYLOAD must point to the verified Windows stage"
!endif
!ifndef OUTPUT
  !error "OUTPUT must point to the versioned installer"
!endif
!ifndef WAIT_SCRIPT
  !error "WAIT_SCRIPT must point to the startup verifier"
!endif

Name "ChoscorDB"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\ChoscorDB"
RequestExecutionLevel user
SetCompressor /FINAL lzma
CRCCheck force
ShowInstDetails show

Var WaitPid
Var NoRestart
Var PendingDir
Var OwnPending
Var OldRestored
Var FailureHandled
Var MayRelaunchOld
Var HadPreviousInstall
Var WroteRegistration
Var WroteShortcut

Function .onInit
  SetShellVarContext current
  StrCpy $INSTDIR "$LOCALAPPDATA\Programs\ChoscorDB"
  StrCpy $WaitPid ""
  StrCpy $NoRestart ""
  StrCpy $OwnPending "0"
  StrCpy $OldRestored "0"
  StrCpy $FailureHandled "0"
  StrCpy $MayRelaunchOld "0"
  StrCpy $WroteRegistration "0"
  StrCpy $WroteShortcut "0"
  IfFileExists "$INSTDIR\choscordb.exe" 0 +2
    StrCpy $OldRestored "1"
  StrCpy $HadPreviousInstall $OldRestored
  System::Call 'kernel32::GetCurrentProcessId() i .r0'
  StrCpy $PendingDir "$INSTDIR.pending.$0"
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/WAITPID=" $WaitPid
  ClearErrors
  ${GetOptions} $R0 "/NORESTART" $NoRestart
  ${IfNot} ${Errors}
    StrCpy $NoRestart "1"
  ${EndIf}
  ${If} $WaitPid != ""
    IntCmp $WaitPid 1 bad_pid valid_pid valid_pid
    bad_pid:
      MessageBox MB_ICONSTOP "The updater process identifier is invalid."
      Abort
    valid_pid:
  ${EndIf}
FunctionEnd

Function .onInstFailed
  Call FailInstall
FunctionEnd

Function FailInstall
  ${If} $FailureHandled == "1"
    Return
  ${EndIf}
  StrCpy $FailureHandled "1"
  ${If} $OwnPending == "1"
    SetOutPath "$TEMP"
    RMDir /r "$PendingDir"
  ${EndIf}
  CreateDirectory "$LOCALAPPDATA\ChoscorDB"
  FileOpen $R0 "$LOCALAPPDATA\ChoscorDB\update-install-failure.txt" w
  FileWrite $R0 "ChoscorDB update installation failed. Retry or install manually.$\r$\n"
  FileClose $R0
  ; Never launch the new executable when rollback could not be confirmed.
  ${If} $OldRestored == "1"
    ${If} $MayRelaunchOld == "1"
      IfFileExists "$INSTDIR\choscordb.exe" 0 done_failure
        Exec '"$INSTDIR\choscordb.exe"'
    ${EndIf}
  ${EndIf}
  done_failure:
FunctionEnd

Function RestorePrevious
  StrCpy $OldRestored "0"
  SetOutPath "$TEMP"
  RMDir /r "$INSTDIR"
  IfFileExists "$INSTDIR" rollback_failed restore_backup
  restore_backup:
    IfFileExists "$INSTDIR.previous\choscordb.exe" 0 no_backup
    ClearErrors
    Rename "$INSTDIR.previous" "$INSTDIR"
    IfErrors rollback_failed
    IfFileExists "$INSTDIR\choscordb.exe" 0 rollback_failed
    StrCpy $OldRestored "1"
    Return
  no_backup:
    Return
  rollback_failed:
    ; Keep .previous and the failed payload for manual recovery.
    Return
FunctionEnd

Function CleanupOwnedFirstInstall
  ${If} $HadPreviousInstall == "0"
    ${If} $WroteShortcut == "1"
      Delete "$SMPROGRAMS\ChoscorDB.lnk"
    ${EndIf}
    ${If} $WroteRegistration == "1"
      DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ChoscorDB"
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "Install"
  IfFileExists "$PendingDir" pending_conflict previous_check
  pending_conflict:
    StrCpy $OldRestored "0"
    MessageBox MB_ICONSTOP "An incomplete ChoscorDB staging directory needs manual review."
    Call FailInstall
    Abort
  previous_check:
  IfFileExists "$INSTDIR.previous" previous_conflict registration_check
  previous_conflict:
    ; Only a verified prior success may safely discard a leftover backup.
    IfFileExists "$INSTDIR\choscordb.exe" 0 manual_previous
    ClearErrors
    FileOpen $R1 "$LOCALAPPDATA\ChoscorDB\update-cleanup-pending.txt" r
    IfErrors manual_previous
    FileRead $R1 $R2
    FileClose $R1
    StrCmp $R2 "ready-upgrade-backup" 0 manual_previous
    RMDir /r "$INSTDIR.previous"
    IfFileExists "$INSTDIR.previous" manual_previous cleanup_recovered
  cleanup_recovered:
    Delete "$LOCALAPPDATA\ChoscorDB\update-cleanup-pending.txt"
    Delete "$LOCALAPPDATA\ChoscorDB\update-install-failure.txt"
    Goto registration_check
  manual_previous:
    StrCpy $OldRestored "0"
    MessageBox MB_ICONSTOP "A previous ChoscorDB backup needs manual review."
    Call FailInstall
    Abort
  registration_check:
  ${If} $HadPreviousInstall == "0"
    StrCpy $R2 "0"
  find_registration:
    ClearErrors
    EnumRegKey $R1 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall" $R2
    IfErrors registration_failed
    StrCmp $R1 "" registration_absent
    StrCmp $R1 "ChoscorDB" unknown_registration
    IntOp $R2 $R2 + 1
    Goto find_registration
  registration_failed:
    MessageBox MB_ICONSTOP "ChoscorDB installation registration could not be checked."
    Call FailInstall
    Abort
  registration_absent:
    IfFileExists "$SMPROGRAMS\ChoscorDB.lnk" unknown_registration no_restart_check
  ${EndIf}
  Goto no_restart_check
  unknown_registration:
    MessageBox MB_ICONSTOP "ChoscorDB registration exists without an installation; review it manually."
    Call FailInstall
    Abort
  no_restart_check:
  ${If} $NoRestart == "1"
    ${If} $OldRestored == "1"
      MessageBox MB_ICONSTOP "An upgrade must restart ChoscorDB to verify it."
      Call FailInstall
      Abort
    ${EndIf}
  ${EndIf}
  ; This unique staging path was absent at entry and belongs to this invocation.
  StrCpy $OwnPending "1"
  ClearErrors
  SetOutPath "$PendingDir"
  IfErrors incomplete_payload
  ClearErrors
  File /r "${PAYLOAD}\*"
  IfErrors incomplete_payload
  IfFileExists "$PendingDir\choscordb.exe" wait_for_close incomplete_payload
  incomplete_payload:
    MessageBox MB_ICONSTOP "The installer payload is incomplete."
    Call FailInstall
    Abort
  wait_for_close:
  ${If} $WaitPid != ""
    System::Call 'kernel32::OpenProcess(i 0x100000, i 0, i $WaitPid) p .r1 ?e'
    Pop $3
    ${If} $1 != 0
      System::Call 'kernel32::WaitForSingleObject(p r1, i 300000) i .r2'
      System::Call 'kernel32::CloseHandle(p r1)'
      ${If} $2 != 0
        MessageBox MB_ICONSTOP "ChoscorDB did not close in time. Retry the update."
        Call FailInstall
        Abort
      ${EndIf}
    ${Else}
      ${If} $3 != 87
        MessageBox MB_ICONSTOP "ChoscorDB process state could not be checked."
        Call FailInstall
        Abort
      ${EndIf}
    ${EndIf}
    StrCpy $MayRelaunchOld "1"
  ${EndIf}
  IfFileExists "$INSTDIR" existing_path new_install
  existing_path:
    IfFileExists "$INSTDIR\choscordb.exe" move_old occupied_path
  occupied_path:
    MessageBox MB_ICONSTOP "The installation location contains unknown files."
    Call FailInstall
    Abort
  move_old:
    ClearErrors
    Rename "$INSTDIR" "$INSTDIR.previous"
    IfErrors cannot_move_old
    StrCpy $OldRestored "0"
    Goto new_install
  cannot_move_old:
    MessageBox MB_ICONSTOP "The current installation could not be replaced."
    Call FailInstall
    Abort
  new_install:
    ClearErrors
    Rename "$PendingDir" "$INSTDIR"
    IfErrors failed_install
    StrCpy $OwnPending "0"
    IfFileExists "$INSTDIR\choscordb.exe" 0 failed_install
    ClearErrors
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    IfErrors failed_install
    IfFileExists "$INSTDIR\Uninstall.exe" 0 failed_install
    StrCpy $WroteRegistration "1"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ChoscorDB" "DisplayName" "ChoscorDB"
    IfErrors failed_install
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ChoscorDB" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    IfErrors failed_install
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ChoscorDB" "InstallLocation" "$INSTDIR"
    IfErrors failed_install
    StrCpy $WroteShortcut "1"
    CreateShortCut "$SMPROGRAMS\ChoscorDB.lnk" "$INSTDIR\choscordb.exe"
    IfErrors failed_install
    ${If} $NoRestart == ""
      InitPluginsDir
      IfErrors failed_install
      ClearErrors
      SetOutPath "$PLUGINSDIR"
      IfErrors failed_install
      ClearErrors
      File "/oname=windows_wait_for_start.ps1" "${WAIT_SCRIPT}"
      IfErrors failed_install
      StrCpy $R0 "99"
      ClearErrors
      ExecWait '"$SYSDIR\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\windows_wait_for_start.ps1"' $R0
      IfErrors failed_install
      ${If} $R0 != 0
        Goto failed_install
      ${EndIf}
    ${EndIf}
    RMDir /r "$INSTDIR.previous"
    IfFileExists "$INSTDIR.previous" cleanup_pending clean_success
  cleanup_pending:
    ; The new app is running. Keep its verified backup and report nonzero status.
    CreateDirectory "$LOCALAPPDATA\ChoscorDB"
    FileOpen $R1 "$LOCALAPPDATA\ChoscorDB\update-cleanup-pending.txt" w
    FileWrite $R1 "ready-upgrade-backup"
    FileClose $R1
    FileOpen $R1 "$LOCALAPPDATA\ChoscorDB\update-install-failure.txt" w
    FileWrite $R1 "ChoscorDB updated, but old backup cleanup is pending. Retry the installer or review manually.$\r$\n"
    FileClose $R1
    MessageBox MB_ICONEXCLAMATION "ChoscorDB updated, but old backup cleanup is pending."
    SetErrorLevel 2
    Goto done_install
  clean_success:
    Delete "$LOCALAPPDATA\ChoscorDB\update-cleanup-pending.txt"
    Delete "$LOCALAPPDATA\ChoscorDB\update-install-failure.txt"
    Goto done_install
  failed_install:
    Call RestorePrevious
    Call CleanupOwnedFirstInstall
    MessageBox MB_ICONSTOP "ChoscorDB could not be installed and started. Retry or install manually."
    Call FailInstall
    Abort
  done_install:
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\ChoscorDB.lnk"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\ChoscorDB"
  RMDir /r "$INSTDIR"
SectionEnd
