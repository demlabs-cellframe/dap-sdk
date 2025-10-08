cmake_minimum_required(VERSION 3.10)
project (dap_global_db C)

file(GLOB dap_global_db_SRC dap_global_db.c dap_global_db_driver.c dap_global_db_ch.c dap_global_db_pkt.c dap_global_db_cluster.c)
file(GLOB dap_global_db_HDR include/*.h)

# Dependencies - removed link_manager (breaks cycle)
# - Link to dap_stream for stream cluster functionality
# - Link to dap_net_common for common types and callback registry
set(dap_global_db_LIBS dap_core dap_io dap_crypto dap_json-c dap_net_common dap_enc_server dap_json_rpc dap_stream)

if(BUILD_WITH_GDB_DRIVER_MDBX)
    set(SAVED_CMAKE_MESSAGE_LOG_LEVEL ${CMAKE_MESSAGE_LOG_LEVEL})
    set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
    list(APPEND dap_global_db_SRC dap_global_db_driver_mdbx.c)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/libmdbx ${CMAKE_CURRENT_BINARY_DIR}/../../3rdparty/libmdbx EXCLUDE_FROM_ALL)
    # Only for Android builds of libmdbx, define _GNU_SOURCE to avoid android_compat mntent redefinition
    if(ANDROID)
        target_compile_definitions(mdbx-static PRIVATE _GNU_SOURCE)
    endif()
    set(CMAKE_MESSAGE_LOG_LEVEL ${SAVED_CMAKE_MESSAGE_LOG_LEVEL})
    set(dap_global_db_LIBS ${dap_global_db_LIBS} mdbx-static)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_MDBX")
endif()

if(BUILD_WITH_GDB_DRIVER_PGSQL)
    list(APPEND dap_global_db_SRC dap_global_db_driver_pgsql.c)
    set(dap_global_db_LIBS ${dap_global_db_LIBS} pq)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_PGSQL")
    if(WIN32)
        set(dap_global_db_LIBS ${dap_global_db_LIBS} ssl crypto z pgcommon pgport)
    endif()
endif()

if(BUILD_WITH_GDB_DRIVER_SQLITE)
    list(APPEND dap_global_db_SRC dap_global_db_driver_sqlite.c)
    set(dap_global_db_LIBS ${dap_global_db_LIBS} sqlite3)
    add_definitions("-DDAP_CHAIN_GDB_ENGINE_SQLITE")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSQLITE_THREADSAFE=2")
endif()

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${dap_global_db_SRC} HEADERS ${dap_global_db_HDR})

target_link_libraries(${PROJECT_NAME} ${dap_global_db_LIBS})

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src)

# All other includes auto-propagate via linked modules (dap_net_common provides base types)

# Export dependencies for main SDK build
set(DAP_GLOBAL_DB_DEPS "${dap_global_db_LIBS}" CACHE INTERNAL "dap_global_db external dependencies")

if (BUILD_GLOBAL_DB_TEST)
    add_subdirectory(test)
endif()
if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${dap_global_db_HDR}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/global_db/
        ARCHIVE DESTINATION lib/dap/global_db/
        PUBLIC_HEADER DESTINATION include/dap/global_db/
)
endif()
