//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#pragma once

class lua_State;

// Lua API of the online mode, see online/CoopOnline.lua
class LuaOnlineFunctionDefs {
  public:
    static void Load(lua_State *L);
};
