file(GLOB HEADERS "include/gamebank/chain/*.hpp" "include/gamebank/chain/util/*.hpp")

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

             ${HEADERS}
           )

target_link_libraries( gamebank_chain gamebank_protocol fc chainbase gamebank_schema appbase
                       ${PATCH_MERGE_LIB} )
target_include_directories( gamebank_chain
                            PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/include" )

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
