#include <contract/contract_lualib.hpp>
#include <fc/log/logger.hpp>

extern "C"
{
#include "contract/lua/lua.h"
#include "contract/lua/lualib.h"
#include "contract/lua/lauxlib.h"
#include "contract/lua/lobject.h"
#include "contract/lua/lstate.h"
#include "contract/lua/lopcodes.h"
}

static int contract_get_name(lua_State *L) {
	lua_pushstring(L, L->extend.contract_name);
	return 1;
}

static int contract_get_caller(lua_State *L) {
	lua_pushstring(L, L->extend.caller_name);
	return 1;
}

static int contract_get_data(lua_State *L) {
	return 0;
}

static int contract_get_user_data(lua_State *L) {
	return 0;
}

static const luaL_Reg contractlib[] = {
	{ "get_name", contract_get_name },
	{ "get_caller", contract_get_caller },
	{ "get_data", contract_get_data },
	{ "get_user_data", contract_get_user_data },
	{ nullptr, nullptr }
};

LUAMOD_API int luaopen_contract(lua_State *L) {
	luaL_newlib(L, contractlib);
	createmetatable(L);
	return 1;
}

/*
** these libs are loaded by lua.c and are readily available to any Lua
** program
*/
static const luaL_Reg loadedlibs_contract[] = {
	{ LUA_CONTRACTLIBNAME, luaopen_contract },
	{ nullptr, nullptr }
};

LUALIB_API void luaL_openlibs_contract(lua_State *L) {
	const luaL_Reg *lib;
	/* "require" functions from 'loadedlibs' and set results to global table */
	for (lib = loadedlibs_contract; lib->func; lib++) {
		luaL_requiref(L, lib->name, lib->func, 1);
		lua_pop(L, 1);  /* remove lib */
	}
}

