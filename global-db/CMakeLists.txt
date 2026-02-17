cmake_minimum_required(VERSION 3.10)
project (dap_global_db C)

file(GLOB dap_global_db_SRC dap_global_db.c dap_global_db_driver.c dap_global_db_ch.c dap_global_db_pkt.c dap_global_db_cluster.c dap_global_db_legacy.c)
file(GLOB dap_global_db_HDR include/*.h)

set(dap_global_db_LIBS dap_core dap_io dap_crypto dap_stream dap_link_manager dap_json-c)

if(BUILD_WITH_GDB_DRIVER_MDBX)
    set(SAVED_CMAKE_MESSAGE_LOG_LEVEL ${CMAKE_MESSAGE_LOG_LEVEL})
    set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
    # Save flags that interfere with libmdbx's check_function_exists() tests
    # The -Wl,--strip-all and -Wl,--gc-sections in CMAKE_C_FLAGS break function checks
    set(SAVED_CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    set(SAVED_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    set(SAVED_CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    set(SAVED_CMAKE_REQUIRED_LINK_OPTIONS "${CMAKE_REQUIRED_LINK_OPTIONS}")
    # Remove problematic linker flags from C flags for the duration of MDBX configuration
    # Remove all -Wl, flags (linker flags passed via compiler) that break check_function_exists
    # Convert to list, filter out -Wl, flags, then convert back
    separate_arguments(CMAKE_C_FLAGS_LIST UNIX_COMMAND "${CMAKE_C_FLAGS}")
    list(FILTER CMAKE_C_FLAGS_LIST EXCLUDE REGEX "^-Wl,")
    string(REPLACE ";" " " CMAKE_C_FLAGS "${CMAKE_C_FLAGS_LIST}")
    string(STRIP "${CMAKE_C_FLAGS}" CMAKE_C_FLAGS)
    # Clear flags used by check_function_exists
    set(CMAKE_REQUIRED_FLAGS "")
    set(CMAKE_EXE_LINKER_FLAGS "")
    set(CMAKE_REQUIRED_LINK_OPTIONS "")
    list(APPEND dap_global_db_SRC dap_global_db_driver_mdbx.c)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/libmdbx ${CMAKE_CURRENT_BINARY_DIR}/../3rdparty/libmdbx EXCLUDE_FROM_ALL)
    # Restore all flags
    set(CMAKE_C_FLAGS "${SAVED_CMAKE_C_FLAGS}")
    set(CMAKE_REQUIRED_FLAGS "${SAVED_CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${SAVED_CMAKE_EXE_LINKER_FLAGS}")
    set(CMAKE_REQUIRED_LINK_OPTIONS "${SAVED_CMAKE_REQUIRED_LINK_OPTIONS}")
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
