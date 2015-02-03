!define CP_KEY "SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers"

!macro installCredProvider
    ${If} ${RunningX64}
        File "../win64/cred-connectors/credential-provider/flexVDICredentialProvider.dll"
    ${Else}
        File "../win32/cred-connectors/credential-provider/flexVDICredentialProvider.dll"
    ${EndIf}
    Rename /REBOOTOK $INSTDIR\flexVDICredentialProvider.dll $SYSDIR\flexVDICredentialProvider.dll
    WriteRegStr HKLM "${CP_KEY}\{@FLEXVDI_GUID@}" "" "flexVDICredentialProvider"
    WriteRegStr HKCR "CLSID\{@FLEXVDI_GUID@}" "" "flexVDICredentialProvider"
    WriteRegStr HKCR "CLSID\{@FLEXVDI_GUID@}\InProcServer32" "" "flexVDICredentialProvider.dll"
    WriteRegStr HKCR "CLSID\{@FLEXVDI_GUID@}\InProcServer32" "ThreadingModel" "Apartment"
    DetailPrint "Credential provider installed"
!macroend

!macro uninstallCredProvider
    DeleteRegKey HKLM "${CP_KEY}\{@FLEXVDI_GUID@}"
    DeleteRegKey HKCR "CLSID\{@FLEXVDI_GUID@}"
    Delete /REBOOTOK $SYSDIR\flexVDICredentialProvider.dll
    DetailPrint "Credential provider removed"
!macroend
