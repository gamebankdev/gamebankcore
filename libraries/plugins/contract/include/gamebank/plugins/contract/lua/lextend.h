#ifndef lextend_h
#define lextend_h
#pragma once

typedef struct lua_Extend {
	int opcode_limit;
	int opcode_execute_limit;
	int current_opcode_execute_count;
	int memory_limit;
	int force_stop;
	char contract_name[17];
	char caller_name[17];
	void* pointer;
} lua_Extend;

#endif