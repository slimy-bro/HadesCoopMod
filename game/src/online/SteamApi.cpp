//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#include "pch.h"

#include "OnlineUtils.h"
#include "SteamApi.h"

namespace Steam {

// Interface versions the mod needs, in the double null terminated format of SteamInternal_SteamAPI_Init
static const char INTERFACE_VERSIONS[] = "SteamUser023\0"
                                         "SteamUtils011\0"
                                         "SteamFriends018\0"
                                         "SteamMatchMaking009\0"
                                         "SteamNetworking006\0";

// ESteamAPIInitResult
constexpr int k_ESteamAPIInitResult_OK = 0;
constexpr size_t k_cchMaxSteamErrMsg = 1024;

template <class T> static bool GetExport(HMODULE module, const char *name, T &out) {
    out = reinterpret_cast<T>(GetProcAddress(module, name));
    return out != nullptr;
}

bool SteamApi::AttachToLoadedModule() {
    if (m_module)
        return true;

    HMODULE module = GetModuleHandleW(DLL_NAME);
    if (!module)
        return false;

    m_module = module;

    std::string error;
    if (!ResolveExports(error)) {
        m_module = nullptr;
        return false;
    }

    return true;
}

bool SteamApi::LoadModule(const std::wstring &path, std::string &error) {
    if (m_module)
        return true;

    HMODULE module = LoadLibraryW(path.c_str());
    if (!module) {
        error = "Cannot load " + OnlineUtils::ToUtf8(path) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    m_module = module;

    if (!ResolveExports(error)) {
        m_module = nullptr;
        FreeLibrary(module);
        return false;
    }

    return true;
}

bool SteamApi::ResolveExports(std::string &error) {
    bool ok = true;
    ok &= GetExport(m_module, "SteamAPI_GetHSteamUser", m_getHSteamUser);
    ok &= GetExport(m_module, "SteamInternal_FindOrCreateUserInterface", m_findOrCreateUserInterface);
    ok &= GetExport(m_module, "SteamAPI_RunCallbacks", m_runCallbacks);
    ok &= GetExport(m_module, "SteamAPI_RegisterCallback", m_registerCallback);
    ok &= GetExport(m_module, "SteamAPI_UnregisterCallback", m_unregisterCallback);
    ok &= GetExport(m_module, "SteamAPI_RegisterCallResult", m_registerCallResult);
    ok &= GetExport(m_module, "SteamAPI_UnregisterCallResult", m_unregisterCallResult);

    // Different SDK versions export different init functions
    GetExport(m_module, "SteamInternal_SteamAPI_Init", m_internalInit);
    GetExport(m_module, "SteamAPI_InitFlat", m_initFlat);
    GetExport(m_module, "SteamAPI_Init", m_initLegacy);

    if (!ok) {
        error = "steam_api64.dll is too old or not a Steamworks library";
        return false;
    }

    return true;
}

bool SteamApi::IsInitialized() const {
    if (!m_module)
        return false;

    return m_getHSteamUser() != 0;
}

bool SteamApi::Init(std::string &error) {
    if (!m_module) {
        error = "steam_api64.dll is not loaded";
        return false;
    }

    char errMsg[k_cchMaxSteamErrMsg] = {};
    bool success = false;

    if (m_internalInit) {
        success = m_internalInit(INTERFACE_VERSIONS, errMsg) == k_ESteamAPIInitResult_OK;
    } else if (m_initFlat) {
        success = m_initFlat(errMsg) == k_ESteamAPIInitResult_OK;
    } else if (m_initLegacy) {
        success = m_initLegacy();
    } else {
        error = "steam_api64.dll has no init function";
        return false;
    }

    if (!success) {
        error = errMsg[0] ? errMsg : "SteamAPI_Init failed. Is Steam running?";
        return false;
    }

    return IsInitialized();
}

void SteamApi::RunCallbacks() {
    if (m_module)
        m_runCallbacks();
}

void SteamApi::RegisterCallback(CCallbackBase *callback, int callbackId) {
    if (m_module)
        m_registerCallback(callback, callbackId);
}

void SteamApi::UnregisterCallback(CCallbackBase *callback) {
    if (m_module)
        m_unregisterCallback(callback);
}

void SteamApi::RegisterCallResult(CCallbackBase *callback, SteamAPICall_t apiCall) {
    if (m_module)
        m_registerCallResult(callback, apiCall);
}

void SteamApi::UnregisterCallResult(CCallbackBase *callback, SteamAPICall_t apiCall) {
    if (m_module)
        m_unregisterCallResult(callback, apiCall);
}

void *SteamApi::FindInterface(const char *version) {
    if (!IsInitialized())
        return nullptr;

    return m_findOrCreateUserInterface(m_getHSteamUser(), version);
}

} // namespace Steam
