//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#include "LuaOnlineFunctionDefs.h"
#include "lua.hpp"
#include "online/OnlineLog.h"
#include "online/OnlineSession.h"
#include "pch.h"

#include <cstdlib>

// Steam IDs don't fit into Lua numbers, they are passed as strings

static void SetField(lua_State *L, const char *key, bool value) {
    lua_pushboolean(L, value);
    lua_setfield(L, -2, key);
}

static void SetField(lua_State *L, const char *key, const char *value) {
    lua_pushstring(L, value);
    lua_setfield(L, -2, key);
}

static void SetField(lua_State *L, const char *key, const std::string &value) {
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, key);
}

static void SetField(lua_State *L, const char *key, double value) {
    lua_pushnumber(L, value);
    lua_setfield(L, -2, key);
}

static void SetSteamIdField(lua_State *L, const char *key, uint64_t steamId) {
    if (steamId != 0)
        SetField(L, key, std::to_string(steamId));
}

static int PushResult(lua_State *L, bool success, const std::string &error) {
    lua_pushboolean(L, success);
    if (success)
        return 1;

    lua_pushstring(L, error.c_str());
    return 2;
}

static const char *ToString(LobbyState state) {
    switch (state) {
    case LobbyState::Creating:
        return "Creating";
    case LobbyState::Joining:
        return "Joining";
    case LobbyState::InLobby:
        return "InLobby";
    default:
        return "Idle";
    }
}

static const char *ToString(ConnectionState state) {
    switch (state) {
    case ConnectionState::Handshaking:
        return "Handshaking";
    case ConnectionState::Connected:
        return "Connected";
    case ConnectionState::VersionMismatch:
        return "VersionMismatch";
    default:
        return "None";
    }
}

static const char *ToString(OnlineEventType type) {
    switch (type) {
    case OnlineEventType::LobbyCreateFailed:
        return "LobbyCreateFailed";
    case OnlineEventType::LobbyJoined:
        return "LobbyJoined";
    case OnlineEventType::LobbyJoinFailed:
        return "LobbyJoinFailed";
    case OnlineEventType::LobbyLeft:
        return "LobbyLeft";
    case OnlineEventType::PeerJoined:
        return "PeerJoined";
    case OnlineEventType::PeerLeft:
        return "PeerLeft";
    case OnlineEventType::Connected:
        return "Connected";
    case OnlineEventType::VersionMismatch:
        return "VersionMismatch";
    case OnlineEventType::ConnectionLost:
        return "ConnectionLost";
    case OnlineEventType::InviteIgnored:
        return "InviteIgnored";
    default:
        return "Message";
    }
}

// bool, string? CoopOnlineInit()
static int CoopOnlineInit(lua_State *L) {
    std::string error;
    bool success = OnlineSession::Instance().EnsureReady(error);
    return PushResult(L, success, error);
}

// table CoopOnlineGetState()
static int CoopOnlineGetState(lua_State *L) {
    OnlineStatus status = OnlineSession::Instance().GetStatus();

    lua_createtable(L, 0, 24);
    SetField(L, "Ready", status.ready);
    SetField(L, "Error", status.error);
    SetField(L, "ModVersion", status.modVersion);
    SetField(L, "AppId", static_cast<double>(status.appId));
    SetField(L, "ExpectedAppId", static_cast<double>(status.desiredAppId));
    SetField(L, "AppIdOverride", status.appIdOverride);
    SetField(L, "OwnsSteamInit", status.ownsSteamInit);
    SetField(L, "OverlayEnabled", status.overlayEnabled);
    SetField(L, "LocalName", status.localName);
    SetSteamIdField(L, "LocalSteamId", status.localSteamId);
    SetField(L, "Lobby", ToString(status.lobby));
    SetSteamIdField(L, "LobbyId", status.lobbyId);
    SetField(L, "IsHost", status.isHost);
    SetSteamIdField(L, "PeerSteamId", status.peerSteamId);
    SetField(L, "PeerName", status.peerName);
    SetField(L, "Connection", ToString(status.connection));
    SetField(L, "PeerModVersion", status.peerModVersion);
    if (status.pingMs >= 0)
        SetField(L, "Ping", static_cast<double>(status.pingMs));
    SetField(L, "UsingRelay", status.usingRelay);
    return 1;
}

// bool, string? CoopOnlineHost(bool openInviteDialog = false)
static int CoopOnlineHost(lua_State *L) {
    bool openInviteDialog = lua_toboolean(L, 1) != 0;

    std::string error;
    bool success = OnlineSession::Instance().Host(error, openInviteDialog);
    return PushResult(L, success, error);
}

// bool, string? CoopOnlineJoin(string lobbyId)
static int CoopOnlineJoin(lua_State *L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        return luaL_error(L, "Argument 1 must be a string");
    }

    uint64_t lobbyId = std::strtoull(lua_tolstring(L, 1, nullptr), nullptr, 10);

    std::string error;
    bool success = OnlineSession::Instance().Join(lobbyId, error);
    return PushResult(L, success, error);
}

// bool CoopOnlineInvite()
static int CoopOnlineInvite(lua_State *L) {
    lua_pushboolean(L, OnlineSession::Instance().OpenInviteDialog());
    return 1;
}

// CoopOnlineLeave()
static int CoopOnlineLeave(lua_State *L) {
    OnlineSession::Instance().Leave();
    return 0;
}

// bool, string? CoopOnlineSend(string data, bool reliable = true)
static int CoopOnlineSend(lua_State *L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        return luaL_error(L, "Argument 1 must be a string");
    }

    size_t size = 0;
    const char *data = lua_tolstring(L, 1, &size);
    bool reliable = lua_type(L, 2) <= LUA_TNIL || lua_toboolean(L, 2);

    std::string error;
    bool success = OnlineSession::Instance().Send(data, size, reliable, error);
    return PushResult(L, success, error);
}

// table CoopOnlinePoll()
static int CoopOnlinePoll(lua_State *L) {
    std::vector<OnlineEvent> events = OnlineSession::Instance().PollEvents();

    lua_createtable(L, static_cast<int>(events.size()), 0);
    int index = 1;
    for (const OnlineEvent &event : events) {
        lua_createtable(L, 0, 4);
        SetField(L, "Type", ToString(event.type));

        switch (event.type) {
        case OnlineEventType::LobbyCreateFailed:
        case OnlineEventType::LobbyJoinFailed:
        case OnlineEventType::LobbyLeft:
            SetField(L, "Reason", event.text);
            break;
        case OnlineEventType::LobbyJoined:
            SetSteamIdField(L, "LobbyId", event.lobbyId);
            SetField(L, "IsHost", event.flag);
            break;
        case OnlineEventType::PeerJoined:
        case OnlineEventType::PeerLeft:
        case OnlineEventType::InviteIgnored:
            SetSteamIdField(L, "SteamId", event.steamId);
            SetField(L, "Name", event.name);
            break;
        case OnlineEventType::Connected:
        case OnlineEventType::VersionMismatch:
            SetSteamIdField(L, "SteamId", event.steamId);
            SetField(L, "Name", event.name);
            SetField(L, "ModVersion", event.text);
            break;
        case OnlineEventType::ConnectionLost:
            SetSteamIdField(L, "SteamId", event.steamId);
            SetField(L, "Name", event.name);
            SetField(L, "Reason", event.text);
            break;
        case OnlineEventType::Message:
            SetField(L, "Data", event.data);
            break;
        }

        lua_rawseti(L, -2, index++);
    }

    return 1;
}

// table CoopOnlineGetFriendLobbies()
static int CoopOnlineGetFriendLobbies(lua_State *L) {
    std::vector<FriendLobby> lobbies = OnlineSession::Instance().GetFriendLobbies();

    lua_createtable(L, static_cast<int>(lobbies.size()), 0);
    int index = 1;
    for (const FriendLobby &lobby : lobbies) {
        lua_createtable(L, 0, 3);
        SetSteamIdField(L, "SteamId", lobby.steamId);
        SetField(L, "Name", lobby.name);
        SetSteamIdField(L, "LobbyId", lobby.lobbyId);
        lua_rawseti(L, -2, index++);
    }

    return 1;
}

// CoopOnlineLog(string text)
static int CoopOnlineLog(lua_State *L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        return luaL_error(L, "Argument 1 must be a string");
    }

    OnlineLog::Write("Script: %s", lua_tolstring(L, 1, nullptr));
    return 0;
}

void LuaOnlineFunctionDefs::Load(lua_State *L) {
#define REGISTER(fun) lua_register(L, #fun, fun)

    REGISTER(CoopOnlineInit);
    REGISTER(CoopOnlineGetState);
    REGISTER(CoopOnlineHost);
    REGISTER(CoopOnlineJoin);
    REGISTER(CoopOnlineInvite);
    REGISTER(CoopOnlineLeave);
    REGISTER(CoopOnlineSend);
    REGISTER(CoopOnlinePoll);
    REGISTER(CoopOnlineGetFriendLobbies);
    REGISTER(CoopOnlineLog);

#undef REGISTER
}
