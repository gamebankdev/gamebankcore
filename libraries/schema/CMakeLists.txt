file(GLOB HEADERS "include/gamebank/schema/*.hpp")
add_library( gamebank_schema schema.cpp ${HEADERS} )
target_link_libraries( gamebank_schema fc )
target_include_directories( gamebank_schema PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

install( TARGETS
   gamebank_schema

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
install( FILES ${HEADERS} DESTINATION "include/gamebank/schema" )
