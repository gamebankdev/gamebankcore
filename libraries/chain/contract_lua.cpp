#include <gamebank/chain/contract/contract_lua.hpp>
#include <fc/log/logger.hpp>
#include <gamebank/chain/database.hpp>
#include <gamebank/chain/contract/contract_lualib.hpp>
#include <gamebank/chain/contract/contract_object.hpp>
#include <gamebank/chain/contract/contract_user_object.hpp>

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

	namespace detail {
		class contract_lua_impl
		{
		public:
			contract_lua_impl(contract_lua& _contract) : contract(_contract)
			{
				L = luaL_newstate();
				luaL_openlibs(L);
				luaL_openlibs_contract(L);
			}
			~contract_lua_impl()
			{
				lua_close(L);
			}

			bool deploy(const std::string& data)
			{
				int stack_pos = lua_gettop(L);
				//dlog("deploy 1 stack_pos=%d\n", stack_pos);
				int data_size = data.length();
				std::string contract_name = contract.name;
				int ret = luaL_loadbuffer(L, data.c_str(), data_size, contract_name.c_str());
				if (ret != 0)
				{
					const char* str = lua_tostring(L, -1);
					if (str != nullptr)
					{
						elog("luaL_loadbuffer Error: ${err}", ("err", str));
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

			bool call_method(const std::string& method, const variants& args, std::string& result)
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
				if (lua_pcall(L, lua_gettop(L) - (oldStackPos + 1), LUA_MULTRET, 0) != 0)
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

			void save_modified_data()
			{
				//printf("save_modified_data\n" );
				chain::database* db = (chain::database*)(L->extend.pointer);
				lua_getglobal(L, "_modified_data");
				FC_ASSERT(lua_istable(L, -1), "_modified_data must be a table");
				lua_pushnil(L);
				while (lua_next(L, -2) != 0) {
					/* table, key, value */
					int keytype = lua_type(L, -2);
					int valuetype = lua_type(L, -1);
					FC_ASSERT(keytype == LUA_TSTRING, "key must be string");
					FC_ASSERT(valuetype == LUA_TTABLE, "key must be table");

					const char* key = lua_tostring(L, -2);
					std::string user_name(key);
					int datalen = 0;
					char* json = json_encode_tostring(L, &datalen);
					FC_ASSERT((json != nullptr) && (datalen > 0), "get user data from lua error");

					auto contract_data = db->find<contract_user_object, by_contract_user>(boost::make_tuple(L->extend.contract_name, user_name));
					if (contract_data == nullptr) {
						db->create< contract_user_object >([&](contract_user_object& obj)
						{
							obj.contract_name = string(L->extend.contract_name);
							obj.user_name = user_name;
							from_string(obj.data, json);
							obj.created = db->head_block_time();
							obj.last_update = obj.created;
						});
					}
					else {
						db->modify(*contract_data, [&](contract_user_object& obj)
						{
							from_string(obj.data, json);
							obj.last_update = db->head_block_time();
						});
					}

					lua_pop(L, 1);
				}
			}

		public:
			lua_State * L = nullptr;
			contract_lua& contract;
		};
	}

	contract_lua::contract_lua(account_name_type n) : contract_interface(n)
	{
		my = std::make_unique< detail::contract_lua_impl >(*this);
	}

	contract_lua::~contract_lua()
	{
	}

	bool contract_lua::deploy(const std::string& data)
	{
		return my->deploy(data);
	}

	bool contract_lua::call_method(const std::string& method, const variants& args, std::string& result)
	{
		return my->call_method(method, args, result);
	}

	void contract_lua::set_database(chain::database* db)
	{
		my->L->extend.pointer = db;
	}

}}
