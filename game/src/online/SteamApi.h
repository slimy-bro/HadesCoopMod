//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <windows.h>

// Minimal Steamworks declarations.
//
// The mod loads steam_api64.dll at runtime instead of linking the Steamworks SDK.
// The declarations below mirror the SDK's binary interface for the exact interface
// versions requested in SteamApi::*_VERSION. Each version string pins a vtable layout,
// so the virtual methods must stay in this order even if the mod doesn't call them.
// Only methods up to the last one the mod uses are declared.
namespace Steam {

using HSteamUser = int32_t;
using HSteamPipe = int32_t;
using SteamAPICall_t = uint64_t;
using AppId_t = uint32_t;

constexpr SteamAPICall_t k_uAPICallInvalid = 0;

#pragma pack(push, 1)
class CSteamID {
  public:
    CSteamID() : m_steamId(0) {}
    explicit CSteamID(uint64_t steamId) : m_steamId(steamId) {}

    uint64_t ConvertToUint64() const noexcept { return m_steamId; }
    bool IsValid() const noexcept { return m_steamId != 0; }

    bool operator==(const CSteamID &other) const noexcept { return m_steamId == other.m_steamId; }
    bool operator!=(const CSteamID &other) const noexcept { return m_steamId != other.m_steamId; }

  private:
    uint64_t m_steamId;
};
#pragma pack(pop)
static_assert(sizeof(CSteamID) == 8, "Incorrect CSteamID size");

enum ELobbyType : int {
    k_ELobbyTypePrivate = 0,
    k_ELobbyTypeFriendsOnly = 1,
};

enum EP2PSend : int {
    k_EP2PSendUnreliable = 0,
    k_EP2PSendReliable = 2,
};

enum EResult : int {
    k_EResultOK = 1,
};

enum EChatRoomEnterResponse : uint32_t {
    k_EChatRoomEnterResponseSuccess = 1,
    k_EChatRoomEnterResponseDoesntExist = 2,
    k_EChatRoomEnterResponseNotAllowed = 3,
    k_EChatRoomEnterResponseFull = 4,
    k_EChatRoomEnterResponseError = 5,
    k_EChatRoomEnterResponseBanned = 6,
    k_EChatRoomEnterResponseLimited = 7,
    k_EChatRoomEnterResponseClanDisabled = 8,
    k_EChatRoomEnterResponseCommunityBan = 9,
    k_EChatRoomEnterResponseMemberBlockedYou = 10,
    k_EChatRoomEnterResponseYouBlockedMember = 11,
    k_EChatRoomEnterResponseRatelimitExceeded = 15,
};

enum EChatMemberStateChange : uint32_t {
    k_EChatMemberStateChangeEntered = 0x0001,
    k_EChatMemberStateChangeLeft = 0x0002,
    k_EChatMemberStateChangeDisconnected = 0x0004,
    k_EChatMemberStateChangeKicked = 0x0008,
    k_EChatMemberStateChangeBanned = 0x0010,
};

constexpr int k_EFriendFlagImmediate = 0x04;
constexpr int k_EPersonaStateOffline = 0;

constexpr int k_cchMaxRichPresenceValueLength = 256;

// Callback structs use 8 byte packing on Windows (VALVE_CALLBACK_PACK_LARGE)
#pragma pack(push, 8)

struct FriendGameInfo_t {
    uint64_t m_gameID; // CGameID
    uint32_t m_unGameIP;
    uint16_t m_usGamePort;
    uint16_t m_usQueryPort;
    CSteamID m_steamIDLobby;

    AppId_t AppID() const noexcept { return static_cast<AppId_t>(m_gameID & 0xFFFFFF); }
};
static_assert(offsetof(FriendGameInfo_t, m_steamIDLobby) == 16, "Incorrect FriendGameInfo_t layout");

struct P2PSessionState_t {
    uint8_t m_bConnectionActive;
    uint8_t m_bConnecting;
    uint8_t m_eP2PSessionError;
    uint8_t m_bUsingRelay;
    int32_t m_nBytesQueuedForSend;
    int32_t m_nPacketsQueuedForSend;
    uint32_t m_nRemoteIP;
    uint16_t m_nRemotePort;
};
static_assert(sizeof(P2PSessionState_t) == 20, "Incorrect P2PSessionState_t size");

constexpr int k_iSteamFriendsCallbacks = 300;
constexpr int k_iSteamMatchmakingCallbacks = 500;
constexpr int k_iSteamNetworkingCallbacks = 1200;

struct GameLobbyJoinRequested_t {
    enum { k_iCallback = k_iSteamFriendsCallbacks + 33 };
    CSteamID m_steamIDLobby;
    CSteamID m_steamIDFriend;
};
static_assert(sizeof(GameLobbyJoinRequested_t) == 16, "Incorrect GameLobbyJoinRequested_t size");

struct GameRichPresenceJoinRequested_t {
    enum { k_iCallback = k_iSteamFriendsCallbacks + 37 };
    CSteamID m_steamIDFriend;
    char m_rgchConnect[k_cchMaxRichPresenceValueLength];
};
static_assert(sizeof(GameRichPresenceJoinRequested_t) == 264, "Incorrect GameRichPresenceJoinRequested_t size");

struct LobbyEnter_t {
    enum { k_iCallback = k_iSteamMatchmakingCallbacks + 4 };
    uint64_t m_ulSteamIDLobby;
    uint32_t m_rgfChatPermissions;
    bool m_bLocked;
    uint32_t m_EChatRoomEnterResponse;
};
static_assert(sizeof(LobbyEnter_t) == 24, "Incorrect LobbyEnter_t size");

struct LobbyChatUpdate_t {
    enum { k_iCallback = k_iSteamMatchmakingCallbacks + 6 };
    uint64_t m_ulSteamIDLobby;
    uint64_t m_ulSteamIDUserChanged;
    uint64_t m_ulSteamIDMakingChange;
    uint32_t m_rgfChatMemberStateChange;
};
static_assert(sizeof(LobbyChatUpdate_t) == 32, "Incorrect LobbyChatUpdate_t size");

struct LobbyKicked_t {
    enum { k_iCallback = k_iSteamMatchmakingCallbacks + 12 };
    uint64_t m_ulSteamIDLobby;
    uint64_t m_ulSteamIDAdmin;
    uint8_t m_bKickedDueToDisconnect;
};
static_assert(sizeof(LobbyKicked_t) == 24, "Incorrect LobbyKicked_t size");

struct LobbyCreated_t {
    enum { k_iCallback = k_iSteamMatchmakingCallbacks + 13 };
    EResult m_eResult;
    uint64_t m_ulSteamIDLobby;
};
static_assert(sizeof(LobbyCreated_t) == 16, "Incorrect LobbyCreated_t size");

struct P2PSessionRequest_t {
    enum { k_iCallback = k_iSteamNetworkingCallbacks + 2 };
    CSteamID m_steamIDRemote;
};
static_assert(sizeof(P2PSessionRequest_t) == 8, "Incorrect P2PSessionRequest_t size");

struct P2PSessionConnectFail_t {
    enum { k_iCallback = k_iSteamNetworkingCallbacks + 3 };
    CSteamID m_steamIDRemote;
    uint8_t m_eP2PSessionError;
};
static_assert(sizeof(P2PSessionConnectFail_t) == 9, "Incorrect P2PSessionConnectFail_t size");

#pragma pack(pop)

// "SteamUser023". Only the first methods are declared, they are the same in every version.
class ISteamUser {
  public:
    virtual HSteamUser GetHSteamUser() = 0;
    virtual bool BLoggedOn() = 0;
    virtual CSteamID GetSteamID() = 0;
};

// "SteamUtils011"
class ISteamUtils {
  public:
    virtual uint32_t GetSecondsSinceAppActive() = 0;
    virtual uint32_t GetSecondsSinceComputerActive() = 0;
    virtual int GetConnectedUniverse() = 0;
    virtual uint32_t GetServerRealTime() = 0;
    virtual const char *GetIPCountry() = 0;
    virtual bool GetImageSize(int iImage, uint32_t *pnWidth, uint32_t *pnHeight) = 0;
    virtual bool GetImageRGBA(int iImage, uint8_t *pubDest, int nDestBufferSize) = 0;
    virtual uint8_t GetCurrentBatteryPower() = 0;
    virtual AppId_t GetAppID() = 0;
    virtual void SetOverlayNotificationPosition(int eNotificationPosition) = 0;
    virtual bool IsAPICallCompleted(SteamAPICall_t hSteamAPICall, bool *pbFailed) = 0;
    virtual int GetAPICallFailureReason(SteamAPICall_t hSteamAPICall) = 0;
    virtual bool GetAPICallResult(SteamAPICall_t hSteamAPICall, void *pCallback, int cubCallback, int iCallbackExpected,
                                  bool *pbFailed) = 0;
    virtual void RunFrame() = 0;
    virtual uint32_t GetIPCCallCount() = 0;
    virtual void SetWarningMessageHook(void *pFunction) = 0;
    virtual bool IsOverlayEnabled() = 0;
};

// "SteamFriends018"
class ISteamFriends {
  public:
    virtual const char *GetPersonaName() = 0;
    virtual int GetPersonaState() = 0;
    virtual int GetFriendCount(int iFriendFlags) = 0;
    virtual CSteamID GetFriendByIndex(int iFriend, int iFriendFlags) = 0;
    virtual int GetFriendRelationship(CSteamID steamIDFriend) = 0;
    virtual int GetFriendPersonaState(CSteamID steamIDFriend) = 0;
    virtual const char *GetFriendPersonaName(CSteamID steamIDFriend) = 0;
    virtual bool GetFriendGamePlayed(CSteamID steamIDFriend, FriendGameInfo_t *pFriendGameInfo) = 0;
    virtual const char *GetFriendPersonaNameHistory(CSteamID steamIDFriend, int iPersonaName) = 0;
    virtual int GetFriendSteamLevel(CSteamID steamIDFriend) = 0;
    virtual const char *GetPlayerNickname(CSteamID steamIDPlayer) = 0;
    virtual int GetFriendsGroupCount() = 0;
    virtual int16_t GetFriendsGroupIDByIndex(int iFG) = 0;
    virtual const char *GetFriendsGroupName(int16_t friendsGroupID) = 0;
    virtual int GetFriendsGroupMembersCount(int16_t friendsGroupID) = 0;
    virtual void GetFriendsGroupMembersList(int16_t friendsGroupID, CSteamID *pOutSteamIDMembers,
                                            int nMembersCount) = 0;
    virtual bool HasFriend(CSteamID steamIDFriend, int iFriendFlags) = 0;
    virtual int GetClanCount() = 0;
    virtual CSteamID GetClanByIndex(int iClan) = 0;
    virtual const char *GetClanName(CSteamID steamIDClan) = 0;
    virtual const char *GetClanTag(CSteamID steamIDClan) = 0;
    virtual bool GetClanActivityCounts(CSteamID steamIDClan, int *pnOnline, int *pnInGame, int *pnChatting) = 0;
    virtual SteamAPICall_t DownloadClanActivityCounts(CSteamID *psteamIDClans, int cClansToRequest) = 0;
    virtual int GetFriendCountFromSource(CSteamID steamIDSource) = 0;
    virtual CSteamID GetFriendFromSourceByIndex(CSteamID steamIDSource, int iFriend) = 0;
    virtual bool IsUserInSource(CSteamID steamIDUser, CSteamID steamIDSource) = 0;
    virtual void SetInGameVoiceSpeaking(CSteamID steamIDUser, bool bSpeaking) = 0;
    virtual void ActivateGameOverlay(const char *pchDialog) = 0;
    virtual void ActivateGameOverlayToUser(const char *pchDialog, CSteamID steamID) = 0;
    virtual void ActivateGameOverlayToWebPage(const char *pchURL, int eMode) = 0;
    virtual void ActivateGameOverlayToStore(AppId_t nAppID, int eFlag) = 0;
    virtual void SetPlayedWith(CSteamID steamIDUserPlayedWith) = 0;
    virtual void ActivateGameOverlayInviteDialog(CSteamID steamIDLobby) = 0;
    virtual int GetSmallFriendAvatar(CSteamID steamIDFriend) = 0;
    virtual int GetMediumFriendAvatar(CSteamID steamIDFriend) = 0;
    virtual int GetLargeFriendAvatar(CSteamID steamIDFriend) = 0;
    virtual bool RequestUserInformation(CSteamID steamIDUser, bool bRequireNameOnly) = 0;
    virtual SteamAPICall_t RequestClanOfficerList(CSteamID steamIDClan) = 0;
    virtual CSteamID GetClanOwner(CSteamID steamIDClan) = 0;
    virtual int GetClanOfficerCount(CSteamID steamIDClan) = 0;
    virtual CSteamID GetClanOfficerByIndex(CSteamID steamIDClan, int iOfficer) = 0;
    virtual bool SetRichPresence(const char *pchKey, const char *pchValue) = 0;
    virtual void ClearRichPresence() = 0;
};

// "SteamMatchMaking009"
class ISteamMatchmaking {
  public:
    virtual int GetFavoriteGameCount() = 0;
    virtual bool GetFavoriteGame(int iGame, AppId_t *pnAppID, uint32_t *pnIP, uint16_t *pnConnPort,
                                 uint16_t *pnQueryPort, uint32_t *punFlags, uint32_t *pRTime32LastPlayedOnServer) = 0;
    virtual int AddFavoriteGame(AppId_t nAppID, uint32_t nIP, uint16_t nConnPort, uint16_t nQueryPort, uint32_t unFlags,
                                uint32_t rTime32LastPlayedOnServer) = 0;
    virtual bool RemoveFavoriteGame(AppId_t nAppID, uint32_t nIP, uint16_t nConnPort, uint16_t nQueryPort,
                                    uint32_t unFlags) = 0;
    virtual SteamAPICall_t RequestLobbyList() = 0;
    virtual void AddRequestLobbyListStringFilter(const char *pchKeyToMatch, const char *pchValueToMatch,
                                                 int eComparisonType) = 0;
    virtual void AddRequestLobbyListNumericalFilter(const char *pchKeyToMatch, int nValueToMatch,
                                                    int eComparisonType) = 0;
    virtual void AddRequestLobbyListNearValueFilter(const char *pchKeyToMatch, int nValueToBeCloseTo) = 0;
    virtual void AddRequestLobbyListFilterSlotsAvailable(int nSlotsAvailable) = 0;
    virtual void AddRequestLobbyListDistanceFilter(int eLobbyDistanceFilter) = 0;
    virtual void AddRequestLobbyListResultCountFilter(int cMaxResults) = 0;
    virtual void AddRequestLobbyListCompatibleMembersFilter(CSteamID steamIDLobby) = 0;
    virtual CSteamID GetLobbyByIndex(int iLobby) = 0;
    virtual SteamAPICall_t CreateLobby(ELobbyType eLobbyType, int cMaxMembers) = 0;
    virtual SteamAPICall_t JoinLobby(CSteamID steamIDLobby) = 0;
    virtual void LeaveLobby(CSteamID steamIDLobby) = 0;
    virtual bool InviteUserToLobby(CSteamID steamIDLobby, CSteamID steamIDInvitee) = 0;
    virtual int GetNumLobbyMembers(CSteamID steamIDLobby) = 0;
    virtual CSteamID GetLobbyMemberByIndex(CSteamID steamIDLobby, int iMember) = 0;
    virtual const char *GetLobbyData(CSteamID steamIDLobby, const char *pchKey) = 0;
    virtual bool SetLobbyData(CSteamID steamIDLobby, const char *pchKey, const char *pchValue) = 0;
    virtual int GetLobbyDataCount(CSteamID steamIDLobby) = 0;
    virtual bool GetLobbyDataByIndex(CSteamID steamIDLobby, int iLobbyData, char *pchKey, int cchKeyBufferSize,
                                     char *pchValue, int cchValueBufferSize) = 0;
    virtual bool DeleteLobbyData(CSteamID steamIDLobby, const char *pchKey) = 0;
    virtual const char *GetLobbyMemberData(CSteamID steamIDLobby, CSteamID steamIDUser, const char *pchKey) = 0;
    virtual void SetLobbyMemberData(CSteamID steamIDLobby, const char *pchKey, const char *pchValue) = 0;
    virtual bool SendLobbyChatMsg(CSteamID steamIDLobby, const void *pvMsgBody, int cubMsgBody) = 0;
    virtual int GetLobbyChatEntry(CSteamID steamIDLobby, int iChatID, CSteamID *pSteamIDUser, void *pvData, int cubData,
                                  int *peChatEntryType) = 0;
    virtual bool RequestLobbyData(CSteamID steamIDLobby) = 0;
    virtual void SetLobbyGameServer(CSteamID steamIDLobby, uint32_t unGameServerIP, uint16_t unGameServerPort,
                                    CSteamID steamIDGameServer) = 0;
    virtual bool GetLobbyGameServer(CSteamID steamIDLobby, uint32_t *punGameServerIP, uint16_t *punGameServerPort,
                                    CSteamID *psteamIDGameServer) = 0;
    virtual bool SetLobbyMemberLimit(CSteamID steamIDLobby, int cMaxMembers) = 0;
    virtual int GetLobbyMemberLimit(CSteamID steamIDLobby) = 0;
    virtual bool SetLobbyType(CSteamID steamIDLobby, ELobbyType eLobbyType) = 0;
    virtual bool SetLobbyJoinable(CSteamID steamIDLobby, bool bLobbyJoinable) = 0;
    virtual CSteamID GetLobbyOwner(CSteamID steamIDLobby) = 0;
};

// "SteamNetworking006"
class ISteamNetworking {
  public:
    virtual bool SendP2PPacket(CSteamID steamIDRemote, const void *pubData, uint32_t cubData, EP2PSend eP2PSendType,
                               int nChannel) = 0;
    virtual bool IsP2PPacketAvailable(uint32_t *pcubMsgSize, int nChannel) = 0;
    virtual bool ReadP2PPacket(void *pubDest, uint32_t cubDest, uint32_t *pcubMsgSize, CSteamID *psteamIDRemote,
                               int nChannel) = 0;
    virtual bool AcceptP2PSessionWithUser(CSteamID steamIDRemote) = 0;
    virtual bool CloseP2PSessionWithUser(CSteamID steamIDRemote) = 0;
    virtual bool CloseP2PChannelWithUser(CSteamID steamIDRemote, int nChannel) = 0;
    virtual bool GetP2PSessionState(CSteamID steamIDRemote, P2PSessionState_t *pConnectionState) = 0;
    virtual bool AllowP2PPacketRelay(bool bAllow) = 0;
};

// Same layout as CCallbackBase from the SDK. steam_api64.dll writes the flags and the callback id.
// Don't add a virtual destructor, it would change the vtable.
class CCallbackBase {
  public:
    CCallbackBase() {
        m_nCallbackFlags = 0;
        m_iCallback = 0;
    }
    virtual void Run(void *pvParam) = 0;
    virtual void Run(void *pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall) = 0;
    int GetICallback() const noexcept { return m_iCallback; }
    virtual int GetCallbackSizeBytes() = 0;

  protected:
    enum { k_ECallbackFlagsRegistered = 0x01, k_ECallbackFlagsGameServer = 0x02 };
    uint8_t m_nCallbackFlags;
    int m_iCallback;

  private:
    CCallbackBase(const CCallbackBase &) = delete;
    CCallbackBase &operator=(const CCallbackBase &) = delete;
};

// Resolves steam_api64.dll exports and hands out interfaces.
class SteamApi {
  public:
    static constexpr const char *USER_VERSION = "SteamUser023";
    static constexpr const char *UTILS_VERSION = "SteamUtils011";
    static constexpr const char *FRIENDS_VERSION = "SteamFriends018";
    static constexpr const char *MATCHMAKING_VERSION = "SteamMatchMaking009";
    static constexpr const char *NETWORKING_VERSION = "SteamNetworking006";

#ifdef _WIN64
    static constexpr const wchar_t *DLL_NAME = L"steam_api64.dll";
#else
    static constexpr const wchar_t *DLL_NAME = L"steam_api.dll";
#endif

    // Uses steam_api64.dll if the game has already loaded it
    bool AttachToLoadedModule();
    bool LoadModule(const std::wstring &path, std::string &error);

    bool IsLoaded() const noexcept { return m_module != nullptr; }
    // True after someone (the game or the mod) has called SteamAPI_Init successfully
    bool IsInitialized() const;
    bool Init(std::string &error);

    void RunCallbacks();
    void RegisterCallback(CCallbackBase *callback, int callbackId);
    void UnregisterCallback(CCallbackBase *callback);
    void RegisterCallResult(CCallbackBase *callback, SteamAPICall_t apiCall);
    void UnregisterCallResult(CCallbackBase *callback, SteamAPICall_t apiCall);

    void *FindInterface(const char *version);

  private:
    bool ResolveExports(std::string &error);

    using GetHSteamUser_t = HSteamUser(__cdecl *)();
    using FindOrCreateUserInterface_t = void *(__cdecl *)(HSteamUser hSteamUser, const char *pszVersion);
    using RunCallbacks_t = void(__cdecl *)();
    using RegisterCallback_t = void(__cdecl *)(CCallbackBase *pCallback, int iCallback);
    using UnregisterCallback_t = void(__cdecl *)(CCallbackBase *pCallback);
    using RegisterCallResult_t = void(__cdecl *)(CCallbackBase *pCallback, SteamAPICall_t hAPICall);
    using UnregisterCallResult_t = void(__cdecl *)(CCallbackBase *pCallback, SteamAPICall_t hAPICall);
    using InternalInit_t = int(__cdecl *)(const char *pszInternalCheckInterfaceVersions, char *pOutErrMsg);
    using InitFlat_t = int(__cdecl *)(char *pOutErrMsg);
    using InitLegacy_t = bool(__cdecl *)();

    HMODULE m_module = nullptr;

    GetHSteamUser_t m_getHSteamUser = nullptr;
    FindOrCreateUserInterface_t m_findOrCreateUserInterface = nullptr;
    RunCallbacks_t m_runCallbacks = nullptr;
    RegisterCallback_t m_registerCallback = nullptr;
    UnregisterCallback_t m_unregisterCallback = nullptr;
    RegisterCallResult_t m_registerCallResult = nullptr;
    UnregisterCallResult_t m_unregisterCallResult = nullptr;
    InternalInit_t m_internalInit = nullptr;
    InitFlat_t m_initFlat = nullptr;
    InitLegacy_t m_initLegacy = nullptr;
};

// Steam callback bound to a member function. Handlers can run on any thread that pumps Steam callbacks.
template <class Param, class Owner> class SteamCallback final : public CCallbackBase {
  public:
    using Handler = void (Owner::*)(Param *param, bool ioFailure);

    SteamCallback(Owner *owner, Handler handler) : m_owner(owner), m_handler(handler) {}

    void Register(SteamApi &api) {
        if (m_nCallbackFlags & k_ECallbackFlagsRegistered)
            return;
        api.RegisterCallback(this, Param::k_iCallback);
    }

    void Unregister(SteamApi &api) {
        if (m_nCallbackFlags & k_ECallbackFlagsRegistered)
            api.UnregisterCallback(this);
    }

  private:
    void Run(void *param) override { (m_owner->*m_handler)(static_cast<Param *>(param), false); }
    void Run(void *param, bool ioFailure, SteamAPICall_t) override {
        (m_owner->*m_handler)(static_cast<Param *>(param), ioFailure);
    }
    int GetCallbackSizeBytes() override { return sizeof(Param); }

    Owner *m_owner;
    Handler m_handler;
};

// One-shot result of an async Steam call. Steam drops the registration after delivering the result.
template <class Param, class Owner> class SteamCallResult final : public CCallbackBase {
  public:
    using Handler = void (Owner::*)(Param *param, bool ioFailure);

    SteamCallResult(Owner *owner, Handler handler) : m_owner(owner), m_handler(handler) {
        m_iCallback = Param::k_iCallback;
    }

    void Set(SteamApi &api, SteamAPICall_t apiCall) {
        Cancel(api);
        m_apiCall = apiCall;
        if (apiCall != k_uAPICallInvalid)
            api.RegisterCallResult(this, apiCall);
    }

    void Cancel(SteamApi &api) {
        if (m_apiCall != k_uAPICallInvalid)
            api.UnregisterCallResult(this, m_apiCall);
        m_apiCall = k_uAPICallInvalid;
    }

  private:
    void Run(void *param) override { Run(param, false, m_apiCall); }
    void Run(void *param, bool ioFailure, SteamAPICall_t apiCall) override {
        if (apiCall != m_apiCall)
            return;
        m_apiCall = k_uAPICallInvalid;
        (m_owner->*m_handler)(static_cast<Param *>(param), ioFailure);
    }
    int GetCallbackSizeBytes() override { return sizeof(Param); }

    Owner *m_owner;
    Handler m_handler;
    SteamAPICall_t m_apiCall = k_uAPICallInvalid;
};

} // namespace Steam
