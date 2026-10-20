--
-- Copyright (c) Uladzislau Nikalayevich <thenormalnij@gmail.com>. All rights reserved.
-- Licensed under the MIT license. See LICENSE file in the project root for details.
--

---@class CoopModConfig
local Config = {
    -- Choose loot delivery type here
    -- Possible values: "Shared", "RoomDuplicated"
    --
    -- Shared - rooms generate one reward. Only one player can pick it up.
    -- The game has a query that select a player context for a reward.
    -- E.g. Player 1 boon room, meta progress room, Player 2 boon room, Player 1 boon room...
    --
    -- RoomDuplicated - rooms generate two reward for each player.
    LootDelivery = "RoomDuplicated",

    -- This config is used for the "RoomDuplicated" loot delivery option
    RoomDuplicatedDelivery = {
        -- Enables duplication of the Chaos boons.
        DuplicateChaosBoon = false;
    },
    Player1HasOutline = true;
    Player1Outline = {
        R = 0,
        G = 0,
        B = 200,
        Opacity = 0.6,
        Thickness = 2,
        Threshold = 0.6,
    };
    Player2HasOutline = true,
    Player2Outline = {
        R = 0,
        G = 200,
        B = 0,
        Opacity = 0.6,
        Thickness = 2,
        Threshold = 0.6,
    };
    TextAbovePlayersEnabled = false;
    TextAbovePlayersParams = {
        -- CreateTextBox params
        OffsetX = 0,
        OffsetY = -150,
        FontSize = 28,
        Color = { 255, 255, 255, 255 },
        ShadowColor = { 0, 0, 0, 240 },
        ShadowOffset = { 0, 2 },
        ShadowBlur = 0,
        OutlineThickness = 3,
        OutlineColor = { 0, 0, 0, 1 },
        Font = "AlegreyaSansSCRegular",
        Justification = "Center"
    },
    -- Online co-op through Steam (in development, see README)
    Online = {
        -- Steam AppID the game reports to Steam while the mod is installed.
        -- 480 is Spacewar, Valve's public test app. Online lobbies, invites and networking go through it,
        -- so Steam and Epic Games Store players can play together.
        -- While it is active Steam shows you as playing Spacewar and Hades achievements don't unlock.
        -- Set to 0 to keep the game's own AppID. Steam players can still play online then,
        -- but both players must use the same AppID.
        -- The mod reads this value when the game starts, restart the game after changing it.
        SteamAppId = 480,
    },
    Debug = {
        OneHit = false,
        P1GodMode = false,
        P2GodMode = false,
    }
}

return Config
