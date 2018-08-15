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
#include "gamebank/chain/contract/lua/ldebug.h"
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

			bool is_global_var(const char* upvalue_name)
			{
				if (strcmp(upvalue_name, "_G") == 0
					|| strcmp(upvalue_name, "_ENV") == 0)
				{
					return true;
				}
				return false;
			}

			bool is_abi(const char* name)
			{
				return abi_method_names.find(name) != abi_method_names.end();
			}

			void set_abi(const std::set<std::string>& method_names)
			{
				abi_method_names = method_names;
			}

			bool compile_check(Proto* proto)
			{
				// opcodes
				for (int pc = 0; pc < proto->sizecode; ++pc)
				{
					Instruction i = proto->code[pc];
					OpCode o = GET_OPCODE(i);
					int a = GETARG_A(i);
					int b = GETARG_B(i);
					int c = GETARG_C(i);
					int ax = GETARG_Ax(i);
					int bx = GETARG_Bx(i);
					int sbx = GETARG_sBx(i);
					int line = getfuncline(proto, pc);

					switch (o)
					{
					case OP_GETUPVAL:
					{
						if (proto->upvalues[b].name != nullptr) {
							const char* upvalue_name = getstr(proto->upvalues[b].name);
							if (is_global_var(upvalue_name)) {
								elog("cant access global var: ${var}", ("var", upvalue_name));
								return false;
							}
						}
					}
					break;
					case OP_SETUPVAL:
					{
						if (proto->upvalues[b].name != nullptr) {
							const char* upvalue_name = getstr(proto->upvalues[b].name);
							if (is_global_var(upvalue_name)) {
								elog("cant modify global var: ${var}", ("var", upvalue_name));
								return false;
							}
						}
						break;
					}
					case OP_GETTABUP:
					{
						if (proto->upvalues[b].name != nullptr) {
							const char* upvalue_name = getstr(proto->upvalues[b].name);
							if (is_global_var(upvalue_name)) {
								if (ISK(c)) {
									int cidx = INDEXK(c);
									const char* cname = getstr(tsvalue(&proto->k[cidx]));
									if (is_global_var(cname)) {
										elog("cant access global var: ${var}", ("var", cname));
										return false;
									}
									if (strcmp(cname, LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME) == 0) {
										elog("cant access global var: ${var}", ("var", cname));
										return false;
									}
									// check abi
									if (!is_abi(cname)) {
										elog("cant access global var: ${var}", ("var", cname));
										return false;
									}
								}
								else {
									elog("cant access global var: ${var}", ("var", upvalue_name));
									return false;
								}
							}
						}
						break;
					}
					case OP_SETTABUP:
					{
						if (proto->upvalues[a].name != nullptr) {
							const char* upvalue_name = getstr(proto->upvalues[a].name);
							if (is_global_var(upvalue_name)) {
								if (ISK(b)) {
									int bidx = INDEXK(b);
									const char* bname = getstr(tsvalue(&proto->k[bidx]));
									if (is_global_var(bname)) {
										elog("cant modify global var: ${var}", ("var", bname));
										return false;
									}
									if (strcmp(bname, LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME) == 0) {
										elog("cant modify global var: ${var}", ("var", LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME));
										return false;
									}
									// check abi
									if (!is_abi(bname)) {
										elog("cant modify global var: ${var}", ("var", bname));
										return false;
									}
								}
								else {
									elog("cant modify global var");
									return false;
								}
							}
						}
					}
					break;
					default:
						break;
					}
				}
				for (int i = 0; i < proto->sizep; ++i)
				{
					if (!compile_check(proto->p[i]))
						return false;
				}
				return true;
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
				if (lc == nullptr || lc->p == nullptr)
				{
					return false;
				}
				if (!compile_check(lc->p))
				{
					return false;
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
				chain::database* db = (chain::database*)(L->extend.pointer);
				lua_getglobal(L, LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME);
				FC_ASSERT(lua_istable(L, -1), "_contract_modified_data must be a table");
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
			std::set<std::string> abi_method_names;
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

	void contract_lua::set_abi(const std::set<std::string>& method_names)
	{
		return my->set_abi(method_names);
	}

}}
