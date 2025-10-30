cmake_minimum_required(VERSION 3.10)
project (dap_global_db C)

file(GLOB dap_global_db_SRC dap_global_db.c dap_global_db_driver.c dap_global_db_ch.c dap_global_db_pkt.c dap_global_db_cluster.c dap_global_db_legacy.c)
file(GLOB dap_global_db_HDR include/*.h)

set(dap_global_db_LIBS dap_core dap_io dap_crypto dap_stream dap_link_manager dap_json-c)

if(BUILD_WITH_GDB_DRIVER_MDBX)
    set(SAVED_CMAKE_MESSAGE_LOG_LEVEL ${CMAKE_MESSAGE_LOG_LEVEL})
    set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
    list(APPEND dap_global_db_SRC dap_global_db_driver_mdbx.c)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/libmdbx ${CMAKE_CURRENT_BINARY_DIR}/../3rdparty/libmdbx EXCLUDE_FROM_ALL)
    set(CMAKE_MESSAGE_LOG_LEVEL ${SAVED_CMAKE_MESSAGE_LOG_LEVEL})
    set(dap_global_db_LIBS ${dap_global_db_LIBS} mdbx-static)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_MDBX")
endif()

if(BUILD_WITH_GDB_DRIVER_PGSQL)
    list(APPEND dap_global_db_SRC dap_global_db_driver_pgsql.c)
    set(dap_global_db_LIBS ${dap_global_db_LIBS} pq)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_PGSQL")
    if(WIN32)
        set(dap_global_db_LIBS ${dap_global_db_LIBS} ssl crypto z pgcommon pgport )
    endif()
endif()

if(BUILD_WITH_GDB_DRIVER_SQLITE)
    list(APPEND dap_global_db_SRC dap_global_db_driver_sqlite.c)
    set(dap_global_db_LIBS ${dap_global_db_LIBS} sqlite3)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_SQLITE")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSQLITE_THREADSAFE=2")
endif()

add_library(${PROJECT_NAME} STATIC ${dap_global_db_SRC} ${dap_global_db_HDR})

target_link_libraries(${PROJECT_NAME} ${dap_global_db_LIBS})

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${dap_global_db_HDR}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/global_db/
        ARCHIVE DESTINATION lib/dap/global_db/
        PUBLIC_HEADER DESTINATION include/dap/global_db/
)
endif()
