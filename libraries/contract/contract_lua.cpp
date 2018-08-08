#include <contract/contract_lua.hpp>
#include <fc/log/logger.hpp>
#include <contract/contract_lualib.hpp>

extern "C"
{
#include "contract/lua/lua.h"
#include "contract/lua/lualib.h"
#include "contract/lua/lauxlib.h"
#include "contract/lua/lobject.h"
#include "contract/lua/lstate.h"
#include "contract/lua/lopcodes.h"
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
		int ret = luaL_loadbuffer(L, data.c_str(), data_size, get_name().c_str());
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
			}
			//FC_ASSERT(ret == 0, "contract compile error");
		}
		stack_pos = lua_gettop(L);
		//printf("deploy 3 stack_pos=%d\n", stack_pos);
		return true;
	}

	bool contract_lua::call_method(const std::string& method, const variants& args, std::string& result)
	{
		return false;
	}

}}
