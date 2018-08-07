add_executable( gamebankd main.cpp )
if( UNIX AND NOT APPLE )
  set(rt_library rt )
endif()

find_package( Gperftools QUIET )
if( GPERFTOOLS_FOUND )
    message( STATUS "Found gperftools; compiling gamebankd with TCMalloc")
    list( APPEND PLATFORM_SPECIFIC_LIBS tcmalloc )
endif()

if( GAMEBANK_STATIC_BUILD )
   target_link_libraries( gamebankd PRIVATE
      "-static-libstdc++ -static-libgcc"
      appbase
      gamebank_utilities
      gamebank_plugins
      ${CMAKE_DL_LIBS}
      ${PLATFORM_SPECIFIC_LIBS}
   )
else( GAMEBANK_STATIC_BUILD )
   target_link_libraries( gamebankd PRIVATE
      appbase
      gamebank_utilities
      gamebank_plugins
      ${CMAKE_DL_LIBS}
      ${PLATFORM_SPECIFIC_LIBS}
   )
endif( GAMEBANK_STATIC_BUILD )

if( CLANG_TIDY_EXE )
   set_target_properties(
      gamebankd PROPERTIES
      CXX_CLANG_TIDY "${DO_CLANG_TIDY}"
   )
endif( CLANG_TIDY_EXE )

install( TARGETS
   gamebankd

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
