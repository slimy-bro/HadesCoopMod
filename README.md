
# Hades coop mod

Play Hades with a friend!
This mod adds local cooperative multiplayer to Hades, allowing two players to fight through the Underworld together on the same PC.

For online play: Use a streaming tool like [Parsec](https://parsec.app/) to share your game session with a remote friend.
A native online mode is in development, see [Online co-op](#online-co-op-in-development).

The mod supports the **Steam** and **Epic Games Store** versions of the gamme

**Warning**

You need a gameplay to play this mod.

# Online co-op (in development)

Each player runs their own copy of Hades and the games talk to each other through Steam.
This version contains the first part: Steam lobbies, invites and the connection between the two games.
**Gameplay isn't synchronized yet, so you can't play a run together online yet.**

## What works now

- Host a lobby from *Select gamemode* -> *Online Coop* and invite a friend through the Steam overlay (Shift+Tab).
- Join a friend: accept their invite in Steam, use *Join Game* in the Steam friends list,
  or press *Join <name>* in the Online Coop menu.
- The games connect peer to peer through Steam (Steam relays the traffic when a direct connection isn't possible),
  check that both run a compatible version of the mod and show the ping.
- Leaving the Online Coop screen closes the lobby.

## Spacewar (Steam AppID 480)

Online mode runs through Spacewar, Valve's public Steamworks test app, so players don't need a shared game AppID.
This is set by `Online.SteamAppId` in `config.lua` and is on by default:

- Steam shows you as playing Spacewar while the mod is installed.
- Hades achievements don't unlock while Spacewar is active.
- Hades keeps its saves in `Documents/Saved Games/Hades`, they should not be affected.
  Back them up before you try online mode for the first time anyway.
- Set `SteamAppId = 0` to turn it off. Steam players can still play online then, through the game's own AppID.

The Online Coop menu shows the AppID in use. Both players need the same AppID.

The mod switches the AppID when the game starts, before the game connects to Steam.
If the menu shows a different AppID with a warning, the game connected to Steam before the mod was loaded.
In that case create `steam_appid.txt` with the text `480` next to `Hades.exe` (`Hades/x64` or `Hades/x64Vk`)
and start `Hades.exe` directly instead of starting it from Steam.

If Steam restarts the game during launch because of the AppID change, the mod doesn't change the AppID
on the next launch and notes it in the log.

## Requirements

- Both players use the same version of the mod, and Steam is running with them logged in.
- Epic Games Store version: copy `steam_api64.dll` next to `HadesCoopGame.dll`.
  It ships with the [Steamworks SDK](https://partner.steamgames.com/downloads/list) (`redistributable_bin/win64`)
  and with many Steam games. The Steam overlay may not work in this version.
  Your friend can join you from their Online Coop menu or their Steam friends list instead.

## Invites

Start Hades with the mod before you accept an invite.
With Spacewar, Steam can't start Hades from an invite and would try to start Spacewar instead.
Accepting an invite while the game is running joins the lobby, even outside of the Online Coop menu.

## Troubleshooting

The mod writes `HadesCoopOnline.log` next to `HadesCoopGame.dll`, or to `%TEMP%` if that folder is read only.
Attach it to bug reports.

- *Steam is not available*: start Steam and log in. Epic Games Store players need `steam_api64.dll`, see above.
- *Timed out*: a firewall may block Steam networking. Steam needs outgoing UDP traffic.
- *Different version of the mod*: both players need the same release of the mod.

## Next steps

The next part synchronizes the game itself: both games load the same rooms, the host runs the enemies
and rewards, and each player controls their own Zagreus. It has to be developed and tested in the real game.

# Build

## Using CMake for Windows x64

```powershell
cmake -A x64 . -B build_msvc
cmake --build build_msvc --config Release
```

Binary files are located in the `bin` folder.

## Using [Visual Studio](https://visualstudio.microsoft.com/) GUI

You need to install cmake in the Visual Studio Installer to build the project.
Open the project in VS and click Build -> Build All in the top menu.
Check `bin` folder for `HadesCoopGame.dll`.

# How to load the mod from the repository folder

Follow these steps if you want to develop the mod and save changes in git:

1. Create the folder `Hades/Hades/Content/ModModules/TN_CoopMod/`
2. Copy the repository into `Hades/Hades/Content/ModModules/TN_CoopMod/dev`
3. Create `Hades/Hades/Content/ModModules/TN_CoopMod/init.lua` with the following content:
```lua
ModRequire "dev/game/scripts/init.lua"
```
4. Create `Hades/Hades/Content/ModModules/TN_CoopMod/meta.sjson` with the following content:
```sjson
{
	Name = "Coop mod"
	Library = "dev/bin/HadesCoopGame.dll"
	Author = "Uladzislau 'TheNormalnij' Nikalayevich"
}
```
5. Now you can make changes directly in the repository and load them in the game.

# Suppoort

You can support development using crypto. See [my page](https://thenormalnij.de/donate) for details
