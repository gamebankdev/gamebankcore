#pragma once


struct lua_Extend {
	int opcode_limit;
	int opcode_execute_limit;
	int current_opcode_execute_count;
	int memory_limit;
	bool force_stop;
	char contract_name[17];
	char caller_name[17];
	void* pointer;
};