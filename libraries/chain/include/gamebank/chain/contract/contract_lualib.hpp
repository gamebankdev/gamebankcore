#pragma once

extern "C"
{
#include "lua/lua.h"
}

namespace gamebank { namespace chain {

#define LUA_CONTRACTLIBNAME "contract"
LUALIB_API void (luaL_openlibs_contract)(lua_State *L);

}}