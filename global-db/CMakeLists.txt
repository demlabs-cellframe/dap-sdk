cmake_minimum_required(VERSION 3.1)
project (dap_global_db C)

file(GLOB dap_global_db_SRC *.c)
file(GLOB dap_global_db_HDR include/*.h)

set(dap_global_db_LIBS dap_core dap_io dap_crypto dap_stream dap_json-c)

if(BUILD_WITH_GDB_DRIVER_MDBX)
    set(SAVED_CMAKE_MESSAGE_LOG_LEVEL ${CMAKE_MESSAGE_LOG_LEVEL})
    set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/libmdbx ${CMAKE_CURRENT_BINARY_DIR}/../3rdparty/libmdbx EXCLUDE_FROM_ALL)
    set(CMAKE_MESSAGE_LOG_LEVEL ${SAVED_CMAKE_MESSAGE_LOG_LEVEL})
    set(dap_global_db_LIBS ${dap_global_db_LIBS} mdbx-static)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_MDBX")
else()
    list(FILTER dap_global_db_SRC EXCLUDE REGEX "mdbx")
    list(FILTER dap_global_db_HDR EXCLUDE REGEX "mdbx")
endif()

if(BUILD_WITH_GDB_DRIVER_PGSQL)
    set(dap_global_db_LIBS ${dap_global_db_LIBS} pq)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_PGSQL")
else()
    list(FILTER dap_global_db_SRC EXCLUDE REGEX "pgsql")
    list(FILTER dap_global_db_HDR EXCLUDE REGEX "pgsql")
endif()

if(BUILD_WITH_GDB_DRIVER_SQLITE)
    set(dap_global_db_LIBS ${dap_global_db_LIBS} sqlite3)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_SQLITE")
else()
    list(FILTER dap_global_db_SRC EXCLUDE REGEX "sqlite")
    list(FILTER dap_global_db_HDR EXCLUDE REGEX "sqlite")
endif()

if(BUILD_WITH_GDB_DRIVER_CUTTDB)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/cuttdb ${CMAKE_CURRENT_BINARY_DIR}/../3rdparty/cuttdb)
    set(dap_global_db_LIBS ${dap_global_db_LIBS} dap_cuttdb)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_CUTTDB")
else()
    list(FILTER dap_global_db_SRC EXCLUDE REGEX "cdb")
    list(FILTER dap_global_db_HDR EXCLUDE REGEX "cdb")
endif()

add_library(${PROJECT_NAME} STATIC ${dap_global_db_SRC} ${dap_global_db_HDR})

target_link_libraries(${PROJECT_NAME} ${dap_global_db_LIBS})

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src)

