#include <gamebank/plugins/contract/contract_lualib.hpp>
#include <fc/log/logger.hpp>

extern "C"
{
#include "gamebank/plugins/contract/lua/lua.h"
#include "gamebank/plugins/contract/lua/lualib.h"
#include "gamebank/plugins/contract/lua/lauxlib.h"
#include "gamebank/plugins/contract/lua/lobject.h"
#include "gamebank/plugins/contract/lua/lstate.h"
#include "gamebank/plugins/contract/lua/lopcodes.h"
#include "gamebank/plugins/contract/lua/lua_cjson.h"
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
	if (lua_gettop(L) != 0)
	{
		return 0;
	}
	std::string data = "{}"; // read from database
	int ret = json_decode_fromstring(L, data.c_str(), data.length()); // create datatable

	int check_top = lua_gettop(L);
	lua_getglobal(L, "_modified_data");
	assert(lua_istable(L, -1));
	lua_pushstring(L, L->extend.contract_name);
	lua_pushvalue(L, -3 ); // push datatable to top
	lua_rawset(L, -3); // _modified_data[contract_name] = datatable
	lua_pop(L, 1);
	int check_top2 = lua_gettop(L);
	assert(check_top == check_top2);

	return ret;
}

static int contract_get_user_data(lua_State *L) {
	if (lua_gettop(L) != 1)
	{
		return 0;
	}
	if (!lua_isstring(L, 1))
	{
		return 0;
	}
	const char* user_name = lua_tostring(L, 1);
	if (user_name == nullptr)
	{
		return 0;
	}
	std::string data = "{}"; // read from database
	return json_decode_fromstring(L, data.c_str(), data.length());
}

static int contract_transfer(lua_State *L) {
	if (lua_gettop(L) != 3)
	{
		return 0;
	}
	return 0;
}

static const luaL_Reg contractlib[] = {
	{ "get_name", contract_get_name },
	{ "get_caller", contract_get_caller },
	{ "get_data", contract_get_data },
	{ "get_user_data", contract_get_user_data },
	{ "transfer", contract_transfer },
	{ nullptr, nullptr }
};

LUAMOD_API int luaopen_contract(lua_State *L) {
	luaL_newlib(L, contractlib);
	//createmetatable(L);
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

