file(GLOB HEADERS "include/gamebank/plugins/contract/*.hpp" "include/gamebank/plugins/contract/lua/*.h")

set(SOURCES contract_lua.cpp
			contract_lualib.cpp
			contract_plugin.cpp
			include/gamebank/plugins/contract/lua/lapi.c
			include/gamebank/plugins/contract/lua/lauxlib.c
			include/gamebank/plugins/contract/lua/lbaselib.c
			include/gamebank/plugins/contract/lua/lbitlib.c
			include/gamebank/plugins/contract/lua/lcode.c
			include/gamebank/plugins/contract/lua/lcorolib.c
			include/gamebank/plugins/contract/lua/lctype.c
			include/gamebank/plugins/contract/lua/ldblib.c
			include/gamebank/plugins/contract/lua/ldebug.c
			include/gamebank/plugins/contract/lua/ldo.c
			include/gamebank/plugins/contract/lua/ldump.c
			include/gamebank/plugins/contract/lua/lfunc.c
			include/gamebank/plugins/contract/lua/lgc.c
			include/gamebank/plugins/contract/lua/linit.c
			include/gamebank/plugins/contract/lua/liolib.c
			include/gamebank/plugins/contract/lua/llex.c
			include/gamebank/plugins/contract/lua/lmathlib.c
			include/gamebank/plugins/contract/lua/lmem.c
			include/gamebank/plugins/contract/lua/loadlib.c
			include/gamebank/plugins/contract/lua/lobject.c
			include/gamebank/plugins/contract/lua/lopcodes.c
			include/gamebank/plugins/contract/lua/loslib.c
			include/gamebank/plugins/contract/lua/lparser.c
			include/gamebank/plugins/contract/lua/lstate.c
			include/gamebank/plugins/contract/lua/lstring.c
			include/gamebank/plugins/contract/lua/lstrlib.c
			include/gamebank/plugins/contract/lua/ltable.c
			include/gamebank/plugins/contract/lua/ltablib.c
			include/gamebank/plugins/contract/lua/ltm.c
			include/gamebank/plugins/contract/lua/lundump.c
			include/gamebank/plugins/contract/lua/lutf8lib.c
			include/gamebank/plugins/contract/lua/lvm.c
			include/gamebank/plugins/contract/lua/lzio.c
			include/gamebank/plugins/contract/lua/fpconv.c
			include/gamebank/plugins/contract/lua/lua_cjson.c
			include/gamebank/plugins/contract/lua/strbuf.c
			)

add_library( contract_plugin
             ${SOURCES}
             ${HEADERS}
           )

target_link_libraries( contract_plugin chain_plugin appbase gamebank_chain gamebank_utilities )
target_include_directories( contract_plugin
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/include/gamebank/plugins/contract" "${CMAKE_CURRENT_SOURCE_DIR}/include/gamebank/plugins/contract/lua" )

if( CLANG_TIDY_EXE )
   set_target_properties(
      contract_plugin PROPERTIES
      CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
   )
endif( CLANG_TIDY_EXE )

install( TARGETS
   contract_plugin

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
