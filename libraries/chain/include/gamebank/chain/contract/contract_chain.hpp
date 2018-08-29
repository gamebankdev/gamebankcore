#pragma once

extern "C"
{
#include "lua/lua.h"
}

namespace gamebank { namespace chain {

#define LUA_CHAINLIBNAME "chain"
LUALIB_API void (luaL_openlibs_chain)(lua_State *L);

}}