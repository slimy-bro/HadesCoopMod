--
-- Licensed under the MIT license. See LICENSE file in the project root for details.
--

-- Lua side of the online session (src/online/OnlineSession.cpp).
-- The native code keeps the Steam lobby and the connection alive on its own thread,
-- scripts read its events with Tick.
---@class CoopOnline
local CoopOnline = {}

---@private
---@type fun(event: table)[]
CoopOnline.Listeners = {}

---@return boolean
function CoopOnline.IsSupported()
    return CoopOnlineInit ~= nil
end

-- Writes to HadesCoopOnline.log
---@param text string
function CoopOnline.Log(text)
    if CoopOnlineLog then
        CoopOnlineLog(tostring(text))
    end
end

-- Connects to Steam. Safe to call many times.
---@return boolean success
---@return string? error
function CoopOnline.Init()
    if not CoopOnline.IsSupported() then
        return false, "HadesCoopGame.dll is outdated"
    end

    return CoopOnlineInit()
end

---@return table
function CoopOnline.GetState()
    if not CoopOnline.IsSupported() then
        return { Ready = false, Error = "HadesCoopGame.dll is outdated" }
    end

    return CoopOnlineGetState()
end

---@param openInviteDialog boolean? open the Steam invite dialog once the lobby exists
---@return boolean success
---@return string? error
function CoopOnline.Host(openInviteDialog)
    return CoopOnlineHost(openInviteDialog == true)
end

---@param lobbyId string
---@return boolean success
---@return string? error
function CoopOnline.Join(lobbyId)
    return CoopOnlineJoin(lobbyId)
end

-- Opens the Steam overlay invite dialog
---@return boolean
function CoopOnline.Invite()
    return CoopOnlineInvite()
end

function CoopOnline.Leave()
    CoopOnlineLeave()
end

---@param data string
---@param reliable boolean?
---@return boolean success
---@return string? error
function CoopOnline.Send(data, reliable)
    return CoopOnlineSend(data, reliable ~= false)
end

-- Friends who are in a lobby of the same Steam app
---@return { Name: string, SteamId: string, LobbyId: string }[]
function CoopOnline.GetFriendLobbies()
    if not CoopOnline.IsSupported() then
        return {}
    end

    return CoopOnlineGetFriendLobbies()
end

---@param listener fun(event: table)
function CoopOnline.AddListener(listener)
    table.insert(CoopOnline.Listeners, listener)
end

---@private
function CoopOnline.Call(listener, ...)
    local success, err = pcall(listener, ...)
    if not success then
        CoopOnline.Log("Event listener failed: " .. tostring(err))
    end
end

-- Passes queued network events to the listeners. Menus call it from their own handlers.
-- It isn't hooked into the game's draw function: the native code keeps the connection alive without it,
-- and menu screens should only change from the handlers the main menu API runs.
function CoopOnline.Tick()
    if not CoopOnline.IsSupported() then
        return
    end

    for _, event in ipairs(CoopOnlinePoll()) do
        for _, listener in ipairs({ table.unpack(CoopOnline.Listeners) }) do
            CoopOnline.Call(listener, event)
        end
    end
end

return CoopOnline
