file(GLOB HEADERS "include/gamebank/chain/*.hpp" "include/gamebank/chain/util/*.hpp" "include/gamebank/chain/contract/lua/*.h")

## SORT .cpp by most likely to change / break compile
add_library( gamebank_chain

             # As database takes the longest to compile, start it first
             database.cpp

             gamebank_evaluator.cpp
             gamebank_objects.cpp

             witness_schedule.cpp
             fork_database.cpp

             shared_authority.cpp
             block_log.cpp

             generic_custom_operation_interpreter.cpp

             util/reward.cpp
             util/impacted.cpp
             util/advanced_benchmark_dumper.cpp
			 
			contract_lua.cpp
			contract_lualib.cpp
			contract_chain.cpp
			include/gamebank/chain/contract/lua/lapi.c
			include/gamebank/chain/contract/lua/lauxlib.c
			include/gamebank/chain/contract/lua/lbaselib.c
			include/gamebank/chain/contract/lua/lbitlib.c
			include/gamebank/chain/contract/lua/lcode.c
			include/gamebank/chain/contract/lua/lcorolib.c
			include/gamebank/chain/contract/lua/lctype.c
			include/gamebank/chain/contract/lua/ldblib.c
			include/gamebank/chain/contract/lua/ldebug.c
			include/gamebank/chain/contract/lua/ldo.c
			include/gamebank/chain/contract/lua/ldump.c
			include/gamebank/chain/contract/lua/lextend.c
			include/gamebank/chain/contract/lua/lfunc.c
			include/gamebank/chain/contract/lua/lgc.c
			include/gamebank/chain/contract/lua/linit.c
			include/gamebank/chain/contract/lua/liolib.c
			include/gamebank/chain/contract/lua/llex.c
			include/gamebank/chain/contract/lua/lmathlib.c
			include/gamebank/chain/contract/lua/lmem.c
			include/gamebank/chain/contract/lua/loadlib.c
			include/gamebank/chain/contract/lua/lobject.c
			include/gamebank/chain/contract/lua/lopcodes.c
			include/gamebank/chain/contract/lua/loslib.c
			include/gamebank/chain/contract/lua/lparser.c
			include/gamebank/chain/contract/lua/lstate.c
			include/gamebank/chain/contract/lua/lstring.c
			include/gamebank/chain/contract/lua/lstrlib.c
			include/gamebank/chain/contract/lua/ltable.c
			include/gamebank/chain/contract/lua/ltablib.c
			include/gamebank/chain/contract/lua/ltm.c
			include/gamebank/chain/contract/lua/lundump.c
			include/gamebank/chain/contract/lua/lutf8lib.c
			include/gamebank/chain/contract/lua/lvm.c
			include/gamebank/chain/contract/lua/lzio.c
			include/gamebank/chain/contract/lua/fpconv.c
			include/gamebank/chain/contract/lua/lua_cjson.c
			include/gamebank/chain/contract/lua/strbuf.c

             ${HEADERS}
           )

target_link_libraries( gamebank_chain gamebank_protocol fc chainbase gamebank_schema appbase
                       ${PATCH_MERGE_LIB} )
target_include_directories( gamebank_chain
                            PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/include/gamebank/chain/contract/lua" "${CMAKE_CURRENT_BINARY_DIR}/include"  )

if( CLANG_TIDY_EXE )
   set_target_properties(
      gamebank_chain PROPERTIES
      CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
   )
endif( CLANG_TIDY_EXE )

if(MSVC)
  set_source_files_properties( database.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

INSTALL( TARGETS
   gamebank_chain

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/gamebank/chain" )
