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
    string asset_num_to_string(uint32_t asset_num)
    {
        switch (asset_num)
        {
        case GAMEBANK_ASSET_NUM_GBC:
            //return "GBC";
            return "GB";
        case GAMEBANK_ASSET_NUM_GBD:
            return "GBD";
        case GAMEBANK_ASSET_NUM_GBS:
            return "GBS";
        default:
            return "UNKN";
        }
    }

    int64_t precision(const asset_symbol_type& symbol)
    {
        static int64_t table[] = {
            1, 10, 100, 1000, 10000,
            100000, 1000000, 10000000, 100000000ll,
            1000000000ll, 10000000000ll,
            100000000000ll, 1000000000000ll,
            10000000000000ll, 100000000000000ll
        };
        uint8_t d = symbol.decimals();
        return table[d];
    }

    string to_string(const asset& legacy_asset)
    {
        int64_t prec = precision(legacy_asset.symbol);
        string result = fc::to_string(legacy_asset.amount.value / prec);
        if (prec > 1)
        {
            auto fract = legacy_asset.amount.value % prec;
            // prec is a power of ten, so for example when working with
            // 7.005 we have fract = 5, prec = 1000.  So prec+fract=1005
            // has the correct number of zeros and we can simply trim the
            // leading 1.
            result += "." + fc::to_string(prec + fract).erase(0, 1);
        }
        return result + " " + asset_num_to_string(legacy_asset.symbol.asset_num);
    }

static int contract_get_name(lua_State *L) {
	lua_pushstring(L, L->extend.contract_name);
	return 1;
}

static int contract_get_caller(lua_State *L) {
	lua_pushstring(L, L->extend.caller_name);
	return 1;
}

static int contract_get_creator(lua_State *L) {
	if (lua_gettop(L) != 0)
	{
		luaL_error(L, "expected zero arg");
		return 0;
	}
	const char* user_name = L->extend.contract_name;
	chain::database* db = (chain::database*)(L->extend.pointer);
	const contract_object* obj = db->find_contract(user_name);
	if (obj == nullptr)
	{
		luaL_error(L, "expected a real contract name");
		return 0;
	}
	string str_creator = obj->creator;
	lua_pushstring(L, str_creator.c_str());
	return 1;
}


static int contract_get_data_by_username(lua_State *L, const char* user_name) {
	int check_top = lua_gettop(L);
	lua_getglobal(L, LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME);
	if (!lua_istable(L, -1)) {
		luaL_error(L, "_contract_modified_data must be a table");
		return 0;
	}
	lua_getfield(L, -1, user_name);
	if (lua_istable(L, -1)) // check if the table has already exist
	{
		return 1; // return TRACT_MODIFIED_DATA_TABLE_NAME[user_name]
	}
	lua_pop(L, 2); // pop LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME and LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME[user_name]
	if (!(check_top == lua_gettop(L))) {
		luaL_error(L, "lua stack error");
		return 0;
	}

	chain::database* db = (chain::database*)(L->extend.pointer);
	auto contract_data = db->find<contract_user_object, by_contract_user>(boost::make_tuple(L->extend.contract_name, user_name));
	std::string data = contract_data ? to_string(contract_data->data) : "{}";
	//ilog("read contract_data ${contract_name}.${user_name}:${data}", ("contract_name", L->extend.contract_name)("user_name", user_name)("data", data));
	int ret = json_decode_fromstring(L, data.c_str(), data.length()); // create datatable

	check_top = lua_gettop(L);
	lua_getglobal(L, LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME);
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
	chain::database* db = (chain::database*)(L->extend.pointer);
	if (db->find_contract(user_name) == nullptr) {
		luaL_error(L, "expected a real contract name");
		return 0;
	}
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
	chain::database* db = (chain::database*)(L->extend.pointer);
	if (db->find_account(user_name) == nullptr) {
		luaL_error(L, "expected a real account name");
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
    if (strcmp(L->extend.caller_name, from_account) == 0) {
        if (db->get_balance(from, amount.symbol) < amount) {
            luaL_error(L, "Account does not have sufficient funds for transfer");
            return 0;
        }
        if (strcmp(L->extend.contract_name, to_account) != 0) {
            luaL_error(L, "caller can only tranfer GBC to the current contract");
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
        db->adjust_balance(from, -amount);
		db->adjust_contract_balance(to, amount);
        
    }
    else {
        db->adjust_contract_balance(from, -amount);
        db->adjust_balance(to, amount);
    }

    contract_log_operation logs;
    logs.name = L->extend.contract_name;
	logs.key = "transfer";
    logs.data = string("[\"") + from + string("\",\"") + to + string("\",\"") + to_string(amount) + string("\"]");
    db->contract_operation(logs);
	return 0;
}

static int contract_emit(lua_State *L) {
    int32_t n = lua_gettop(L);
	if (n != 2)
	{
		luaL_error(L, "expected 2 argument");
		return 0;
	}
	luaL_argcheck(L, lua_type(L, 1) == LUA_TSTRING, 1, "string expected");
	luaL_argcheck(L, lua_type(L, 2) == LUA_TTABLE, 2, "table expected");

    string key = lua_tostring(L, 1);
    string data;
	int datalen = 0;
	char* json = json_encode_tostring(L, &datalen);
	if (json != nullptr && datalen > 0)
		data.assign(json, datalen);

    chain::database* db = (chain::database*)(L->extend.pointer);

    contract_log_operation logs;
    logs.name = L->extend.contract_name;
	logs.key = key;
    logs.data = data;
    db->contract_operation(logs);
    return 0;
}

static int contract_jsonstr_to_table(lua_State *L) {
	int n = lua_gettop(L);  /* number of arguments */
	if (n != 1) {
		luaL_error(L, "expected 1 argument");
		return 0;
	}
	luaL_argcheck(L, lua_isstring(L, 1), 1, "string expected");
	std::string jsonstr = lua_tostring(L, 1);
	int ret = json_decode_fromstring(L, jsonstr.c_str(), jsonstr.length()); // create datatable
	return ret;
}

static const luaL_Reg contractlib[] = {
	{ "get_name", contract_get_name },
    { "get_creator", contract_get_creator },
	{ "get_caller", contract_get_caller },
	{ "get_data", contract_get_data },
	{ "get_user_data", contract_get_user_data },
	{ "transfer", contract_transfer },
    { "emit", contract_emit },
	{ "jsonstr_to_table", contract_jsonstr_to_table },
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
