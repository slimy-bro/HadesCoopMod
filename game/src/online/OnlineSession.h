//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "NetProtocol.h"
#include "SteamApi.h"

enum class OnlineEventType {
    LobbyCreateFailed, // text: reason
    LobbyJoined,       // lobbyId, flag: is host
    LobbyJoinFailed,   // text: reason
    LobbyLeft,         // text: reason
    PeerJoined,        // steamId, name
    PeerLeft,          // steamId, name
    Connected,         // steamId, name, text: peer mod version
    VersionMismatch,   // steamId, name, text: peer mod version
    ConnectionLost,    // steamId, name, text: reason
    InviteIgnored,     // steamId, name: the friend who invited
    Message,           // data
};

struct OnlineEvent {
    OnlineEventType type = OnlineEventType::Message;
    uint64_t steamId = 0;
    uint64_t lobbyId = 0;
    std::string name;
    std::string text;
    bool flag = false;
    std::string data;
};

enum class LobbyState { Idle, Creating, Joining, InLobby };
enum class ConnectionState { None, Handshaking, Connected, VersionMismatch };

struct OnlineStatus {
    bool ready = false;
    std::string error;
    std::string modVersion;
    uint32_t appId = 0;
    uint32_t desiredAppId = 0;
    // How the Steam AppID override went: Applied, Disabled, TooLate, SkippedAfterRelaunch
    std::string appIdOverride;
    bool ownsSteamInit = false;
    bool overlayEnabled = false;
    std::string localName;
    uint64_t localSteamId = 0;
    LobbyState lobby = LobbyState::Idle;
    uint64_t lobbyId = 0;
    bool isHost = false;
    uint64_t peerSteamId = 0;
    std::string peerName;
    ConnectionState connection = ConnectionState::None;
    std::string peerModVersion;
    int pingMs = -1;
    bool usingRelay = false;
};

struct FriendLobby {
    uint64_t steamId;
    std::string name;
    uint64_t lobbyId;
};

// Steam lobby + P2P connection between two games.
//
// Steam callbacks can run on the game's thread or on the network thread, so all state is guarded by m_mutex.
// Steam is never called while m_mutex is held to avoid lock order issues with steam_api's own locks.
class OnlineSession {
  public:
    // Spacewar, the public Steamworks test app
    static constexpr uint32_t DEFAULT_APP_ID = 480;

    static OnlineSession &Instance();

    // Called from HadesModInit. Points the game to the configured Steam AppID if Steam isn't initialized yet.
    void PrepareEarly();
    // Called when the game creates its Lua state, the game has finished its startup by then
    void OnLuaCreated();

    bool EnsureReady(std::string &error);
    void Shutdown();

    bool Host(std::string &error);
    bool Join(uint64_t lobbyId, std::string &error);
    bool OpenInviteDialog();
    void Leave();

    bool Send(const void *data, size_t size, bool reliable, std::string &error);
    std::vector<OnlineEvent> PollEvents();
    OnlineStatus GetStatus();
    std::vector<FriendLobby> GetFriendLobbies();

  private:
    OnlineSession();

    void EnterLobby(uint64_t lobbyId);
    void LeaveLobby(const char *reason, bool notify);
    void HandleJoinRequest(uint64_t lobbyId, uint64_t friendId);
    bool IsLobbyMember(uint64_t lobbyId, uint64_t steamId);
    uint64_t FindLobbyPeer(uint64_t lobbyId);
    std::string GetName(uint64_t steamId);

    // Callers must hold m_mutex
    void SetPeerLocked(uint64_t steamId, const std::string &name);
    void PushEventLocked(OnlineEvent &&event);

    void NetThreadMain();
    void ReceivePackets(std::vector<uint8_t> &buffer);
    void HandleControlPacket(uint64_t sender, const uint8_t *data, size_t size);
    void HandleGamePacket(uint64_t sender, const uint8_t *data, size_t size);
    void UpdateConnection();
    void SendControl(uint64_t steamId, const std::vector<uint8_t> &packet, bool reliable);

    void OnLobbyCreated(Steam::LobbyCreated_t *param, bool ioFailure);
    void OnLobbyEnter(Steam::LobbyEnter_t *param, bool ioFailure);
    void OnLobbyChatUpdate(Steam::LobbyChatUpdate_t *param, bool ioFailure);
    void OnLobbyKicked(Steam::LobbyKicked_t *param, bool ioFailure);
    void OnGameLobbyJoinRequested(Steam::GameLobbyJoinRequested_t *param, bool ioFailure);
    void OnRichPresenceJoinRequested(Steam::GameRichPresenceJoinRequested_t *param, bool ioFailure);
    void OnP2PSessionRequest(Steam::P2PSessionRequest_t *param, bool ioFailure);
    void OnP2PSessionConnectFail(Steam::P2PSessionConnectFail_t *param, bool ioFailure);

    Steam::SteamApi m_steam;
    Steam::ISteamUser *m_user = nullptr;
    Steam::ISteamUtils *m_utils = nullptr;
    Steam::ISteamFriends *m_friends = nullptr;
    Steam::ISteamMatchmaking *m_matchmaking = nullptr;
    Steam::ISteamNetworking *m_networking = nullptr;

    Steam::SteamCallResult<Steam::LobbyCreated_t, OnlineSession> m_lobbyCreatedResult;
    Steam::SteamCallback<Steam::LobbyEnter_t, OnlineSession> m_lobbyEnterCallback;
    Steam::SteamCallback<Steam::LobbyChatUpdate_t, OnlineSession> m_lobbyChatUpdateCallback;
    Steam::SteamCallback<Steam::LobbyKicked_t, OnlineSession> m_lobbyKickedCallback;
    Steam::SteamCallback<Steam::GameLobbyJoinRequested_t, OnlineSession> m_gameLobbyJoinRequestedCallback;
    Steam::SteamCallback<Steam::GameRichPresenceJoinRequested_t, OnlineSession> m_richPresenceJoinRequestedCallback;
    Steam::SteamCallback<Steam::P2PSessionRequest_t, OnlineSession> m_p2pSessionRequestCallback;
    Steam::SteamCallback<Steam::P2PSessionConnectFail_t, OnlineSession> m_p2pSessionConnectFailCallback;

    std::thread m_netThread;
    std::atomic<bool> m_stopNetThread = false;
    std::atomic<bool> m_ready = false;

    // Set before the network thread starts, read only afterwards
    std::wstring m_modDirectory;
    uint32_t m_desiredAppId = DEFAULT_APP_ID;
    uint32_t m_appId = 0;
    uint64_t m_localSteamId = 0;
    bool m_ownsSteamInit = false;

    std::mutex m_mutex;
    std::string m_error;
    std::string m_appIdOverride;
    std::deque<OnlineEvent> m_events;
    size_t m_droppedEvents = 0;

    LobbyState m_lobbyState = LobbyState::Idle;
    uint64_t m_lobbyId = 0;
    bool m_isHost = false;

    uint64_t m_peerSteamId = 0;
    std::string m_peerName;
    std::string m_peerModVersion;
    ConnectionState m_connection = ConnectionState::None;
    uint64_t m_lastReceiveUs = 0;
    uint64_t m_nextHelloUs = 0;
    uint64_t m_nextPingUs = 0;
    uint64_t m_nextSessionStateUs = 0;
    uint32_t m_pingSequence = 0;
    int m_pingMs = -1;
    bool m_usingRelay = false;
};
