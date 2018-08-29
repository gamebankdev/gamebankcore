#include <gamebank/chain/contract/contract_lua.hpp>
#include <fc/log/logger.hpp>
#include <gamebank/chain/database.hpp>
#include <gamebank/chain/contract/contract_lualib.hpp>
#include <gamebank/chain/contract/contract_chain.hpp>
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
				luaL_openlibs_chain(L);

				// contract
				sys_functions.insert("contract");
				sys_functions.insert("chain");

				// baselib
				sys_functions.insert("assert");
				sys_functions.insert("error");
				sys_functions.insert("getmetatable");
				sys_functions.insert("ipairs");
				sys_functions.insert("next");
				sys_functions.insert("pairs");
				sys_functions.insert("print");
				sys_functions.insert("rawequal");
				sys_functions.insert("rawlen");
				sys_functions.insert("rawget");
				sys_functions.insert("rawset");
				sys_functions.insert("select");
				sys_functions.insert("setmetatable");
				sys_functions.insert("tonumber");
				sys_functions.insert("tostring");
				sys_functions.insert("type");

				// tablib
				sys_functions.insert("table");

				// strlib
				sys_functions.insert("string");

				// math
				sys_functions.insert("math");

				// utf8
				sys_functions.insert("utf8");
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

			bool is_sys_function(const char* name)
			{
				return sys_functions.find(name) != sys_functions.end();
			}

			void set_abi(const std::set<std::string>& method_names)
			{
				abi_method_names = method_names;
			}

			void set_extend(const string& contract_name, const string& caller_name)
			{
				strcpy(L->extend.contract_name, contract_name.c_str());
				L->extend.contract_name[contract_name.length()] = '\0';
				strcpy(L->extend.caller_name, caller_name.c_str());
				L->extend.caller_name[caller_name.length()] = '\0';
			}

			void set_extend_arg(int memory_limit, int opcode_limit)
			{
				if (memory_limit > GAMEBANK_CONTRACT_MAX_MEMORY)
					L->extend.memory_limit = GAMEBANK_CONTRACT_MAX_MEMORY;
				else
					L->extend.memory_limit = memory_limit;
				L->extend.opcode_execute_limit = opcode_limit;
			}

			int get_current_opcount()
			{
				return L->extend.current_opcode_execute_count;
			}

			bool compile_check(Proto* proto, Proto* parent_proto)
			{
				// opcodes
				for (int pc = 0; pc < proto->sizecode; ++pc)
				{
					Instruction i = proto->code[pc];
					OpCode o = GET_OPCODE(i);
					int a = GETARG_A(i);
					int b = GETARG_B(i);
					int c = GETARG_C(i);
					//int ax = GETARG_Ax(i);
					//int bx = GETARG_Bx(i);
					//int sbx = GETARG_sBx(i);
					int line = getfuncline(proto, pc);

					switch (o)
					{
					case OP_GETUPVAL:
					{
						if (proto->upvalues[b].name != nullptr) {
							const char* upvalue_name = getstr(proto->upvalues[b].name);
							if (is_global_var(upvalue_name)) {
								elog("cant access global var: ${var} line:${line}", ("var", upvalue_name)("line", line));
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
								elog("cant modify global var: ${var} line:${line}", ("var", upvalue_name)("line", line));
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
										elog("cant access global var: ${var} line:${line}", ("var", cname)("line", line));
										return false;
									}
									if (strcmp(cname, LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME) == 0) {
										elog("cant access global var: ${var} line:${line}", ("var", cname)("line", line));
										return false;
									}
									// check abi
									if (!is_sys_function(cname) && !is_abi(cname)) {
										elog("cant access global var: ${var} line:${line}", ("var", cname)("line", line));
										return false;
									}
								}
								else {
									elog("cant access global var: ${var} line:${line}", ("var", upvalue_name)("line", line));
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
										elog("cant modify global var: ${var} line:${line}", ("var", bname)("line", line));
										return false;
									}
									if (strcmp(bname, LUA_CONTRACT_MODIFIED_DATA_TABLE_NAME) == 0) {
										elog("cant modify global var: ${var} line:${line}", ("var", bname)("line", line));
										return false;
									}
									// check abi
									if ( !is_abi(bname)) {
										elog("cant modify global var: ${var} line:${line}", ("var", bname)("line", line));
										return false;
									}
								}
								else {
									elog("cant modify global var line:${line}", ("line", line));
									return false;
								}
							}
						}
					}
					break;
					case OP_CALL:
					{
						if (parent_proto == nullptr) {
							elog("cant call method when load line:${line}", ("line", line));
							return false;
						}
					}
					break;
					default:
						break;
					}
				}
				for (int i = 0; i < proto->sizep; ++i)
				{
					if (!compile_check(proto->p[i], proto))
						return false;
				}
				return true;
			}

			bool deploy(const std::string& data)
			{
				//int stack_pos = lua_gettop(L);
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
				//stack_pos = lua_gettop(L);
				int type = lua_type(L, -1);
				//printf("deploy 2 stack_pos=%d type=%d\n", stack_pos, type);
				LClosure* lc = clLvalue(L->top - 1);
				if (lc == nullptr || lc->p == nullptr)
				{
					return false;
				}
				if (!compile_check(lc->p, nullptr))
				{
					return false;
				}

				ret = lua_pcall(L, 0, LUA_MULTRET, 0);
				if (ret != 0)
				{
					if (L->extend.error_no != LUA_EXTEND_OK) {
						switch (L->extend.error_no)
						{
						case LUA_EXTEND_THROW:
							elog("lua_pcall Error: ${err}", ("err", "LUA_EXTEND_THROW"));
							break;
						case LUA_EXTEND_MEM_ERR:
							elog("lua_pcall Error: ${err}", ("err", "LUA_EXTEND_MEM_ERR"));
							break;
						case LUA_EXTEND_OPCODE_ERR:
							elog("lua_pcall Error: ${err}", ("err", "LUA_EXTEND_OPCODE_ERR"));
							break;
						default:
							break;
						}
					}
					else {
						const char* str = lua_tostring(L, -1);
						if (str)
						{
							//printf("lua_pcall: %s\n", str);
							elog("lua_pcall Error: ${err}", ("err", str));
							lua_pop(L, 1);
							return false;
						}
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
					if (arg.is_integer())
						lua_pushinteger(L, arg.as_int64());
					else if (arg.is_double())
						lua_pushnumber(L, arg.as_double());
					else if (arg.is_string())
						lua_pushstring(L, arg.as_string().c_str());
					else if (arg.is_null())
						lua_pushnil(L);
					else
						lua_pushstring(L, arg.as_string().c_str());
				}
				if (lua_pcall(L, lua_gettop(L) - (oldStackPos + 1), LUA_MULTRET, 0) != 0)
				{
					if (L->extend.error_no != LUA_EXTEND_OK) {
						switch (L->extend.error_no)
						{
						case LUA_EXTEND_THROW:
							elog("lua_pcall Error: ${err}", ("err", "LUA_EXTEND_THROW"));
							break;
						case LUA_EXTEND_MEM_ERR:
							elog("lua_pcall Error: ${err}", ("err", "LUA_EXTEND_MEM_ERR"));
							break;
						case LUA_EXTEND_OPCODE_ERR:
							elog("lua_pcall Error: ${err}", ("err", "LUA_EXTEND_OPCODE_ERR"));
							break;
						default:
							break;
						}
					} else {
						const char* str = lua_tostring(L, -1);
						if (str != NULL) {
							elog("lua_pcall Error: ${err}", ("err", str));
						}
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
					FC_ASSERT(valuetype == LUA_TTABLE, "value must be table");

					const char* key = lua_tostring(L, -2);
					std::string user_name(key);
					int datalen = 0;
					char* json = json_encode_tostring(L, &datalen);
					FC_ASSERT((json != nullptr) && (datalen > 0), "get user data from lua error");
					ilog("save contract_data ${contract_name}.${user_name}:${data}", ("contract_name", L->extend.contract_name)("user_name", user_name)("data", json));

					auto contract_data = db->find<contract_user_object, by_contract_user>(boost::make_tuple(L->extend.contract_name, user_name));
					if (contract_data == nullptr) {
						db->create< contract_user_object >([&](contract_user_object& obj)
						{
							obj.contract_name = string(L->extend.contract_name);
							obj.user_name = user_name;
							//from_string(obj.data, json);
							obj.data.assign(json, datalen);
							obj.created = db->head_block_time();
							obj.last_update = obj.created;
						});
					}
					else {
						db->modify(*contract_data, [&](contract_user_object& obj)
						{
							obj.data.assign(json, datalen);
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
			std::set<std::string> sys_functions;
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

	void contract_lua::set_extend(const account_name_type& contract_name, const account_name_type& caller_name)
	{
		return my->set_extend(contract_name, caller_name);
	}

	void contract_lua::set_extend_arg(int memory_limit, int opcode_limit)
	{
		return my->set_extend_arg(memory_limit, opcode_limit);
	}

	int contract_lua::get_current_opcount()
	{
		return my->get_current_opcount();
	}

}}
