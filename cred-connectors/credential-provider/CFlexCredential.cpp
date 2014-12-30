//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//

#include <ntstatus.h>
#include <Shlwapi.h>
#include "CFlexCredential.h"
#include "guid.h"
#include "helpers.h"
#include "util.hpp"
#include "resources.h"
using namespace flexvm;

void DllAddRef();
void DllRelease();


// Get rid of warnings due to const casts
static wchar_t imageFieldName[] = L"Image";
static wchar_t greetingFieldName[] = L"Greeting";
static wchar_t usernameFieldName[] = L"Username";
static wchar_t passwordFieldName[] = L"Password";
static wchar_t domainFieldName[] = L"Domain";
static wchar_t submitFieldName[] = L"Submit";
// Field descriptors for unlock and logon.
// The first field is the index of the field.
// The second is the type of the field.
// The third is the name of the field, NOT the value which will appear in the field.
const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR CFlexCredential::fieldDescriptors[] = {
    { SFI_TILEIMAGE, CPFT_TILE_IMAGE, imageFieldName },
    { SFI_GREETING, CPFT_LARGE_TEXT, greetingFieldName },
    { SFI_USERNAME, CPFT_EDIT_TEXT, usernameFieldName },
    { SFI_PASSWORD, CPFT_PASSWORD_TEXT, passwordFieldName },
    { SFI_DOMAIN, CPFT_EDIT_TEXT, domainFieldName },
    { SFI_SUBMIT_BUTTON, CPFT_SUBMIT_BUTTON, submitFieldName },
};


// The field state value indicates whether the field is displayed
// in the selected tile, the deselected tile, or both.
// The Field interactive state indicates when
const FIELD_STATE_PAIR CFlexCredential::fieldStatePairs[] = {
    { CPFS_DISPLAY_IN_BOTH, CPFIS_NONE },                   // SFI_TILEIMAGE
    { CPFS_DISPLAY_IN_BOTH, CPFIS_NONE },                   // SFI_GREETING
    { CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE },          // SFI_USERNAME
    { CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_FOCUSED },       // SFI_PASSWORD
    { CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE },          // SFI_DOMAIN
    { CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE    },       // SFI_SUBMIT_BUTTON
};


CFlexCredential::CFlexCredential(HINSTANCE h): refCount(1), dllHInst(h), events(NULL) {
    LogCall cl(__FUNCTION__);
    DllAddRef();
    ZeroMemory(fieldContent, sizeof(fieldContent));
    InitializeCriticalSection(&mutex);
}


struct Lock {
    CRITICAL_SECTION & mutex;
    Lock(CRITICAL_SECTION & mutex) : mutex(mutex) {
        EnterCriticalSection(&mutex);
    }
    ~Lock() {
        LeaveCriticalSection(&mutex);
    }
};


CFlexCredential::~CFlexCredential() {
    LogCall cl(__FUNCTION__);
    clearPassword();
    for (int i = 0; i < SFI_NUM_FIELDS; i++) {
        CoTaskMemFree(fieldContent[i]);
    }

    DllRelease();
}


void CFlexCredential::clearPassword() {
    Lock lock(mutex);
    if (fieldContent[SFI_PASSWORD]) {
        size_t lenPassword = lstrlen(fieldContent[SFI_PASSWORD]);
        SecureZeroMemory(fieldContent[SFI_PASSWORD], lenPassword * sizeof(wchar_t));
        CoTaskMemFree(fieldContent[SFI_PASSWORD]);
        fieldContent[SFI_PASSWORD] = nullptr;
    }
}


IFACEMETHODIMP CFlexCredential::QueryInterface(REFIID riid, void ** ppv) {
    if (riid == IID_ICredentialProviderCredential) {
        ICredentialProviderCredential * thisCPC = static_cast<ICredentialProviderCredential *>(this);
        thisCPC->AddRef();
        *ppv = thisCPC;
        return S_OK;
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
}


/* Obtiene el valor en el registro de windows asociado a la clave "key", dentro de la  subclave "subkey".
 * S�lo busca bajo HKLM (local machine) que es suficiente para lo que buscamos.
 * N�tese que en el registro las claves est�n dentro de las subclaves.
 * Ejemplo:
 *    Subkey: SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\LogonUI\\SessionData\\1
 *    Key: LoggedOnSAMUSer
 *
 *    Hace uso de .Net framework. NO recomendada para XP que no lo trae de serie.
 */
static bool getRegistryValueInLocalMachine(const wchar_t * subkey, const wchar_t * key,
                                           wchar_t * value, int size) {
    Log(L_DEBUG) << "getRegistryValueInLocalMachine(" << subkey << ", " << key << ")";
    HKEY hkey;
    LONG status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, subkey, 0, KEY_QUERY_VALUE, &hkey);
    return_if(status, "Failed to open WinLogon registry key", false);
    DWORD cb = size * sizeof(wchar_t);
    status = RegQueryValueEx(hkey, key, 0, 0, (BYTE *)value, &cb);
    RegCloseKey(hkey);
    return_if(status, "Failed to read registry key", false);
    return true;
}


/* Parsea input que ser� una cadena como domain\username, username, o "".
 * Los valores de los trozos los devuelve en los parametros user y domain
 * username y domain son wchar_t[size]. Los sobreescribimos
 * En el Initialize de CrednetialProvider podemos contar con que es as�.
 */
static void parseCompositeUserName(wchar_t * input, wchar_t * username,
                                   wchar_t * domain, int size) {
    wchar_t * nextToken = NULL;
    wchar_t * token1 = wcstok_s(input, L"\\", &nextToken);
    if (token1 != NULL) {
        wchar_t * token2 = wcstok_s(NULL,  L"\\", &nextToken);
        if (token2 != NULL) {
            wcscpy_s(username, size, token2);
            wcscpy_s(domain, size, token1);
        } else {
            wcscpy_s(username, size, token1);
        }
    } else {
        wcscpy_s(username, size, L"");
        wcscpy_s(domain, size, L"");
    }
}


// Initializes one credential with the field information passed in.
// Set the value of the SFI_USERNAME field to pwzUsername.
HRESULT CFlexCredential::Initialize(CREDENTIAL_PROVIDER_USAGE_SCENARIO scenario) {
    LogCall cl(__FUNCTION__);
    cpus = scenario;

    // Get last login username and domain
    wchar_t username[MAX_CRED_SIZE];
    wchar_t domain[MAX_CRED_SIZE];
    wchar_t registryUserName[2 * MAX_CRED_SIZE];
    getRegistryValueInLocalMachine(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\LogonUI\\",
            L"LastLoggedOnSAMUSer", registryUserName, 2 * MAX_CRED_SIZE);
    Log(L_DEBUG) << "CFlexCredential::Initialize() registryUserName: " << registryUserName;
    parseCompositeUserName(registryUserName, username, domain, MAX_CRED_SIZE);
    Log(L_DEBUG) << "CFlexCredential::Initialize() flexUser: " << username;
    Log(L_DEBUG) << "CFlexCredential::Initialize() proposedDomain: " << domain;

    Lock lock(mutex);
    HRESULT hr = S_OK;
    // Initialize the String value of all the fields.
    hr = SHStrDupW(L"FlexVDI SSO", &fieldContent[SFI_GREETING]);
    if (SUCCEEDED(hr)) {
        hr = SHStrDupW(username, &fieldContent[SFI_USERNAME]);
    }
    if (SUCCEEDED(hr)) {
        hr = SHStrDupW(L"", &fieldContent[SFI_PASSWORD]);
    }
    if (SUCCEEDED(hr)) {
        hr = SHStrDupW(domain, &fieldContent[SFI_DOMAIN]);
    }
    if (SUCCEEDED(hr)) {
        hr = SHStrDupW(L"Submit", &fieldContent[SFI_SUBMIT_BUTTON]);
    }
    return hr;
}


// LogonUI calls this in order to give us a callback in case we need to notify it of anything.
HRESULT CFlexCredential::Advise(ICredentialProviderCredentialEvents * pcpce) {
    LogCall cl(__FUNCTION__);
    if (events != NULL) {
        events->Release();
    }
    events = pcpce;
    events->AddRef();
    return S_OK;
}


// LogonUI calls this to tell us to release the callback.
HRESULT CFlexCredential::UnAdvise() {
    LogCall cl(__FUNCTION__);
    if (events) {
        events->Release();
    }
    events = NULL;
    return S_OK;
}


// LogonUI calls this function when our tile is selected (zoomed)
// If you simply want fields to show/hide based on the selected state,
// there's no need to do anything here - you can set that up in the
// field definitions.  But if you want to do something
// more complicated, like change the contents of a field when the tile is
// selected, you would do it here.
HRESULT CFlexCredential::SetSelected(BOOL * pbAutoLogon) {
    LogCall lc(__FUNCTION__);
    *pbAutoLogon = FALSE;
    return S_OK;
}


// Similarly to SetSelected, LogonUI calls this when your tile was selected
// and now no longer is.  The most common thing to do here (which we do below)
// is to clear out the password field.
HRESULT CFlexCredential::SetDeselected() {
    LogCall lc(__FUNCTION__);
    HRESULT hr = S_OK;
    Lock lock(mutex);
    if (fieldContent[SFI_PASSWORD]) {
        clearPassword();
        hr = SHStrDupW(L"", &fieldContent[SFI_PASSWORD]);
        if (SUCCEEDED(hr) && events) {
            events->SetFieldString(this, SFI_PASSWORD, fieldContent[SFI_PASSWORD]);
        }
    }
    return hr;
}

// Get info for a particular field of a tile. Called by logonUI to get information to
// display the tile.
HRESULT CFlexCredential::GetFieldState(DWORD dwFieldID,
    CREDENTIAL_PROVIDER_FIELD_STATE * pcpfs,
    CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE * pcpfis
) {
    LogCall cl(__FUNCTION__);
    if (dwFieldID < SFI_NUM_FIELDS) {
        *pcpfis = fieldStatePairs[dwFieldID].cpfis;
        *pcpfs = fieldStatePairs[dwFieldID].cpfs;
        return S_OK;
    } else {
        return E_INVALIDARG;
    }
}

// Sets ppwsz to the string value of the field at the index dwFieldID.
HRESULT CFlexCredential::GetStringValue(DWORD dwFieldID, PWSTR * ppwsz) {
    LogCall cl(__FUNCTION__);
    if (dwFieldID < SFI_NUM_FIELDS) {
        Lock lock(mutex);
        return SHStrDupW(fieldContent[dwFieldID], ppwsz);
    } else {
        return E_INVALIDARG;
    }
}

// Get the image to show in the user tile.
HRESULT CFlexCredential::GetBitmapValue(DWORD dwFieldID, HBITMAP * phbmp) {
    LogCall cl(__FUNCTION__);
    if (dwFieldID == SFI_TILEIMAGE) {
        *phbmp = LoadBitmap(dllHInst, MAKEINTRESOURCE(IDB_TILE_IMAGE));
        return *phbmp ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    } else {
        return E_INVALIDARG;
    }
}

// Sets pdwAdjacentTo to the index of the field the submit button should be
// adjacent to. We recommend that the submit button is placed next to the last
// field which the user is required to enter information in. Optional fields
// should be below the submit button.
HRESULT CFlexCredential::GetSubmitButtonValue(DWORD dwFieldID, DWORD * pdwAdjacentTo) {
    LogCall cl(__FUNCTION__);
    if (dwFieldID == SFI_SUBMIT_BUTTON) {
        // pdwAdjacentTo is a pointer to the fieldID you want the submit button to
        // appear next to.
        *pdwAdjacentTo = SFI_PASSWORD;
        return S_OK;
    } else {
        return E_INVALIDARG;
    }
}

// Sets the value of a field which can accept a string as a value.
// This is called on each keystroke when a user types into an edit field
HRESULT CFlexCredential::SetStringValue(DWORD dwFieldID, PCWSTR pwz) {
    LogCall cl(__FUNCTION__);
    if (dwFieldID < SFI_NUM_FIELDS &&
        (fieldDescriptors[dwFieldID].cpft == CPFT_EDIT_TEXT ||
        fieldDescriptors[dwFieldID].cpft == CPFT_PASSWORD_TEXT)) {
        Lock lock(mutex);
        CoTaskMemFree(fieldContent[dwFieldID]);
        return SHStrDupW(pwz, &fieldContent[dwFieldID]);
    } else {
        return E_INVALIDARG;
    }
}


void CFlexCredential::setCredentials(wchar_t * username, wchar_t * password,
                                     wchar_t * domain) {
    Lock lock(mutex);
    CoTaskMemFree(fieldContent[SFI_USERNAME]);
    fieldContent[SFI_USERNAME] = username;
    clearPassword();
    fieldContent[SFI_PASSWORD] = password;
    CoTaskMemFree(fieldContent[SFI_DOMAIN]);
    fieldContent[SFI_DOMAIN] = domain;
}


// Collect the username and password into a serialized credential for the correct usage scenario
// (logon/unlock is what's demonstrated in this sample).  LogonUI then passes these credentials
// back to the system to log on.
// Los valores que se serializan para pasar a windows son flexUser, flexPassword, flexDomain.
// 2014-04: Ahora tambi�n usa el dominio.
// El parametro de salida "importante" es el pcpcs que contiene username, pass, domain, ... "empaquetados"
HRESULT CFlexCredential::GetSerialization(
    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE * pcpgsr,
    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION * pcpcs,
    PWSTR * ppwszOptionalStatusText,
    CREDENTIAL_PROVIDER_STATUS_ICON * pcpsiOptionalStatusIcon
) {
    LogCall cl(__FUNCTION__);
    // If we have read usable credentials, we have them in en flexUser, flexPassword, flexDomain
    // We use them and overwrite screen field values (fieldContent).
    // Otherwise leave gui fields untouched and login with them.

    HRESULT hr = S_OK; //
    Lock lock(mutex);
    
    WCHAR effectiveDomain[MAX_CRED_SIZE]; // Variable que contendr� el nombre de dominio usado para autenticar. En el original se llamaba wsz �?
    DWORD domainLength = MAX_CRED_SIZE;
    Log(L_DEBUG) << L"Deciding Effective Domain";

    // Si han especificado domain, lo usamos. Si no, usamos getComputerName()
    if (wcslen(fieldContent[SFI_DOMAIN]) > 0) {
        wcscpy_s(effectiveDomain, MAX_CRED_SIZE, fieldContent[SFI_DOMAIN]);
    } else {
        hr = GetComputerNameW(effectiveDomain, &domainLength);
        return_if(FAILED(hr), "GetComputerNameW failed", HRESULT_FROM_WIN32(GetLastError()));
    }
    Log(L_DEBUG) << "Effective domain: " << effectiveDomain;

    Log(L_DEBUG) << "-> KIUL: usu: " << fieldContent[SFI_USERNAME]
        << ", pas: *, dom: " << effectiveDomain;

    PWSTR protectedPassword; // Var. auxiliar para guardar el password protegido
    hr = ProtectIfNecessaryAndCopyPassword(fieldContent[SFI_PASSWORD],
                                           cpus, &protectedPassword);
    return_if(FAILED(hr), "Protect password failed", HRESULT_FROM_WIN32(GetLastError()));
    KERB_INTERACTIVE_UNLOCK_LOGON kiul; // Var que contiene username, pass y domain
    // Initialize kiul with weak references to our credential.
    hr = KerbInteractiveUnlockLogonInit(effectiveDomain, fieldContent[SFI_USERNAME],
                                        protectedPassword, cpus, &kiul);
    if (SUCCEEDED(hr)) {
        // We use KERB_INTERACTIVE_UNLOCK_LOGON in both unlock and logon scenarios.  It contains a
        // KERB_INTERACTIVE_LOGON to hold the creds plus a LUID that is filled in for us by Winlogon
        // as necessary.
        hr = KerbInteractiveUnlockLogonPack(kiul, &pcpcs->rgbSerialization,
                                            &pcpcs->cbSerialization);
        CoTaskMemFree(protectedPassword);
        return_if(FAILED(hr), "Creds pack failed", HRESULT_FROM_WIN32(GetLastError()));
        ULONG ulAuthPackage;
        hr = RetrieveNegotiateAuthPackage(&ulAuthPackage);
        return_if(FAILED(hr), "AuthPackage failed", HRESULT_FROM_WIN32(GetLastError()));
        pcpcs->ulAuthenticationPackage = ulAuthPackage;
        pcpcs->clsidCredentialProvider = CLSID_flexVDI_CP;// GUID creado para nuestro CredProv
        // At this point the credential has created the serialized credential used for logon
        // By setting this to CPGSR_RETURN_CREDENTIAL_FINISHED we are letting logonUI know
        // that we have all the information we need and it should attempt to submit the
        // serialized credential.
        *pcpgsr = CPGSR_RETURN_CREDENTIAL_FINISHED;
    }

    return hr;
}


// struct REPORT_RESULT_STATUS_INFO {
//     NTSTATUS ntsStatus;
//     NTSTATUS ntsSubstatus;
//     PCWSTR     pwzMessage;
//     CREDENTIAL_PROVIDER_STATUS_ICON cpsi;
// };
//
// static const REPORT_RESULT_STATUS_INFO s_rgLogonStatusInfo[] = {
//     { STATUS_LOGON_FAILURE, STATUS_SUCCESS, L"Incorrect password or username.", CPSI_ERROR, },
//     { STATUS_ACCOUNT_RESTRICTION, STATUS_ACCOUNT_DISABLED, L"The account is disabled.", CPSI_WARNING },
// };


// ReportResult is completely optional.  Its purpose is to allow a credential to customize the string
// and the icon displayed in the case of a logon failure.  For example, we have chosen to
// customize the error shown in the case of bad username/password and in the case of the account
// being disabled.
HRESULT CFlexCredential::ReportResult(
    NTSTATUS ntsStatus,
    NTSTATUS ntsSubstatus,
    PWSTR * ppwszOptionalStatusText,
    CREDENTIAL_PROVIDER_STATUS_ICON * pcpsiOptionalStatusIcon
) {
    LogCall lc(__FUNCTION__);

    *ppwszOptionalStatusText = NULL;
    *pcpsiOptionalStatusIcon = CPSI_NONE;

//     DWORD dwStatusInfo = (DWORD) - 1;
//
//     //FLEX Autologon. Tras un intento de login quitamos el autologon (por si las credenciales fallan)
//     /*CReaderThread::log("Setting usable to false");
//     usableCredentials=false;*/
//
//     // Look for a match on status and substatus.
//     for (DWORD i = 0; i < ARRAYSIZE(s_rgLogonStatusInfo); i++) {
//         if (s_rgLogonStatusInfo[i].ntsStatus == ntsStatus && s_rgLogonStatusInfo[i].ntsSubstatus == ntsSubstatus) {
//             dwStatusInfo = i;
//             break;
//         }
//     }
//
//     if ((DWORD) - 1 != dwStatusInfo) {
//         if (SUCCEEDED(SHStrDupW(s_rgLogonStatusInfo[dwStatusInfo].pwzMessage, ppwszOptionalStatusText))) {
//             *pcpsiOptionalStatusIcon = s_rgLogonStatusInfo[dwStatusInfo].cpsi;
//         }
//     }
//
//     // If we failed the logon, try to erase the password field.
//     if (!SUCCEEDED(HRESULT_FROM_NT(ntsStatus))) {
//         if (events) {
//             events->SetFieldString(this, SFI_PASSWORD, L"");
//         }
//         //FLEX Autologon. Y guardamos que las credenciales NO son buenas
//         Log(L_DEBUG) << "ReportResult Setting usable to false";
// //         usableCredentials = false;
//
//     }
//
    // Since NULL is a valid value for *ppwszOptionalStatusText and *pcpsiOptionalStatusIcon
    // this function can't fail.
    return S_OK;
}

//-------------
// The following methods are for logonUI to get the values of various UI elements and then communicate
// to the credential about what the user did in that field.  However, these methods are not implemented
// because our tile doesn't contain these types of UI elements
HRESULT CFlexCredential::GetCheckboxValue(DWORD dwFieldID, BOOL * pbChecked,
                                          PWSTR * ppwszLabel) {
    return E_NOTIMPL;
}

HRESULT CFlexCredential::GetComboBoxValueCount(DWORD dwFieldID, DWORD * pcItems,
                                               DWORD * pdwSelectedItem) {
    return E_NOTIMPL;
}

HRESULT CFlexCredential::GetComboBoxValueAt(DWORD dwFieldID, DWORD dwItem,
                                            PWSTR * ppwszItem) {
    return E_NOTIMPL;
}

HRESULT CFlexCredential::SetCheckboxValue(DWORD dwFieldID, BOOL bChecked) {
    return E_NOTIMPL;
}

HRESULT CFlexCredential::SetComboBoxSelectedValue(DWORD dwFieldId, DWORD dwSelectedItem) {
    return E_NOTIMPL;
}

HRESULT CFlexCredential::CommandLinkClicked(DWORD dwFieldID) {
    return E_NOTIMPL;
}
//------ end of methods for controls we don't have in our tile ----//
