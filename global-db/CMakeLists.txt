cmake_minimum_required(VERSION 3.1)
project (dap_global_db C)

file(GLOB dap_global_db_SRC *.c)
file(GLOB dap_global_db_HDR include/*.h)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/cuttdb ${CMAKE_CURRENT_BINARY_DIR}/../3rdparty/cuttdb)
set(dap_global_db_LIBS dap_core dap_io dap_crypto dap_chain dap_chain_net sqlite3 dap_cuttdb json-c)

if(BUILD_WITH_GDB_DRIVER_MDBX)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/libmdbx ${CMAKE_CURRENT_BINARY_DIR}/../3rdparty/libmdbx EXCLUDE_FROM_ALL)
    file(GLOB dap_global_db_SRC ${dap_global_db_SRC} dap_global_db_driver_mdbx.c)
    file(GLOB dap_global_db_HRR ${dap_global_db_HDR} include/dap_global_db_driver_mdbx.h)
    set(dap_global_db_LIBS ${dap_global_db_LIBS} mdbx-static)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_MDBX")
else()
    list(FILTER dap_global_db_SRC EXCLUDE REGEX "mdbx")
    list(FILTER dap_global_db_HDR EXCLUDE REGEX "mdbx")
endif()

if(BUILD_WITH_GDB_DRIVER_PGSQL)
    set(dap_global_db_LIBS ${dap_global_db_LIBS} pq)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_PGSQL")
endif()

add_library(${PROJECT_NAME} STATIC ${dap_global_db_SRC} ${dap_global_db_HDR})

target_link_libraries(${PROJECT_NAME} ${dap_global_db_LIBS})

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src)

