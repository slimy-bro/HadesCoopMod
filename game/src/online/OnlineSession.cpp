//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#include "pch.h"

#include "OnlineSession.h"

#include "OnlineLog.h"
#include "OnlineUtils.h"
#include "extensions/PlayerManagerExtension.h"

#include <chrono>
#include <cstring>
#include <initializer_list>
#include <iterator>

#ifndef HADES_COOP_VERSION
#define HADES_COOP_VERSION "dev"
#endif

using namespace Steam;
using namespace NetProtocol;

// Spacewar lobbies are shared by every project that uses AppID 480,
// the marker tells the mod lobbies apart.
static const char *LOBBY_KEY_MARKER = "hades_coop";
static const char *LOBBY_MARKER_VALUE = "1";
static const char *LOBBY_KEY_PROTOCOL = "hades_coop_protocol";
static const char *LOBBY_KEY_VERSION = "hades_coop_version";

constexpr uint64_t SECOND_US = 1000000;
constexpr uint64_t HELLO_INTERVAL_US = SECOND_US;
constexpr uint64_t PING_INTERVAL_US = SECOND_US;
constexpr uint64_t SESSION_STATE_INTERVAL_US = 2 * SECOND_US;
constexpr uint64_t CONNECTION_TIMEOUT_US = 10 * SECOND_US;
constexpr uint64_t MAX_PING_AGE_US = 30 * SECOND_US;
constexpr auto NET_THREAD_INTERVAL = std::chrono::milliseconds(5);
// Without a peer the thread only pumps Steam callbacks
constexpr auto NET_THREAD_IDLE_INTERVAL = std::chrono::milliseconds(50);

constexpr size_t MAX_QUEUED_EVENTS = 4096;
constexpr int MAX_PACKETS_PER_UPDATE = 256;
// Limits of ISteamNetworking::SendP2PPacket
constexpr size_t MAX_UNRELIABLE_SIZE = 1200;
constexpr size_t MAX_RELIABLE_SIZE = 1024 * 1024;

// Exists while a launch with the Steam AppID override hasn't reached the game scripts yet
static const wchar_t *RELAUNCH_GUARD_FILE = L"HadesCoopOnline.relaunch-guard";
constexpr uint64_t RELAUNCH_GUARD_LIFETIME_SECONDS = 120;

static std::wstring GetRelaunchGuardPath() {
    wchar_t tempPath[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tempPath))
        return L"";

    return std::wstring(tempPath) + RELAUNCH_GUARD_FILE;
}

static bool IsRecentFile(const std::wstring &path, uint64_t maxAgeSeconds) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;

    FILETIME now;
    GetSystemTimeAsFileTime(&now);

    ULARGE_INTEGER written;
    written.LowPart = data.ftLastWriteTime.dwLowDateTime;
    written.HighPart = data.ftLastWriteTime.dwHighDateTime;
    ULARGE_INTEGER current;
    current.LowPart = now.dwLowDateTime;
    current.HighPart = now.dwHighDateTime;

    // FILETIME counts 100ns intervals
    return current.QuadPart >= written.QuadPart && current.QuadPart - written.QuadPart <= maxAgeSeconds * 10000000;
}

static void TouchFile(const std::wstring &path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
}

// steam_api64.dll reads the AppID from these variables when the game is launched by Steam
static void SetSteamAppIdEnvironment(uint32_t appId) {
    std::wstring value = std::to_wstring(appId);
    SetEnvironmentVariableW(L"SteamAppId", value.c_str());
    SetEnvironmentVariableW(L"SteamGameId", value.c_str());

    // C runtimes keep their own copy of the environment for getenv. Update the shared ones too.
    using wputenv_s_t = int(__cdecl *)(const wchar_t *name, const wchar_t *value);
    for (const wchar_t *runtime : {L"ucrtbase.dll", L"msvcrt.dll"}) {
        HMODULE module = GetModuleHandleW(runtime);
        if (!module)
            continue;

        auto wputenv = reinterpret_cast<wputenv_s_t>(GetProcAddress(module, "_wputenv_s"));
        if (!wputenv)
            continue;

        wputenv(L"SteamAppId", value.c_str());
        wputenv(L"SteamGameId", value.c_str());
    }
}

static const char *LobbyEnterResponseToString(uint32_t response) {
    switch (response) {
    case k_EChatRoomEnterResponseDoesntExist:
        return "The lobby doesn't exist anymore";
    case k_EChatRoomEnterResponseNotAllowed:
        return "Not allowed to join this lobby";
    case k_EChatRoomEnterResponseFull:
        return "The lobby is full";
    case k_EChatRoomEnterResponseBanned:
    case k_EChatRoomEnterResponseCommunityBan:
        return "Banned from this lobby";
    case k_EChatRoomEnterResponseLimited:
        return "Limited Steam accounts can't join lobbies";
    case k_EChatRoomEnterResponseMemberBlockedYou:
        return "A lobby member has blocked you";
    case k_EChatRoomEnterResponseYouBlockedMember:
        return "You have blocked a lobby member";
    case k_EChatRoomEnterResponseRatelimitExceeded:
        return "Too many join attempts, try again later";
    default:
        return "Unexpected Steam error";
    }
}

static const char *P2PErrorToString(uint8_t error) {
    switch (error) {
    case 2:
        return "Steam says you don't own the app in use";
    case 4:
        return "Timed out. A firewall may block Steam networking";
    default:
        return "Steam networking error";
    }
}

static OnlineEvent MakeEvent(OnlineEventType type, uint64_t steamId = 0, std::string name = {}, std::string text = {}) {
    OnlineEvent event;
    event.type = type;
    event.steamId = steamId;
    event.name = std::move(name);
    event.text = std::move(text);
    return event;
}

static unsigned long long ToULL(uint64_t value) { return static_cast<unsigned long long>(value); }

OnlineSession &OnlineSession::Instance() {
    // Intentionally leaked: Steam may already be shut down when static destructors run
    static OnlineSession *instance = new OnlineSession();
    return *instance;
}

OnlineSession::OnlineSession()
    : m_lobbyCreatedResult(this, &OnlineSession::OnLobbyCreated),
      m_lobbyEnterCallback(this, &OnlineSession::OnLobbyEnter),
      m_lobbyChatUpdateCallback(this, &OnlineSession::OnLobbyChatUpdate),
      m_lobbyKickedCallback(this, &OnlineSession::OnLobbyKicked),
      m_gameLobbyJoinRequestedCallback(this, &OnlineSession::OnGameLobbyJoinRequested),
      m_richPresenceJoinRequestedCallback(this, &OnlineSession::OnRichPresenceJoinRequested),
      m_p2pSessionRequestCallback(this, &OnlineSession::OnP2PSessionRequest),
      m_p2pSessionConnectFailCallback(this, &OnlineSession::OnP2PSessionConnectFail) {}

void OnlineSession::PrepareEarly() {
    m_modDirectory = OnlineUtils::GetModuleDirectory();
    OnlineLog::Open(m_modDirectory + L"HadesCoopOnline.log");
    OnlineLog::Write("Hades coop mod %s, online protocol %u", HADES_COOP_VERSION, PROTOCOL_VERSION);

    // HadesCoopLauncher and Steam set it when the game starts as Spacewar
    char startupAppId[32] = {};
    const char *startupAppIdText =
        GetEnvironmentVariableA("SteamAppId", startupAppId, sizeof(startupAppId)) ? startupAppId : "not set";
    OnlineLog::Write("Started with SteamAppId %s%s", startupAppIdText,
                     OnlineUtils::ParseConnectLobby(GetCommandLineA()) != 0 ? ", from a lobby invite" : "");

    m_desiredAppId = OnlineUtils::ReadConfigNumber(m_modDirectory + L"config.lua", "SteamAppId", DEFAULT_APP_ID);

    const char *appIdOverride;
    if (m_desiredAppId == 0) {
        appIdOverride = "Disabled";
        OnlineLog::Write("The Steam AppID override is disabled in config.lua");
    } else if (m_steam.AttachToLoadedModule() && m_steam.IsInitialized()) {
        appIdOverride = "TooLate";
        OnlineLog::Write("The game initialized Steam before the mod was loaded, Steam AppID %u can't be applied",
                         m_desiredAppId);
    } else {
        std::wstring guardPath = GetRelaunchGuardPath();
        if (!guardPath.empty() && IsRecentFile(guardPath, RELAUNCH_GUARD_LIFETIME_SECONDS)) {
            // The previous launch closed before it loaded the game scripts.
            // Steam may have restarted the game because of the AppID, so don't apply it this time.
            DeleteFileW(guardPath.c_str());
            appIdOverride = "SkippedAfterRelaunch";
            OnlineLog::Write("The previous launch with Steam AppID %u closed during startup. "
                             "The AppID override is skipped for this launch",
                             m_desiredAppId);
        } else {
            if (!guardPath.empty())
                TouchFile(guardPath);

            SetSteamAppIdEnvironment(m_desiredAppId);
            appIdOverride = "Applied";
            OnlineLog::Write("Steam AppID is set to %u", m_desiredAppId);
        }
    }

    std::lock_guard lock(m_mutex);
    m_appIdOverride = appIdOverride;
}

void OnlineSession::OnLuaCreated() {
    std::wstring guardPath = GetRelaunchGuardPath();
    if (!guardPath.empty())
        DeleteFileW(guardPath.c_str());
}

bool OnlineSession::EnsureReady(std::string &error) {
    if (m_ready)
        return true;

    auto fail = [this, &error]() {
        OnlineLog::Write("Steam is not available: %s", error.c_str());
        std::lock_guard lock(m_mutex);
        m_error = error;
        return false;
    };

    if (!m_steam.IsLoaded() && !m_steam.AttachToLoadedModule()) {
        // The Epic Games Store version doesn't ship Steam, use the library next to the mod
        if (!m_steam.LoadModule(m_modDirectory + SteamApi::DLL_NAME, error)) {
            error += ". Copy steam_api64.dll next to HadesCoopGame.dll";
            return fail();
        }
        OnlineLog::Write("Loaded steam_api64.dll from the mod folder");
    }

    if (!m_steam.IsInitialized()) {
        if (m_desiredAppId != 0)
            SetSteamAppIdEnvironment(m_desiredAppId);

        if (!m_steam.Init(error))
            return fail();

        m_ownsSteamInit = true;
    }

    m_user = static_cast<ISteamUser *>(m_steam.FindInterface(SteamApi::USER_VERSION));
    m_utils = static_cast<ISteamUtils *>(m_steam.FindInterface(SteamApi::UTILS_VERSION));
    m_friends = static_cast<ISteamFriends *>(m_steam.FindInterface(SteamApi::FRIENDS_VERSION));
    m_matchmaking = static_cast<ISteamMatchmaking *>(m_steam.FindInterface(SteamApi::MATCHMAKING_VERSION));
    m_networking = static_cast<ISteamNetworking *>(m_steam.FindInterface(SteamApi::NETWORKING_VERSION));

    if (!m_user || !m_utils || !m_friends || !m_matchmaking || !m_networking) {
        error = "The Steam client doesn't provide the required interfaces. Update Steam";
        return fail();
    }

    m_localSteamId = m_user->GetSteamID().ConvertToUint64();
    m_appId = m_utils->GetAppID();
    m_networking->AllowP2PPacketRelay(true);

    m_lobbyEnterCallback.Register(m_steam);
    m_lobbyChatUpdateCallback.Register(m_steam);
    m_lobbyKickedCallback.Register(m_steam);
    m_gameLobbyJoinRequestedCallback.Register(m_steam);
    m_richPresenceJoinRequestedCallback.Register(m_steam);
    m_p2pSessionRequestCallback.Register(m_steam);
    m_p2pSessionConnectFailCallback.Register(m_steam);

    {
        std::lock_guard lock(m_mutex);
        m_error.clear();
    }

    m_ready = true;
    m_stopNetThread = false;
    m_netThread = std::thread(&OnlineSession::NetThreadMain, this);

    OnlineLog::Write("Steam is ready. AppID %u, user %llu, Steam was initialized by the %s", m_appId,
                     ToULL(m_localSteamId), m_ownsSteamInit ? "mod" : "game");

    // Steam passes the lobby this way when a friend's invite starts the game
    uint64_t lobbyId = OnlineUtils::ParseConnectLobby(GetCommandLineA());
    if (lobbyId != 0) {
        OnlineLog::Write("Joining lobby %llu from the command line", ToULL(lobbyId));
        std::string joinError;
        if (!Join(lobbyId, joinError))
            OnlineLog::Write("Cannot join the lobby: %s", joinError.c_str());
    }

    return true;
}

void OnlineSession::Shutdown() {
    if (!m_ready)
        return;

    LeaveLobby("Shutdown", false);

    m_stopNetThread = true;
    if (m_netThread.joinable())
        m_netThread.join();

    m_lobbyEnterCallback.Unregister(m_steam);
    m_lobbyChatUpdateCallback.Unregister(m_steam);
    m_lobbyKickedCallback.Unregister(m_steam);
    m_gameLobbyJoinRequestedCallback.Unregister(m_steam);
    m_richPresenceJoinRequestedCallback.Unregister(m_steam);
    m_p2pSessionRequestCallback.Unregister(m_steam);
    m_p2pSessionConnectFailCallback.Unregister(m_steam);

    m_ready = false;
    OnlineLog::Write("Online session stopped");
}

bool OnlineSession::Host(std::string &error) {
    if (!m_ready) {
        error = "Steam is not ready";
        return false;
    }

    {
        std::lock_guard lock(m_mutex);
        if (m_lobbyState != LobbyState::Idle) {
            error = "Already in a lobby";
            return false;
        }
        m_lobbyState = LobbyState::Creating;
    }

    SteamAPICall_t apiCall = m_matchmaking->CreateLobby(k_ELobbyTypeFriendsOnly, static_cast<int>(MAX_PLAYERS));
    if (apiCall == k_uAPICallInvalid) {
        std::lock_guard lock(m_mutex);
        m_lobbyState = LobbyState::Idle;
        error = "Steam refused to create a lobby";
        return false;
    }

    m_lobbyCreatedResult.Set(m_steam, apiCall);
    OnlineLog::Write("Creating a lobby");
    return true;
}

bool OnlineSession::Join(uint64_t lobbyId, std::string &error) {
    if (!m_ready) {
        error = "Steam is not ready";
        return false;
    }

    if (lobbyId == 0) {
        error = "Invalid lobby";
        return false;
    }

    {
        std::lock_guard lock(m_mutex);
        if (m_lobbyState != LobbyState::Idle) {
            error = "Already in a lobby";
            return false;
        }
        m_lobbyState = LobbyState::Joining;
    }

    SteamAPICall_t apiCall = m_matchmaking->JoinLobby(CSteamID(lobbyId));
    if (apiCall == k_uAPICallInvalid) {
        std::lock_guard lock(m_mutex);
        m_lobbyState = LobbyState::Idle;
        error = "Steam refused to join the lobby";
        return false;
    }

    OnlineLog::Write("Joining lobby %llu", ToULL(lobbyId));
    return true;
}

bool OnlineSession::OpenInviteDialog() {
    if (!m_ready)
        return false;

    uint64_t lobbyId;
    {
        std::lock_guard lock(m_mutex);
        if (m_lobbyState != LobbyState::InLobby)
            return false;
        lobbyId = m_lobbyId;
    }

    m_friends->ActivateGameOverlayInviteDialog(CSteamID(lobbyId));
    return true;
}

void OnlineSession::Leave() {
    if (m_ready)
        LeaveLobby("Left", false);
}

void OnlineSession::LeaveLobby(const char *reason, bool notify) {
    uint64_t lobbyId;
    uint64_t peerId;
    {
        std::lock_guard lock(m_mutex);
        if (notify && m_lobbyState != LobbyState::Idle)
            PushEventLocked(MakeEvent(OnlineEventType::LobbyLeft, 0, {}, reason));

        lobbyId = m_lobbyId;
        peerId = m_peerSteamId;

        m_lobbyState = LobbyState::Idle;
        m_lobbyId = 0;
        m_isHost = false;
        m_peerSteamId = 0;
        m_peerName.clear();
        m_peerModVersion.clear();
        m_connection = ConnectionState::None;
        m_pingMs = -1;
        m_usingRelay = false;
    }

    m_lobbyCreatedResult.Cancel(m_steam);

    if (lobbyId != 0) {
        m_matchmaking->LeaveLobby(CSteamID(lobbyId));
        m_friends->SetRichPresence("connect", "");
        OnlineLog::Write("Left lobby %llu: %s", ToULL(lobbyId), reason);
    }

    if (peerId != 0)
        m_networking->CloseP2PSessionWithUser(CSteamID(peerId));
}

bool OnlineSession::Send(const void *data, size_t size, bool reliable, std::string &error) {
    if (!m_ready) {
        error = "Steam is not ready";
        return false;
    }

    if (size > (reliable ? MAX_RELIABLE_SIZE : MAX_UNRELIABLE_SIZE)) {
        error = "The message is too big";
        return false;
    }

    uint64_t peerId;
    {
        std::lock_guard lock(m_mutex);
        peerId = m_connection == ConnectionState::VersionMismatch ? 0 : m_peerSteamId;
    }

    if (peerId == 0) {
        error = "Nobody to send the message to";
        return false;
    }

    if (!m_networking->SendP2PPacket(CSteamID(peerId), data, static_cast<uint32_t>(size),
                                     reliable ? k_EP2PSendReliable : k_EP2PSendUnreliable, GAME_CHANNEL)) {
        error = "Steam failed to send the message";
        return false;
    }

    return true;
}

std::vector<OnlineEvent> OnlineSession::PollEvents() {
    std::vector<OnlineEvent> events;
    size_t dropped;
    {
        std::lock_guard lock(m_mutex);
        events.assign(std::make_move_iterator(m_events.begin()), std::make_move_iterator(m_events.end()));
        m_events.clear();
        dropped = m_droppedEvents;
        m_droppedEvents = 0;
    }

    if (dropped != 0)
        OnlineLog::Write("Dropped %zu events because the scripts didn't read them in time", dropped);

    return events;
}

OnlineStatus OnlineSession::GetStatus() {
    OnlineStatus status;
    status.modVersion = HADES_COOP_VERSION;
    status.desiredAppId = m_desiredAppId;
    status.ready = m_ready;

    if (status.ready) {
        status.appId = m_appId;
        status.ownsSteamInit = m_ownsSteamInit;
        status.localSteamId = m_localSteamId;
        const char *name = m_friends->GetPersonaName();
        status.localName = name ? name : "";
        status.overlayEnabled = m_utils->IsOverlayEnabled();
    }

    std::lock_guard lock(m_mutex);
    status.error = m_error;
    status.appIdOverride = m_appIdOverride;
    status.lobby = m_lobbyState;
    status.lobbyId = m_lobbyId;
    status.isHost = m_isHost;
    status.peerSteamId = m_peerSteamId;
    status.peerName = m_peerName;
    status.connection = m_connection;
    status.peerModVersion = m_peerModVersion;
    status.pingMs = m_pingMs;
    status.usingRelay = m_usingRelay;
    return status;
}

std::vector<FriendLobby> OnlineSession::GetFriendLobbies() {
    std::vector<FriendLobby> lobbies;
    if (!m_ready)
        return lobbies;

    int count = m_friends->GetFriendCount(k_EFriendFlagImmediate);
    for (int i = 0; i < count; i++) {
        CSteamID friendId = m_friends->GetFriendByIndex(i, k_EFriendFlagImmediate);

        FriendGameInfo_t info{};
        if (!m_friends->GetFriendGamePlayed(friendId, &info))
            continue;

        if (info.AppID() != m_appId || !info.m_steamIDLobby.IsValid())
            continue;

        lobbies.push_back(
            {friendId.ConvertToUint64(), GetName(friendId.ConvertToUint64()), info.m_steamIDLobby.ConvertToUint64()});
    }

    return lobbies;
}

void OnlineSession::EnterLobby(uint64_t lobbyId) {
    CSteamID lobby(lobbyId);

    bool expected;
    {
        std::lock_guard lock(m_mutex);
        if (m_lobbyState == LobbyState::InLobby && m_lobbyId == lobbyId)
            return;
        expected = m_lobbyState == LobbyState::Creating || m_lobbyState == LobbyState::Joining;
    }

    if (!expected) {
        // A late result after Leave()
        OnlineLog::Write("Leaving lobby %llu, the player has cancelled", ToULL(lobbyId));
        m_matchmaking->LeaveLobby(lobby);
        return;
    }

    uint64_t ownerId = m_matchmaking->GetLobbyOwner(lobby).ConvertToUint64();
    bool isHost = ownerId == m_localSteamId;

    if (isHost) {
        m_matchmaking->SetLobbyData(lobby, LOBBY_KEY_MARKER, LOBBY_MARKER_VALUE);
        m_matchmaking->SetLobbyData(lobby, LOBBY_KEY_PROTOCOL, std::to_string(PROTOCOL_VERSION).c_str());
        m_matchmaking->SetLobbyData(lobby, LOBBY_KEY_VERSION, HADES_COOP_VERSION);
        // Lets friends join from the Steam friends list
        m_friends->SetRichPresence("connect", ("+connect_lobby " + std::to_string(lobbyId)).c_str());
    } else {
        const char *marker = m_matchmaking->GetLobbyData(lobby, LOBBY_KEY_MARKER);
        const char *protocol = m_matchmaking->GetLobbyData(lobby, LOBBY_KEY_PROTOCOL);
        const char *hostVersion = m_matchmaking->GetLobbyData(lobby, LOBBY_KEY_VERSION);

        bool failed = false;
        OnlineEvent failure;
        if (!marker || std::strcmp(marker, LOBBY_MARKER_VALUE) != 0) {
            failed = true;
            failure = MakeEvent(OnlineEventType::LobbyJoinFailed, 0, {}, "This lobby isn't a Hades co-op game");
            OnlineLog::Write("Leaving lobby %llu, it isn't a Hades co-op game", ToULL(lobbyId));
        } else if (!protocol || std::to_string(PROTOCOL_VERSION) != protocol) {
            failed = true;
            failure =
                MakeEvent(OnlineEventType::VersionMismatch, ownerId, GetName(ownerId), hostVersion ? hostVersion : "");
            OnlineLog::Write("Leaving lobby %llu, the host uses online protocol %s", ToULL(lobbyId),
                             protocol ? protocol : "?");
        }

        if (failed) {
            m_matchmaking->LeaveLobby(lobby);

            std::lock_guard lock(m_mutex);
            if (m_lobbyState == LobbyState::Joining)
                m_lobbyState = LobbyState::Idle;
            PushEventLocked(std::move(failure));
            return;
        }
    }

    uint64_t peerId = FindLobbyPeer(lobbyId);
    std::string peerName = peerId != 0 ? GetName(peerId) : "";

    bool stale = false;
    {
        std::lock_guard lock(m_mutex);
        // Leave() could have run while Steam was being called
        if (m_lobbyState != LobbyState::Creating && m_lobbyState != LobbyState::Joining) {
            stale = true;
        } else {
            m_lobbyState = LobbyState::InLobby;
            m_lobbyId = lobbyId;
            m_isHost = isHost;

            OnlineEvent event = MakeEvent(OnlineEventType::LobbyJoined);
            event.lobbyId = lobbyId;
            event.flag = isHost;
            PushEventLocked(std::move(event));

            if (peerId != 0)
                SetPeerLocked(peerId, peerName);
        }
    }

    if (stale) {
        m_matchmaking->LeaveLobby(lobby);
        if (isHost)
            m_friends->SetRichPresence("connect", "");
        return;
    }

    if (peerId != 0)
        m_networking->AcceptP2PSessionWithUser(CSteamID(peerId));

    OnlineLog::Write("Entered lobby %llu as %s", ToULL(lobbyId), isHost ? "host" : "guest");
}

void OnlineSession::HandleJoinRequest(uint64_t lobbyId, uint64_t friendId) {
    std::string friendName = GetName(friendId);
    OnlineLog::Write("Steam asks to join lobby %llu (via %s)", ToULL(lobbyId), friendName.c_str());

    {
        std::lock_guard lock(m_mutex);
        if (m_lobbyState == LobbyState::InLobby && m_lobbyId == lobbyId)
            return;

        if (m_lobbyState != LobbyState::Idle) {
            PushEventLocked(MakeEvent(OnlineEventType::InviteIgnored, friendId, friendName));
            return;
        }
    }

    std::string error;
    if (!Join(lobbyId, error))
        OnlineLog::Write("Cannot join the lobby: %s", error.c_str());
}

bool OnlineSession::IsLobbyMember(uint64_t lobbyId, uint64_t steamId) {
    CSteamID lobby(lobbyId);
    int count = m_matchmaking->GetNumLobbyMembers(lobby);
    for (int i = 0; i < count; i++) {
        if (m_matchmaking->GetLobbyMemberByIndex(lobby, i).ConvertToUint64() == steamId)
            return true;
    }
    return false;
}

uint64_t OnlineSession::FindLobbyPeer(uint64_t lobbyId) {
    CSteamID lobby(lobbyId);
    int count = m_matchmaking->GetNumLobbyMembers(lobby);
    for (int i = 0; i < count; i++) {
        uint64_t member = m_matchmaking->GetLobbyMemberByIndex(lobby, i).ConvertToUint64();
        if (member != m_localSteamId)
            return member;
    }
    return 0;
}

std::string OnlineSession::GetName(uint64_t steamId) {
    if (steamId == 0)
        return {};

    const char *name = m_friends->GetFriendPersonaName(CSteamID(steamId));
    return name ? name : "";
}

void OnlineSession::SetPeerLocked(uint64_t steamId, const std::string &name) {
    m_peerSteamId = steamId;
    m_peerName = name;
    m_peerModVersion.clear();
    m_connection = ConnectionState::Handshaking;
    m_lastReceiveUs = OnlineUtils::GetTimeUs();
    m_nextHelloUs = 0;
    m_pingMs = -1;
    m_usingRelay = false;

    PushEventLocked(MakeEvent(OnlineEventType::PeerJoined, steamId, name));
}

void OnlineSession::PushEventLocked(OnlineEvent &&event) {
    if (m_events.size() >= MAX_QUEUED_EVENTS) {
        m_events.pop_front();
        m_droppedEvents++;
    }
    m_events.push_back(std::move(event));
}

void OnlineSession::NetThreadMain() {
    std::vector<uint8_t> buffer;
    while (!m_stopNetThread) {
        // The game pumps callbacks itself when it owns Steam
        if (m_ownsSteamInit)
            m_steam.RunCallbacks();

        bool hasPeer;
        {
            std::lock_guard lock(m_mutex);
            hasPeer = m_peerSteamId != 0;
        }

        // Steam keeps packets queued until there is a peer to read them for
        if (hasPeer) {
            ReceivePackets(buffer);
            UpdateConnection();
        }

        std::this_thread::sleep_for(hasPeer ? NET_THREAD_INTERVAL : NET_THREAD_IDLE_INTERVAL);
    }
}

void OnlineSession::ReceivePackets(std::vector<uint8_t> &buffer) {
    for (int channel : {GAME_CHANNEL, CONTROL_CHANNEL}) {
        uint32_t size = 0;
        for (int i = 0; i < MAX_PACKETS_PER_UPDATE && m_networking->IsP2PPacketAvailable(&size, channel); i++) {
            buffer.resize(size > 0 ? size : 1);

            uint32_t received = 0;
            CSteamID sender;
            if (!m_networking->ReadP2PPacket(buffer.data(), static_cast<uint32_t>(buffer.size()), &received, &sender,
                                             channel))
                break;

            if (channel == CONTROL_CHANNEL)
                HandleControlPacket(sender.ConvertToUint64(), buffer.data(), received);
            else
                HandleGamePacket(sender.ConvertToUint64(), buffer.data(), received);
        }
    }
}

void OnlineSession::HandleControlPacket(uint64_t sender, const uint8_t *data, size_t size) {
    ByteReader reader(data, size);
    ControlType type;
    if (!DecodeType(reader, type))
        return;

    std::vector<uint8_t> reply;
    {
        std::lock_guard lock(m_mutex);
        if (sender == 0 || sender != m_peerSteamId)
            return;

        uint64_t now = OnlineUtils::GetTimeUs();
        m_lastReceiveUs = now;

        switch (type) {
        case ControlType::Hello: {
            Hello hello;
            if (!DecodeHello(reader, hello))
                return;

            m_peerModVersion = hello.modVersion;

            if (hello.needsReply)
                reply = EncodeHello({PROTOCOL_VERSION, HADES_COOP_VERSION, false});

            if (hello.protocolVersion != PROTOCOL_VERSION) {
                if (m_connection != ConnectionState::VersionMismatch) {
                    m_connection = ConnectionState::VersionMismatch;
                    PushEventLocked(MakeEvent(OnlineEventType::VersionMismatch, sender, m_peerName, hello.modVersion));
                    OnlineLog::Write("%s uses online protocol %u, this game uses %u", m_peerName.c_str(),
                                     hello.protocolVersion, PROTOCOL_VERSION);
                }
            } else if (m_connection != ConnectionState::Connected) {
                m_connection = ConnectionState::Connected;
                m_nextPingUs = now;
                m_pingMs = -1;
                PushEventLocked(MakeEvent(OnlineEventType::Connected, sender, m_peerName, hello.modVersion));
                OnlineLog::Write("Connected to %s (mod %s)", m_peerName.c_str(), hello.modVersion.c_str());
            }
            break;
        }
        case ControlType::Ping: {
            Ping ping;
            if (!DecodePing(reader, ping))
                return;

            reply = EncodePing(ControlType::Pong, ping);
            break;
        }
        case ControlType::Pong: {
            Ping pong;
            if (!DecodePing(reader, pong))
                return;

            // The pong carries our own send time
            if (pong.sentTimeUs <= now && now - pong.sentTimeUs <= MAX_PING_AGE_US) {
                int rttMs = static_cast<int>((now - pong.sentTimeUs) / 1000);
                m_pingMs = m_pingMs < 0 ? rttMs : (m_pingMs * 3 + rttMs) / 4;
            }
            break;
        }
        }
    }

    if (!reply.empty())
        SendControl(sender, reply, type == ControlType::Hello);
}

void OnlineSession::HandleGamePacket(uint64_t sender, const uint8_t *data, size_t size) {
    std::lock_guard lock(m_mutex);
    if (sender == 0 || sender != m_peerSteamId || m_connection == ConnectionState::VersionMismatch)
        return;

    m_lastReceiveUs = OnlineUtils::GetTimeUs();

    OnlineEvent event = MakeEvent(OnlineEventType::Message);
    event.data.assign(reinterpret_cast<const char *>(data), size);
    PushEventLocked(std::move(event));
}

void OnlineSession::UpdateConnection() {
    uint64_t now = OnlineUtils::GetTimeUs();

    uint64_t peerId;
    bool sendHello = false;
    bool sendPing = false;
    bool querySessionState = false;
    Ping ping;
    {
        std::lock_guard lock(m_mutex);
        peerId = m_peerSteamId;
        if (peerId == 0)
            return;

        if (m_connection == ConnectionState::Handshaking && now >= m_nextHelloUs) {
            sendHello = true;
            m_nextHelloUs = now + HELLO_INTERVAL_US;
        }

        if (m_connection == ConnectionState::Connected) {
            if (now > m_lastReceiveUs && now - m_lastReceiveUs > CONNECTION_TIMEOUT_US) {
                m_connection = ConnectionState::Handshaking;
                m_nextHelloUs = now;
                m_pingMs = -1;
                PushEventLocked(MakeEvent(OnlineEventType::ConnectionLost, peerId, m_peerName, "Timed out"));
                OnlineLog::Write("Connection to %s timed out", m_peerName.c_str());
            } else if (now >= m_nextPingUs) {
                sendPing = true;
                ping.sequence = ++m_pingSequence;
                ping.sentTimeUs = now;
                m_nextPingUs = now + PING_INTERVAL_US;
            }
        }

        if (now >= m_nextSessionStateUs) {
            querySessionState = true;
            m_nextSessionStateUs = now + SESSION_STATE_INTERVAL_US;
        }
    }

    if (sendHello)
        SendControl(peerId, EncodeHello({PROTOCOL_VERSION, HADES_COOP_VERSION, true}), true);

    if (sendPing)
        SendControl(peerId, EncodePing(ControlType::Ping, ping), false);

    if (querySessionState) {
        P2PSessionState_t state{};
        if (m_networking->GetP2PSessionState(CSteamID(peerId), &state)) {
            std::lock_guard lock(m_mutex);
            if (m_peerSteamId == peerId)
                m_usingRelay = state.m_bUsingRelay != 0;
        }
    }
}

void OnlineSession::SendControl(uint64_t steamId, const std::vector<uint8_t> &packet, bool reliable) {
    m_networking->SendP2PPacket(CSteamID(steamId), packet.data(), static_cast<uint32_t>(packet.size()),
                                reliable ? k_EP2PSendReliable : k_EP2PSendUnreliable, CONTROL_CHANNEL);
}

void OnlineSession::OnLobbyCreated(LobbyCreated_t *param, bool ioFailure) {
    if (ioFailure || param->m_eResult != k_EResultOK) {
        int result = ioFailure ? 0 : static_cast<int>(param->m_eResult);
        OnlineLog::Write("Cannot create a lobby, Steam result %d", result);

        std::string reason = result == 0 ? "Lost connection to Steam" : "Steam error " + std::to_string(result);
        std::lock_guard lock(m_mutex);
        if (m_lobbyState == LobbyState::Creating)
            m_lobbyState = LobbyState::Idle;
        PushEventLocked(MakeEvent(OnlineEventType::LobbyCreateFailed, 0, {}, reason));
        return;
    }

    // LobbyEnter_t follows and does the same. Handle it here in case it comes late.
    EnterLobby(param->m_ulSteamIDLobby);
}

void OnlineSession::OnLobbyEnter(LobbyEnter_t *param, bool) {
    if (param->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
        const char *reason = LobbyEnterResponseToString(param->m_EChatRoomEnterResponse);
        OnlineLog::Write("Cannot join lobby %llu: %s", ToULL(param->m_ulSteamIDLobby), reason);

        std::lock_guard lock(m_mutex);
        if (m_lobbyState == LobbyState::Joining)
            m_lobbyState = LobbyState::Idle;
        PushEventLocked(MakeEvent(OnlineEventType::LobbyJoinFailed, 0, {}, reason));
        return;
    }

    EnterLobby(param->m_ulSteamIDLobby);
}

void OnlineSession::OnLobbyChatUpdate(LobbyChatUpdate_t *param, bool) {
    uint64_t userId = param->m_ulSteamIDUserChanged;
    if (userId == m_localSteamId)
        return;

    const uint32_t change = param->m_rgfChatMemberStateChange;
    const uint32_t removed = k_EChatMemberStateChangeLeft | k_EChatMemberStateChangeDisconnected |
                             k_EChatMemberStateChangeKicked | k_EChatMemberStateChangeBanned;

    std::string name = GetName(userId);
    bool acceptSession = false;
    bool closeSession = false;
    bool hostLeft = false;
    {
        std::lock_guard lock(m_mutex);
        if (m_lobbyState != LobbyState::InLobby || m_lobbyId != param->m_ulSteamIDLobby)
            return;

        if ((change & k_EChatMemberStateChangeEntered) && m_peerSteamId == 0) {
            SetPeerLocked(userId, name);
            acceptSession = true;
        } else if ((change & removed) && userId == m_peerSteamId) {
            PushEventLocked(MakeEvent(OnlineEventType::PeerLeft, userId, name));
            m_peerSteamId = 0;
            m_peerName.clear();
            m_peerModVersion.clear();
            m_connection = ConnectionState::None;
            m_pingMs = -1;
            m_usingRelay = false;
            closeSession = true;
            hostLeft = !m_isHost;
        }
    }

    if (acceptSession) {
        m_networking->AcceptP2PSessionWithUser(CSteamID(userId));
        OnlineLog::Write("%s joined the lobby", name.c_str());
    }

    if (closeSession) {
        m_networking->CloseP2PSessionWithUser(CSteamID(userId));
        OnlineLog::Write("%s left the lobby", name.c_str());
    }

    // Steam would make the guest the new owner. The game can't go on without the host.
    if (hostLeft)
        LeaveLobby("HostLeft", true);
}

void OnlineSession::OnLobbyKicked(LobbyKicked_t *param, bool) {
    bool currentLobby;
    {
        std::lock_guard lock(m_mutex);
        currentLobby = m_lobbyId == param->m_ulSteamIDLobby;
    }

    if (currentLobby)
        LeaveLobby(param->m_bKickedDueToDisconnect ? "LostSteamConnection" : "Kicked", true);
}

void OnlineSession::OnGameLobbyJoinRequested(GameLobbyJoinRequested_t *param, bool) {
    HandleJoinRequest(param->m_steamIDLobby.ConvertToUint64(), param->m_steamIDFriend.ConvertToUint64());
}

void OnlineSession::OnRichPresenceJoinRequested(GameRichPresenceJoinRequested_t *param, bool) {
    char connect[k_cchMaxRichPresenceValueLength + 1] = {};
    std::memcpy(connect, param->m_rgchConnect, k_cchMaxRichPresenceValueLength);

    uint64_t lobbyId = OnlineUtils::ParseConnectLobby(connect);
    if (lobbyId != 0)
        HandleJoinRequest(lobbyId, param->m_steamIDFriend.ConvertToUint64());
}

void OnlineSession::OnP2PSessionRequest(P2PSessionRequest_t *param, bool) {
    uint64_t remoteId = param->m_steamIDRemote.ConvertToUint64();

    uint64_t lobbyId;
    bool isPeer;
    {
        std::lock_guard lock(m_mutex);
        lobbyId = m_lobbyState == LobbyState::InLobby ? m_lobbyId : 0;
        isPeer = remoteId != 0 && remoteId == m_peerSteamId;
    }

    // Only lobby members can talk to this game
    if (isPeer || (lobbyId != 0 && IsLobbyMember(lobbyId, remoteId))) {
        m_networking->AcceptP2PSessionWithUser(param->m_steamIDRemote);
        OnlineLog::Write("Accepted P2P session with %llu", ToULL(remoteId));
    } else {
        OnlineLog::Write("Ignored P2P session request from %llu, not a lobby member", ToULL(remoteId));
    }
}

void OnlineSession::OnP2PSessionConnectFail(P2PSessionConnectFail_t *param, bool) {
    uint64_t remoteId = param->m_steamIDRemote.ConvertToUint64();
    const char *reason = P2PErrorToString(param->m_eP2PSessionError);
    OnlineLog::Write("P2P connection to %llu failed: %s (%u)", ToULL(remoteId), reason, param->m_eP2PSessionError);

    std::lock_guard lock(m_mutex);
    if (remoteId == 0 || remoteId != m_peerSteamId)
        return;

    PushEventLocked(MakeEvent(OnlineEventType::ConnectionLost, remoteId, m_peerName, reason));
    if (m_connection == ConnectionState::Connected) {
        m_connection = ConnectionState::Handshaking;
        m_nextHelloUs = 0;
        m_pingMs = -1;
    }
}
