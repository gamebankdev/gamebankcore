#pragma once

extern "C"
{
#include "lua/lua.h"
}

#define LUA_CONTRACTLIBNAME "contract"
LUAMOD_API int(luaopen_contract)(lua_State *L);

LUALIB_API void (luaL_openlibs_contract)(lua_State *L);
