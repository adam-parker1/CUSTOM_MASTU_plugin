# Plugins to build

# Dependencies
set ( CMAKE_CXX_STANDARD 17 ) 
set ( CMAKE_CXX_STANDARD_REQUIRED ON )
set ( CMAKE_CXX17_STANDARD_COMPILE_OPTION "-std=c++17" )
set ( CMAKE_CXX17_EXTENSION_COMPILE_OPTION "-std=c++17" )
set ( CMAKE_CXX_EXTENSIONS ON )

set( CONFIGS
    custom_mastu.cfg
)

set( SOURCES
    custom_mastu.cpp
)

set( HEADERS
    custom_mastu.h
)

include( plugins )
uda_plugin(
    NAME CUSTOM_MASTU
    ENTRY_FUNC CustomMastu
    DESCRIPTION "Plugin to hold custom functionality needed for specific MAST-U mappings"
    EXAMPLE "CUSTOM_MASTU::get()"
    LIBNAME custom_mastu
    SOURCES custom_mastu.cpp
    CONFIG_FILE ${CONFIGS}
    EXTRA_INCLUDE_DIRS
      ${UDA_CLIENT_INCLUDE_DIRS}
      ${Boost_INCLUDE_DIRS}
    EXTRA_LINK_DIRS
      ${UDA_CLIENT_LIBRARY_DIRS}
      ${Boost_LIBRARY_DIRS}
    EXTRA_LINK_LIBS
      ${UDA_CLIENT_LIBRARIES}
      ${Boost_LIBRARIES}
      uda_cpp
)

list( TRANSFORM SOURCES PREPEND ${CMAKE_CURRENT_LIST_DIR}/ )
list( TRANSFORM HEADERS PREPEND ${CMAKE_CURRENT_LIST_DIR}/ )

set_property( TARGET clang-format APPEND PROPERTY SOURCES ${SOURCES} ${HEADERS} )
