#ifndef lextend_h
#define lextend_h
#pragma once

#define LUA_EXTEND_OK			0
#define LUA_EXTEND_THROW		1
#define LUA_EXTEND_MEM_ERR		2
#define LUA_EXTEND_OPCODE_ERR	3

typedef struct lua_Extend {
	int opcode_limit;
	int opcode_execute_limit;
	int current_opcode_execute_count;
	int memory_limit;
	int force_stop;
	int error_no;
	char contract_name[17];
	char caller_name[17];
	void* pointer;
} lua_Extend;

void init_extend(lua_Extend* extend);

void set_extend_error(lua_Extend* extend, int err);

#endif