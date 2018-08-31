#include "lextend.h"
#include <stddef.h>
#include <string.h>

void init_extend(lua_Extend* extend) {
	// init lua_Extend
	extend->current_opcode_execute_count = 0;
	extend->force_stop = 0;
	extend->memory_limit = 0;
	extend->opcode_execute_limit = 0;
	extend->opcode_limit = 0;
	extend->error_no = LUA_EXTEND_OK;
	memset(extend->contract_name, 0, sizeof(extend->contract_name));
	memset(extend->caller_name, 0, sizeof(extend->caller_name));
	extend->pointer = NULL;
}

void set_extend_error(lua_Extend* extend, int err) {
	extend->error_no = err;
	if (err != LUA_EXTEND_OK) {
		extend->force_stop = 1;
	}
}

