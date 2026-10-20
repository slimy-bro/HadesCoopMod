//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#pragma once

#include <cstdint>
#include <string>

namespace OnlineUtils {

// Directory of HadesCoopGame.dll with a trailing backslash
std::wstring GetModuleDirectory();

std::string ToUtf8(const std::wstring &text);

// Monotonic clock
uint64_t GetTimeUs();

// Reads `key = <integer>` from a Lua config file without running it.
// The game's Lua isn't available this early, and the mod only needs plain numbers here.
uint32_t ReadConfigNumber(const std::wstring &path, const char *key, uint32_t defaultValue);

// Finds "+connect_lobby <id>", the argument Steam uses to pass a lobby to a game
uint64_t ParseConnectLobby(const char *commandLine);

} // namespace OnlineUtils
