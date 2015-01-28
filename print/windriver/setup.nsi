!define REDMON_DLL "flexVDIprint_redmon.dll"
!define REDMON_NAME "flexVDI Redirection Port"
!define PORT_NAME "flexVDIprint"
!define PORT_CONFIG_KEY "SYSTEM\CurrentControlSet\Control\Print\Monitors\${REDMON_NAME}\Ports\${PORT_NAME}"
!define PDF_PRINTER_NAME "flexVDI PDF Printer"

!macro installPrintDriver
    SetOutPath "$INSTDIR\printdriver"
    File "@PROJECT_SOURCE_DIR@/print/windriver/flexvdips.*"
    File "@PROJECT_SOURCE_DIR@/print/windriver/gs9.15/gsdll32.dll"
    File "../win32/print/windriver/filter.exe"
    SetOutPath "$INSTDIR"
    File "print/windriver/redmon.dll" "../win32/print/windriver/setup.dll"
    Rename /REBOOTOK "$INSTDIR\redmon.dll" "$SYSDIR\${REDMON_DLL}"
    System::Call 'setup::installRedMon(w "${REDMON_NAME}", w "${REDMON_DLL}", w "${PORT_NAME}", w .r1) i .r0'
    ${If} $0 = 0
        DetailPrint "Print monitor installed"
        WriteRegStr HKLM "${PORT_CONFIG_KEY}"   "Command" '$INSTDIR\printdriver\filter.exe'
        WriteRegStr HKLM "${PORT_CONFIG_KEY}"   "Arguments" ''
        WriteRegDWORD HKLM "${PORT_CONFIG_KEY}" "RunUser" 0
        DetailPrint "Installing flexVDI printer"
        nsExec::Exec 'rundll32 printui.dll,PrintUIEntry /dl /n "${PDF_PRINTER_NAME}" /q'
        nsExec::Exec 'rundll32 printui.dll,PrintUIEntry /if /f "$INSTDIR\printdriver\flexvdips.inf" /m "flexVDI Printer" /r "${PORT_NAME}"'
        Pop $0
        ${If} $0 = 0
            DetailPrint "flexVDI printer successfully installed"
            System::Call 'setup::renamePrinter(w "flexVDI Printer", w "${PDF_PRINTER_NAME}", w .r1) i .r0'
        ${Else}
            MessageBox MB_OK|MB_ICONEXCLAMATION "flexVDI printer NOT installed (error code $0)."
        ${EndIf}
    ${Else}
        MessageBox MB_OK|MB_ICONEXCLAMATION "Print monitor installation failed with code $0: $1"
    ${EndIf}
!macroend

!macro uninstallPrintDriver
    nsExec::Exec 'rundll32 printui.dll,PrintUIEntry /dl /n "${PDF_PRINTER_NAME}" /q'
    nsExec::Exec 'rundll32 printui.dll,PrintUIEntry /dd /m "flexVDI Printer" /q'
    SetOutPath "$INSTDIR"
    System::Call 'setup::uninstallRedMon(w "${REDMON_NAME}", w .r1) i .r0 u'
    ${If} $0 = 0
        Delete /REBOOTOK "$SYSDIR\redmon.dll"
    ${Else}
        DetailPrint "ERROR Printer monitor could not be removed: $1"
    ${EndIf}
    SetPluginUnload manual
    System::Free 0
!macroend
