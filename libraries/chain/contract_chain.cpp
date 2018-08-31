#include <gamebank/chain/contract/contract_chain.hpp>
#include <fc/log/logger.hpp>
#include <gamebank/chain/contract/contract_object.hpp>
#include <gamebank/chain/contract/contract_user_object.hpp>
#include <gamebank/chain/database.hpp>
#include <gamebank/protocol/types.hpp>

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
	using fc::ripemd160;

static int head_block_num(lua_State *L) {
	chain::database* db = (chain::database*)(L->extend.pointer);
	lua_Integer head_block_num = db->head_block_num();
	lua_pushinteger(L, head_block_num);
	return 1;
}

// get_block_hash block_num block_count
static int get_block_hash(lua_State *L) {
	int n = lua_gettop(L);  /* number of arguments */
	if (n != 3)
	{
		luaL_error(L, "expected 3 argument");
		return 0;
	}
	luaL_argcheck(L, lua_isinteger(L, 1), 1, "integer expected");
	luaL_argcheck(L, lua_isinteger(L, 2), 2, "integer expected");
	luaL_argcheck(L, lua_isinteger(L, 3), 3, "integer expected");
	lua_Integer block_num = lua_tointeger(L, 1);
	lua_Integer block_count = lua_tointeger(L, 2);
	lua_Integer interval = lua_tointeger(L, 3);
	if (!(block_count > 0 && block_count <= 100)) {
		luaL_error(L, "block_count must > 0 && <= 100");
		return 0;
	}
	chain::database* db = (chain::database*)(L->extend.pointer);
	if (!(block_num > 0 && block_num <= db->head_block_num())) {
		luaL_error(L, "block_num must > 0 && <= head_block_num");
		return 0;
	}
	if (!(block_num >= block_count)) {
		luaL_error(L, "block_num must > block_count");
		return 0;
	}
	if (interval < 1 || interval > block_count) {
		luaL_error(L, "interval value error");
		return 0;
	}
	ripemd160 hash_result;
	for ( int i=0; i<block_count; ++i)
	{
		optional<signed_block> block = db->fetch_block_by_number(block_num-i*interval);
		if (!block) {
			luaL_error(L, "block data not found");
			return 0;
		}
		if (block->transactions.empty())
			hash_result = hash_result.hash(block->digest());
		else
			hash_result = hash_result.hash(block->transaction_merkle_root);
	}
	ilog("get_block_hash ${block_num}.${block_count}.${interval}:${hash}", ("block_num", block_num)("block_count", block_count)("interval", interval)("hash", hash_result.str()));
	lua_pushstring(L, hash_result.str().c_str());
	return 1;
}

static const luaL_Reg chainlib[] = {
	{ "head_block_num", head_block_num },
    { "get_block_hash", get_block_hash },
	{ nullptr, nullptr }
};

LUAMOD_API int luaopen_chain(lua_State *L) {
	luaL_newlib(L, chainlib);
	//createmetatable(L);
	return 1;
}

/*
** these libs are loaded by lua.c and are readily available to any Lua
** program
*/
static const luaL_Reg loadedlibs_chain[] = {
	{ LUA_CHAINLIBNAME, luaopen_chain },
	{ nullptr, nullptr }
};

LUALIB_API void luaL_openlibs_chain(lua_State *L) {
	const luaL_Reg *lib;
	/* "require" functions from 'loadedlibs' and set results to global table */
	for (lib = loadedlibs_chain; lib->func; lib++) {
		luaL_requiref(L, lib->name, lib->func, 1);
		lua_pop(L, 1);  /* remove lib */
	}
}

}}
