//
// Copyright (c) Uladzislau Nikalayevich <thenormalnij@gmail.com>. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#include "pch.h"

#include "../include/HadesModApi.h"
#include "CoopContext.h"
#include "online/OnlineSession.h"

HADES_MOD_API void _cdecl HadesModLuaCreated(lua_State *luaState) {
    OnlineSession::Instance().OnLuaCreated();
    CoopContext::GetInstance()->InitLua(luaState);
};

HADES_MOD_API bool _cdecl HadesModInit(const IModApi *modApi) {
    if (modApi->version < MOD_API_VERSION)
        return false;

    // Must run before the game initializes Steam to change the Steam AppID
    OnlineSession::Instance().PrepareEarly();

    HookTable::Instance().Init(modApi->GetSymbolAddress);

    return true;
};

HADES_MOD_API bool _cdecl HadesModStart() { return true; };

HADES_MOD_API bool _cdecl HadesModStop() {
    OnlineSession::Instance().Shutdown();
    return true;
};
