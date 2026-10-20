//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

// Starts Hades as Steam's Spacewar (AppID 480).
//
// Start it directly, from a desktop shortcut, or as a non-Steam game in Steam.
// It starts Steam if needed and then the game, which connects to Steam as Spacewar.
// Steam never starts the Hades entry of the library, so it only shows Spacewar.
//
// Options:
//   --game "C:\...\Hades.exe"  game to start, found next to the mod folder by default
//   --vulkan                   prefer the Vulkan version of the game

#include <windows.h>

#include <shellapi.h>

#include <string>
#include <utility>

static const wchar_t *TITLE = L"Hades coop launcher";
static const wchar_t *SPACEWAR_APP_ID = L"480";

// How long to wait for Steam to log in after starting it
static const DWORD STEAM_START_TIMEOUT_MS = 90000;
static const DWORD STEAM_POLL_INTERVAL_MS = 500;

static void ShowError(const std::wstring &message) {
    MessageBoxW(nullptr, message.c_str(), TITLE, MB_OK | MB_ICONERROR);
}

static bool FileExists(const std::wstring &path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring GetDirectory(const std::wstring &path) {
    size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? L"" : path.substr(0, separator);
}

static std::wstring GetLauncherPath() {
    wchar_t path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return L"";

    return std::wstring(path, length);
}

// The mod lives inside the game folder, so the game is next to one of the parent folders
static std::wstring FindGame(std::wstring directory, bool preferVulkan) {
    const wchar_t *variants[] = {L"x64", L"x64Vk"};
    if (preferVulkan)
        std::swap(variants[0], variants[1]);

    while (!directory.empty()) {
        for (const wchar_t *variant : variants) {
            std::wstring candidate = directory + L"\\" + variant + L"\\Hades.exe";
            if (FileExists(candidate))
                return candidate;
        }
        directory = GetDirectory(directory);
    }

    return L"";
}

enum class SteamState { Unknown, LoggedOut, LoggedIn };

// Steam keeps the logged in account in the registry while it runs
static SteamState GetSteamState() {
    DWORD activeUser = 0;
    DWORD size = sizeof(activeUser);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser",
                                  RRF_RT_REG_DWORD, nullptr, &activeUser, &size);
    if (status != ERROR_SUCCESS)
        return SteamState::Unknown;

    return activeUser != 0 ? SteamState::LoggedIn : SteamState::LoggedOut;
}

// The game can only go online when Steam runs before it starts
static void EnsureSteamRunning() {
    if (GetSteamState() != SteamState::LoggedOut)
        return;

    ShellExecuteW(nullptr, L"open", L"steam://open/main", nullptr, nullptr, SW_SHOWNORMAL);

    for (DWORD waited = 0; waited < STEAM_START_TIMEOUT_MS; waited += STEAM_POLL_INTERVAL_MS) {
        if (GetSteamState() == SteamState::LoggedIn)
            return;
        Sleep(STEAM_POLL_INTERVAL_MS);
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        ShowError(L"Cannot read the command line");
        return 1;
    }

    std::wstring gamePath;
    bool preferVulkan = false;
    std::wstring connectLobby;

    for (int i = 1; i < argc; i++) {
        std::wstring argument = argv[i];
        if (argument == L"--game" && i + 1 < argc) {
            gamePath = argv[++i];
        } else if (argument == L"--vulkan") {
            preferVulkan = true;
        } else if (argument == L"+connect_lobby" && i + 1 < argc) {
            connectLobby = argv[++i];
        }
    }
    LocalFree(argv);

    if (gamePath.empty())
        gamePath = FindGame(GetDirectory(GetLauncherPath()), preferVulkan);

    if (gamePath.empty() || !FileExists(gamePath)) {
        ShowError(L"Cannot find Hades.exe.\n\n"
                  L"Keep HadesCoopLauncher.exe in the mod folder inside the Hades folder, "
                  L"or pass the game path:\n\n"
                  L"HadesCoopLauncher.exe --game \"C:\\...\\Hades\\x64\\Hades.exe\"");
        return 1;
    }

    EnsureSteamRunning();

    // The game connects to Steam as the app in these variables
    SetEnvironmentVariableW(L"SteamAppId", SPACEWAR_APP_ID);
    SetEnvironmentVariableW(L"SteamGameId", SPACEWAR_APP_ID);

    // Steam's overlay for Vulkan is a layer that only loads with this variable.
    // Steam sets it for programs it starts, set it for the Vulkan version started from elsewhere too.
    if (!GetEnvironmentVariableW(L"ENABLE_VK_LAYER_VALVE_steam_overlay_1", nullptr, 0))
        SetEnvironmentVariableW(L"ENABLE_VK_LAYER_VALVE_steam_overlay_1", L"1");

    std::wstring commandLine = L"\"" + gamePath + L"\"";
    if (!connectLobby.empty())
        commandLine += L" +connect_lobby " + connectLobby;

    // The game loads its content relative to its own folder
    std::wstring gameDirectory = GetDirectory(gamePath);

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    if (!CreateProcessW(gamePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        gameDirectory.c_str(), &startupInfo, &processInfo)) {
        ShowError(L"Cannot start " + gamePath + L" (error " + std::to_wstring(GetLastError()) + L")");
        return 1;
    }

    // When Steam started the launcher as a non-Steam game, it shows it as running while the game runs
    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return static_cast<int>(exitCode);
}
