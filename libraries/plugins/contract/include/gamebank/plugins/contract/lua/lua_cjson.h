#pragma once

#include "lua.h"

LUALIB_API int json_encode(lua_State *l);
LUALIB_API char* json_encode_tostring(lua_State *l, int* datalen);
LUALIB_API int json_decode(lua_State *l);
LUALIB_API int json_decode_fromstring(lua_State *l, const char* json_str, int json_str_len);
