file(GLOB HEADERS "include/contract/*.hpp" "include/contract/lua/*.h")

set(SOURCES contract_lua.cpp
			lua/lapi.c
			lua/lauxlib.c
			lua/lbaselib.c
			lua/lbitlib.c
			lua/lcode.c
			lua/lcorolib.c
			lua/lctype.c
			lua/ldblib.c
			lua/ldebug.c
			lua/ldo.c
			lua/ldump.c
			lua/lfunc.c
			lua/lgc.c
			lua/linit.c
			lua/liolib.c
			lua/llex.c
			lua/lmathlib.c
			lua/lmem.c
			lua/loadlib.c
			lua/lobject.c
			lua/lopcodes.c
			lua/loslib.c
			lua/lparser.c
			lua/lstate.c
			lua/lstring.c
			lua/lstrlib.c
			lua/ltable.c
			lua/ltablib.c
			lua/ltm.c
			lua/lundump.c
			lua/lutf8lib.c
			lua/lvm.c
			lua/lzio.c
			)

add_library( contract ${SOURCES} ${HEADERS} )

target_link_libraries( contract
	gamebank_protocol fc chainbase gamebank_schema appbase
                       ${PATCH_MERGE_LIB} )
target_include_directories( contract
                            PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/include/contract/lua" )

if( CLANG_TIDY_EXE )
   set_target_properties(
      contract PROPERTIES
      CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
   )
endif( CLANG_TIDY_EXE )

if(MSVC)
  set_source_files_properties( contract.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

INSTALL( TARGETS
   contract

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/contract" )
