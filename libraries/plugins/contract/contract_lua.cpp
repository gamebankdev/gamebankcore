#include <gamebank/plugins/contract/contract_lua.hpp>
#include <fc/log/logger.hpp>
#include <gamebank/plugins/contract/contract_lualib.hpp>

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

namespace gamebank { namespace contract {

	lua_State* create_lua_state()
	{
		lua_State* L = luaL_newstate();
		luaL_openlibs(L);
		luaL_openlibs_contract(L);
		return L;
	}

	contract_lua::contract_lua(account_name_type n) : contract_interface(n)
	{
		L = create_lua_state();
	}

	contract_lua::~contract_lua()
	{
		lua_close(L);
	}

	bool contract_lua::deploy(const std::string& data)
	{
		int stack_pos = lua_gettop(L);
		//dlog("deploy 1 stack_pos=%d\n", stack_pos);
		int data_size = data.length();
		std::string contract_name = name;
		int ret = luaL_loadbuffer(L, data.c_str(), data_size, contract_name.c_str());
		if (ret != 0)
		{
			const char* str = lua_tostring(L, -1);
			if (str != nullptr)
			{
				elog("luaL_loadbuffer Error: ${err}", ("err",str));
				lua_pop(L, 1);
			}
			//FC_ASSERT(ret == 0, "contract compile error");
			return false;
		}
		stack_pos = lua_gettop(L);
		int type = lua_type(L, -1);
		//printf("deploy 2 stack_pos=%d type=%d\n", stack_pos, type);
		LClosure* lc = clLvalue(L->top - 1);
		if (lc != nullptr)
		{
			if (lc->p != nullptr)
			{
				// check proto
				//print_proto(lc->p);
			}
		}

		ret = lua_pcall(L, 0, LUA_MULTRET, 0);
		if (ret != 0)
		{
			const char* str = lua_tostring(L, -1);
			if (str)
			{
				//printf("lua_pcall: %s\n", str);
				elog("lua_pcall Error: ${err}", ("err", str));
				lua_pop(L, 1);
				return false;
			}
			//FC_ASSERT(ret == 0, "contract compile error");
		}
		stack_pos = lua_gettop(L);
		//printf("deploy 3 stack_pos=%d\n", stack_pos);
		int check_top = lua_gettop(L);
		lua_getglobal(L, "_modified_data");
		assert(lua_isnil(L, -1));
		lua_pop(L, 1);
		assert(check_top == lua_gettop(L));

		lua_createtable(L, 0, 0);
		lua_setglobal(L, "_modified_data");
		int check_top2 = lua_gettop(L);
		assert(check_top == check_top2);

		lua_getglobal(L, "_modified_data");
		assert(lua_istable(L, -1));
		lua_pop(L, 1);
		assert(check_top == lua_gettop(L));

		return true;
	}

	bool contract_lua::call_method(const std::string& method, const variants& args, std::string& result)
	{
		int oldStackPos = lua_gettop(L);
		lua_getglobal(L, method.c_str());
		if (lua_isnil(L, -1))
		{
			lua_pop(L, 1);
			return false;
		}
		if (!lua_isfunction(L, -1))
		{
			lua_pop(L, 1);
			return false;
		}
		for (variant arg : args)
		{
			// push arg
			lua_pushstring(L, arg.as_string().c_str());
		}
		if (lua_pcall(L, lua_gettop(L) - (oldStackPos+1), LUA_MULTRET, 0) != 0)
		{
			const char* str = lua_tostring(L, -1);
			if (str != NULL)
			{
				elog("lua_pcall Error: ${err}", ("err", str));
			}
			lua_settop(L, oldStackPos);
			return false;
		}
		int retNum = lua_gettop(L) - oldStackPos;
		if (retNum == 1 && lua_isstring(L, -1))
		{
			const char* str = lua_tostring(L, -1);
			if (str != NULL)
			{
				result = str;
				//printf("result=%s\n", result.c_str());
				ilog("result:${ret}", ("ret", str));
			}
		}
		for (int i = 0; i < retNum; i++)
		{
			lua_pop(L, 1);
		}

		// auto save modified data
		save_modified_data();
		return true;
	}

	void contract_lua::save_modified_data()
	{
		//printf("save_modified_data\n" );
		lua_getglobal(L, "_modified_data");
		assert(lua_istable(L, -1));
		lua_pushnil(L);
		while (lua_next(L, -2) != 0) {
			/* table, key, value */
			int keytype = lua_type(L, -2);
			int valuetype = lua_type(L, -1);
			assert(keytype == LUA_TSTRING);
			assert(valuetype == LUA_TTABLE);

			const char* key = lua_tostring(L, -2);
			std::string filename = std::string(key) + ".json";
			int datalen = 0;
			char* json = json_encode_tostring(L, &datalen);
			// todo: save json to database

			lua_pop(L, 1);
		}
	}

}}
