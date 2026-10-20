--
-- Licensed under the MIT license. See LICENSE file in the project root for details.
--

---@type CoopOnline
local CoopOnline = ModRequire "CoopOnline.lua"

-- Both games send these through the game channel to check that scripts can talk to each other
local CHANNEL_CHECK_REQUEST = "TN_Coop:ChannelCheck"
local CHANNEL_CHECK_REPLY = "TN_Coop:ChannelCheckReply"

-- The friend lobby scan asks Steam about every friend, don't run it every frame
local FRIEND_LOBBIES_REFRESH_TICKS = 180
local MENU_REFRESH_TICKS = 15

local MenuState = {
    ---@type string?
    Notice = nil,
    GameChannelOk = false,
    FriendLobbies = {},
    TicksUntilFriendsRefresh = 0,
    TicksUntilMenuRefresh = 0,
    ---@type fun()?
    Refresh = nil,
}

---@param key string
---@param params table?
---@return string
local function Format(key, params)
    local text = GetDisplayName { Text = key } or key
    if params then
        text = string.gsub(text, "%$([A-Za-z]+)", function(name)
            local value = params[name]
            if value ~= nil then
                return tostring(value)
            end
        end)
    end
    return text
end

-- Steam names can contain characters that the game treats as text formatting
---@param name string?
---@return string
local function CleanName(name)
    return (string.gsub(name or "", "[{}@]", ""))
end

---@param force boolean?
local function UpdateFriendLobbies(force)
    if not force and MenuState.TicksUntilFriendsRefresh > 0 then
        MenuState.TicksUntilFriendsRefresh = MenuState.TicksUntilFriendsRefresh - 1
        return
    end

    MenuState.TicksUntilFriendsRefresh = FRIEND_LOBBIES_REFRESH_TICKS

    local state = CoopOnline.GetState()
    if state.Ready and state.Lobby == "Idle" then
        MenuState.FriendLobbies = CoopOnline.GetFriendLobbies()
    else
        MenuState.FriendLobbies = {}
    end
end

local function OnOnlineEvent(event)
    local eventType = event.Type

    if eventType == "Connected" then
        MenuState.Notice = nil
        MenuState.GameChannelOk = false
        CoopOnline.Send(CHANNEL_CHECK_REQUEST)
    elseif eventType == "Message" then
        if event.Data == CHANNEL_CHECK_REQUEST then
            CoopOnline.Send(CHANNEL_CHECK_REPLY)
        elseif event.Data == CHANNEL_CHECK_REPLY then
            MenuState.GameChannelOk = true
        end
    elseif eventType == "LobbyJoined" or eventType == "PeerJoined" then
        MenuState.Notice = nil
    elseif eventType == "PeerLeft" then
        MenuState.GameChannelOk = false
        MenuState.Notice = Format("OnlineMenu_PeerLeft", { Name = CleanName(event.Name) })
    elseif eventType == "ConnectionLost" then
        MenuState.GameChannelOk = false
        MenuState.Notice = Format("OnlineMenu_ConnectionLost", { Name = CleanName(event.Name), Reason = event.Reason })
    elseif eventType == "LobbyLeft" then
        MenuState.GameChannelOk = false
        -- PeerLeft already says that the host has left
        if event.Reason ~= "HostLeft" then
            MenuState.Notice = Format("OnlineMenu_Disconnected")
        end
        UpdateFriendLobbies(true)
    elseif eventType == "LobbyJoinFailed" then
        MenuState.Notice = Format("OnlineMenu_JoinFailed", { Reason = event.Reason })
        UpdateFriendLobbies(true)
    elseif eventType == "LobbyCreateFailed" then
        MenuState.Notice = Format("OnlineMenu_HostFailed", { Reason = event.Reason })
    elseif eventType == "VersionMismatch" then
        MenuState.Notice = Format("OnlineMenu_VersionMismatch", {
            Name = CleanName(event.Name),
            Mine = CoopOnline.GetState().ModVersion,
            Theirs = event.ModVersion ~= "" and event.ModVersion or "?",
        })
    elseif eventType == "InviteIgnored" then
        MenuState.Notice = Format("OnlineMenu_InviteIgnored", { Name = CleanName(event.Name) })
    end

    if MenuState.Refresh then
        MenuState.Refresh()
    end
end

CoopOnline.AddListener(OnOnlineEvent)

---@param key string
---@param params table?
---@param handler fun()
local function Action(key, params, handler)
    return { Key = key, Params = params, Handler = handler }
end

---@param success boolean
---@param err string?
---@param noticeKey string
local function ShowError(success, err, noticeKey)
    if not success then
        MenuState.Notice = Format(noticeKey, { Reason = err or "?" })
    end
end

local function Retry()
    MenuState.Notice = nil
    CoopOnline.Init()
    UpdateFriendLobbies(true)
end

local function Host()
    MenuState.Notice = nil
    local success, err = CoopOnline.Host()
    ShowError(success, err, "OnlineMenu_HostFailed")
end

local function Leave()
    MenuState.Notice = nil
    MenuState.GameChannelOk = false
    CoopOnline.Leave()
    UpdateFriendLobbies(true)
end

local function Invite()
    CoopOnline.Invite()
end

-- The check is sent on connect, but the reply can be missed while the menu is closed
local function CheckGameChannel()
    local state = CoopOnline.GetState()
    if state.Ready and state.Connection == "Connected" and not MenuState.GameChannelOk then
        CoopOnline.Send(CHANNEL_CHECK_REQUEST)
    end
end

local function Refresh()
    UpdateFriendLobbies(true)
    CheckGameChannel()
end

-- Returns the message lines and the button actions for the current session state
local function GetMenuModel()
    local state = CoopOnline.GetState()
    local lines = {}

    if MenuState.Notice then
        table.insert(lines, MenuState.Notice)
    end

    if not state.Ready then
        table.insert(lines, Format("OnlineMenu_SteamUnavailable", { Error = state.Error }))
        return lines, Action("OnlineMenu_Retry", nil, Retry), Action("OnlineMenu_Refresh", nil, Retry)
    end

    local primary, secondary

    if state.Lobby == "Idle" then
        table.insert(lines, Format("OnlineMenu_Idle"))
        primary = Action("OnlineMenu_Host", nil, Host)

        local lobbies = MenuState.FriendLobbies
        if #lobbies > 0 then
            local names = {}
            for _, lobby in ipairs(lobbies) do
                table.insert(names, CleanName(lobby.Name))
            end
            table.insert(lines, Format("OnlineMenu_FriendsHosting", { Names = table.concat(names, ", ") }))

            local lobby = lobbies[1]
            secondary = Action("OnlineMenu_JoinFriend", { Name = CleanName(lobby.Name) }, function()
                MenuState.Notice = nil
                local success, err = CoopOnline.Join(lobby.LobbyId)
                ShowError(success, err, "OnlineMenu_JoinFailed")
            end)
        else
            secondary = Action("OnlineMenu_Refresh", nil, Refresh)
        end
    elseif state.Lobby == "Creating" or state.Lobby == "Joining" then
        table.insert(lines, Format(state.Lobby == "Creating" and "OnlineMenu_Creating" or "OnlineMenu_Joining"))
        primary = Action("OnlineMenu_Refresh", nil, Refresh)
        secondary = Action("OnlineMenu_Cancel", nil, Leave)
    elseif not state.PeerSteamId then
        -- Only the host waits in a lobby alone, the guest leaves when the host does
        table.insert(lines, Format("OnlineMenu_Waiting"))
        if not state.OverlayEnabled then
            table.insert(lines, Format("OnlineMenu_NoOverlay"))
        end
        primary = Action("OnlineMenu_Invite", nil, Invite)
        secondary = Action("OnlineMenu_Refresh", nil, Refresh)
    else
        local name = CleanName(state.PeerName)
        if state.Connection == "Connected" then
            table.insert(lines, Format("OnlineMenu_Connected", { Name = name, Ping = state.Ping or "..." }))
            if MenuState.GameChannelOk then
                table.insert(lines, Format("OnlineMenu_ChannelOk"))
            else
                table.insert(lines, Format("OnlineMenu_ChannelWaiting", { Name = name }))
            end
            table.insert(lines, Format("OnlineMenu_NotPlayableYet"))
        elseif state.Connection == "VersionMismatch" then
            table.insert(lines, Format("OnlineMenu_VersionMismatch", {
                Name = name,
                Mine = state.ModVersion,
                Theirs = state.PeerModVersion ~= "" and state.PeerModVersion or "?",
            }))
        else
            table.insert(lines, Format("OnlineMenu_Connecting", { Name = name }))
        end
        primary = Action("OnlineMenu_Refresh", nil, Refresh)
        secondary = Action("OnlineMenu_Leave", nil, Leave)
    end

    table.insert(lines, Format("OnlineMenu_AppId", { AppId = state.AppId }))
    if state.ExpectedAppId ~= 0 and state.AppId ~= state.ExpectedAppId then
        table.insert(lines, Format("OnlineMenu_AppIdMismatch", { Expected = state.ExpectedAppId }))
    end

    return lines, primary, secondary
end

MainMenuAPIAddGamemode("Online Coop", function()
    local menu = CreateMenuScreen()
    menu:CreateBack(0.8)
    menu:CreateBackground("")
    menu:CreateTitleText()
    menu:SetLowerInputBlock(true)

    local message = CreateGUIComponentTextBox(menu)
    menu:AddReflection("mMessageText", message)

    local primaryButton = CreateGUIComponentButton(menu)
    menu:AddReflection("mPrimaryButton", primaryButton)

    local secondaryButton = CreateGUIComponentButton(menu)
    menu:AddReflection("mSecondaryButton", secondaryButton)

    local primaryAction, secondaryAction

    local function SetButtonText(button, action)
        if action.Params then
            button:SetText(Format(action.Key, action.Params))
        else
            button:SetTextLocalizationKey(action.Key)
        end
    end

    local function RefreshMenu()
        local lines
        lines, primaryAction, secondaryAction = GetMenuModel()
        message:SetText(table.concat(lines, "\n"))
        SetButtonText(primaryButton, primaryAction)
        SetButtonText(secondaryButton, secondaryAction)
    end

    -- Keeps the menu up to date when the game ticks while the menu is open
    local function OnTick()
        UpdateFriendLobbies()
        MenuState.TicksUntilMenuRefresh = MenuState.TicksUntilMenuRefresh - 1
        if MenuState.TicksUntilMenuRefresh <= 0 then
            MenuState.TicksUntilMenuRefresh = MENU_REFRESH_TICKS
            RefreshMenu()
        end
    end

    -- Buttons do what their text said when they were pressed
    local function Activate(action)
        CoopOnline.Tick()
        if action then
            action.Handler()
        end
        CoopOnline.Tick()
        RefreshMenu()
    end

    primaryButton:AddActivationHandler(function()
        Activate(primaryAction)
    end)

    secondaryButton:AddActivationHandler(function()
        Activate(secondaryAction)
    end)

    -- There is no online game yet, so the lobby lives as long as this screen
    menu:CreateCancelButton(function()
        CoopOnline.RemoveTickListener(OnTick)
        MenuState.Refresh = nil
        CoopOnline.Leave()
        menu:ExitScreen()
    end)

    MenuState.Notice = nil
    MenuState.GameChannelOk = false
    MenuState.Refresh = RefreshMenu
    CoopOnline.AddTickListener(OnTick)

    CoopOnline.Init()
    -- Events that came while the menu was closed, e.g. a Steam invite accepted from the main menu
    CoopOnline.Tick()
    CheckGameChannel()
    UpdateFriendLobbies(true)
    RefreshMenu()

    menu:LoadDefenitions("../Mods/TN_CoopMod/OnlineMenuScreen.sjson")
end)
