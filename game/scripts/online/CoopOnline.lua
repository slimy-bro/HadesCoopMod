--
-- Licensed under the MIT license. See LICENSE file in the project root for details.
--

---@type HookUtils
local HookUtils = ModRequire "../HookUtils.lua"

-- Lua side of the online session (src/online/OnlineSession.cpp).
-- The native code keeps the Steam lobby and the connection alive on its own thread,
-- scripts read its events with Tick.
---@class CoopOnline
local CoopOnline = {}

---@private
---@type fun(event: table)[]
CoopOnline.Listeners = {}

---@private
---@type fun()[]
CoopOnline.TickListeners = {}

---@private
CoopOnline.IsTickHooked = false

---@return boolean
function CoopOnline.IsSupported()
    return CoopOnlineInit ~= nil
end

-- Connects to Steam. Safe to call many times.
---@return boolean success
---@return string? error
function CoopOnline.Init()
    if not CoopOnline.IsSupported() then
        return false, "HadesCoopGame.dll is outdated"
    end

    CoopOnline.HookTick()
    return CoopOnlineInit()
end

---@return table
function CoopOnline.GetState()
    if not CoopOnline.IsSupported() then
        return { Ready = false, Error = "HadesCoopGame.dll is outdated" }
    end

    return CoopOnlineGetState()
end

---@return boolean success
---@return string? error
function CoopOnline.Host()
    return CoopOnlineHost()
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

---@param listener fun()
function CoopOnline.AddTickListener(listener)
    table.insert(CoopOnline.TickListeners, listener)
end

---@param listener fun()
function CoopOnline.RemoveTickListener(listener)
    for index = #CoopOnline.TickListeners, 1, -1 do
        if CoopOnline.TickListeners[index] == listener then
            table.remove(CoopOnline.TickListeners, index)
        end
    end
end

---@private
function CoopOnline.Call(listener, ...)
    local success, err = pcall(listener, ...)
    if not success then
        DebugPrint { Text = "CoopOnline: " .. tostring(err) }
    end
end

-- Passes queued network events to the listeners
function CoopOnline.Tick()
    if not CoopOnline.IsSupported() then
        return
    end

    for _, event in ipairs(CoopOnlinePoll()) do
        for _, listener in ipairs({ table.unpack(CoopOnline.Listeners) }) do
            CoopOnline.Call(listener, event)
        end
    end

    for _, listener in ipairs({ table.unpack(CoopOnline.TickListeners) }) do
        CoopOnline.Call(listener)
    end
end

-- The game calls draw every frame
---@private
function CoopOnline.HookTick()
    if CoopOnline.IsTickHooked or type(draw) ~= "function" then
        return
    end

    CoopOnline.IsTickHooked = true
    HookUtils.onPostFunction("draw", CoopOnline.Tick)
end

return CoopOnline
