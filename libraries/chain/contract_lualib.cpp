#include <gamebank/chain/contract/contract_lualib.hpp>
#include <fc/log/logger.hpp>
#include <gamebank/chain/contract/contract_object.hpp>
#include <gamebank/chain/contract/contract_user_object.hpp>
#include <gamebank/chain/database.hpp>

extern "C"
{
#include "gamebank/chain/contract/lua/lua.h"
#include "gamebank/chain/contract/lua/lualib.h"
#include "gamebank/chain/contract/lua/lauxlib.h"
#include "gamebank/chain/contract/lua/lobject.h"
#include "gamebank/chain/contract/lua/lstate.h"
#include "gamebank/chain/contract/lua/lopcodes.h"
#include "gamebank/chain/contract/lua/lua_cjson.h"
}

namespace gamebank { namespace chain {

static int contract_get_name(lua_State *L) {
	lua_pushstring(L, L->extend.contract_name);
	return 1;
}

static int contract_get_caller(lua_State *L) {
	lua_pushstring(L, L->extend.caller_name);
	return 1;
}

static int contract_get_data_by_username(lua_State *L, const char* user_name) {
	// todo: check is load in lua?
	chain::database* db = (chain::database*)(L->extend.pointer);
	if (db->find_account(user_name) == nullptr) {
		luaL_error(L, "expected a real account name");
		return 0;
	}
	auto contract_data = db->find<contract_user_object, by_contract_user>(boost::make_tuple(L->extend.contract_name, user_name));
	std::string data = contract_data ? to_string(contract_data->data) : "{}";
	ilog("read contract_data ${contract_name}.${user_name}:${data}", ("contract_name", L->extend.contract_name)("user_name", user_name)("data", data));
	int ret = json_decode_fromstring(L, data.c_str(), data.length()); // create datatable

	int check_top = lua_gettop(L);
	lua_getglobal(L, LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME);
	if (!lua_istable(L, -1)) {
		luaL_error(L, "_contract_modified_data must be a table");
		return 0;
	}
	lua_pushstring(L, user_name);
	lua_pushvalue(L, -3); // push datatable to top
	lua_rawset(L, -3); // _contract_modified_data[contract_name] = datatable
	lua_pop(L, 1);
	int check_top2 = lua_gettop(L);
	if (!(check_top == check_top2)) {
		luaL_error(L, "lua stack error");
		return 0;
	}
	return ret;
}

static int contract_get_data(lua_State *L) {
	if (lua_gettop(L) != 0)
	{
		luaL_error(L, "expected zero arg");
		return 0;
	}
	const char* user_name = L->extend.contract_name;
	return contract_get_data_by_username(L, user_name);
}

static int contract_get_user_data(lua_State *L) {
	if (lua_gettop(L) != 1)
	{
		luaL_error(L, "expected 1 arg");
		return 0;
	}
	if (!lua_isstring(L, 1))
	{
		luaL_error(L, "expected string arg");
		return 0;
	}
	const char* user_name = lua_tostring(L, 1);
	if (user_name == nullptr)
	{
		return 0;
	}
	return contract_get_data_by_username(L, user_name);
}

static int contract_transfer(lua_State *L) {
    int n = lua_gettop(L);  /* number of arguments */
    if (n != 3)
    {
        luaL_error(L, "expected 3 argument");
        return 0;
    }
    luaL_argcheck(L, lua_isstring(L, 1), 1, "string expected");
    luaL_argcheck(L, lua_isstring(L, 2), 2, "string expected");
    luaL_argcheck(L, lua_isinteger(L, 3), 3, "integer expected");
    const char* from_account = lua_tostring(L, 1);
    const char* to_account = lua_tostring(L, 2);
    lua_Integer num = lua_tointeger(L, 3);
	luaL_argcheck(L, num > 0, 3, "amount expected positive");

    account_name_type from = from_account;
    account_name_type to = to_account;
    asset amount(num, GBC_SYMBOL);
    chain::database* db = (chain::database*)(L->extend.pointer);

    bool from_caller = false;
	bool to_contract = false;
    if (strcmp(L->extend.caller_name, from_account) == 0) {
        if (db->get_balance(from, amount.symbol) < amount) {
            luaL_error(L, "Account does not have sufficient funds for transfer");
            return 0;
        }
        from_caller = true;
    }
    else if (strcmp(L->extend.contract_name, from_account) == 0) {
        if (db->get_contract_balance(from, amount.symbol) < amount) {
            luaL_error(L, "contract account does not have sufficient funds for transfer");
            return 0;
        }
        from_caller = false;
    }
    else {
        luaL_error(L, "only the contract caller or owner can call transfer");
        return 0;
    }
	
    if (from_caller) {
		if (strcmp(L->extend.contract_name, to_account) != 0) {
			luaL_error(L, "caller can only tranfer GBC to the current contract");
			return 0;
		}
        db->adjust_balance(from, -amount);
		db->adjust_contract_balance(to, amount);
        
    }
    else {
        db->adjust_contract_balance(from, -amount);
        db->adjust_balance(to, amount);
    }

    transfer_operation tsf;
    tsf.from = from;
    tsf.to = to;
    tsf.amount = amount;
    db->contract_operation(tsf);
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

	int check_top = lua_gettop(L);
	lua_pushglobaltable(L);
	//lua_getglobal(L, "_G");
	assert(lua_istable(L, -1));
	lua_createtable(L, 0, 0);
	lua_setfield(L, -2, LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME); // _G["_contract_modified_data"] = {}
	lua_pop(L, 1);
	assert(check_top == lua_gettop(L));

	//lua_getglobal(L, LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME);
	//lua_pop(L, 1);
}

}}
