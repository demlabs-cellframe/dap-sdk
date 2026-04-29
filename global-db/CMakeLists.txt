cmake_minimum_required(VERSION 3.10)
project (dap_global_db C)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LibraryHelpers.cmake)

file(GLOB dap_global_db_SRC dap_global_db.c dap_global_db_driver.c dap_global_db_ch.c dap_global_db_pkt.c dap_global_db_cluster.c dap_global_db_legacy.c)
file(GLOB dap_global_db_HDR include/*.h)

set(dap_global_db_LIBS dap_core dap_io dap_crypto dap_stream dap_link_manager dap_json-c dap_enc_server)

function(_dap_global_db_filter_mdbx_bad_linker_flags a_out_var)
    set(l_filtered_flags)
    foreach(l_flag IN LISTS ARGN)
        if(l_flag MATCHES "^-Wl,.*--(gc-sections|strip-all)(,|$)")
            continue()
        endif()
        list(APPEND l_filtered_flags "${l_flag}")
    endforeach()
    set(${a_out_var} "${l_filtered_flags}" PARENT_SCOPE)
endfunction()

function(_dap_global_db_filter_mdbx_bad_linker_flags_string a_out_var a_flags)
    separate_arguments(l_flags UNIX_COMMAND "${a_flags}")
    _dap_global_db_filter_mdbx_bad_linker_flags(l_filtered_flags ${l_flags})
    string(REPLACE ";" " " l_filtered_flags "${l_filtered_flags}")
    string(STRIP "${l_filtered_flags}" l_filtered_flags)
    set(${a_out_var} "${l_filtered_flags}" PARENT_SCOPE)
endfunction()

if(BUILD_WITH_GDB_DRIVER_MDBX)
    set(SAVED_CMAKE_MESSAGE_LOG_LEVEL ${CMAKE_MESSAGE_LOG_LEVEL})
    set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
    # Save user/toolchain flags and only filter project-injected linker flags
    # that interfere with libmdbx's check_function_exists() tests.
    set(SAVED_CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    set(SAVED_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    set(SAVED_CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    set(SAVED_CMAKE_REQUIRED_LINK_OPTIONS "${CMAKE_REQUIRED_LINK_OPTIONS}")
    _dap_global_db_filter_mdbx_bad_linker_flags_string(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    _dap_global_db_filter_mdbx_bad_linker_flags_string(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    _dap_global_db_filter_mdbx_bad_linker_flags_string(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    _dap_global_db_filter_mdbx_bad_linker_flags(CMAKE_REQUIRED_LINK_OPTIONS ${CMAKE_REQUIRED_LINK_OPTIONS})
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

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${dap_global_db_SRC} HEADERS ${dap_global_db_HDR})

# Link dependencies as INTERFACE for OBJECT library
# External libraries (pq, sqlite3, mdbx-static) will be linked at final library stage
# Use dap_link_libraries for automatic include propagation with cycle detection
dap_link_libraries(${PROJECT_NAME} INTERFACE ${dap_global_db_LIBS})

# Ensure XKCP is built before compiling global-db (needed for KeccakHash.h headers)
# dap_crypto depends on dap_crypto_XKCP, but we need to ensure BuildXKCP completes
if(TARGET dap_crypto_XKCP)
    add_dependencies(${PROJECT_NAME} dap_crypto_XKCP)
endif()

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)
# All dependency include directories (including dap_core with 3rdparty/uthash/src) are automatically propagated via dap_link_libraries

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${dap_global_db_HDR}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/global_db/
        ARCHIVE DESTINATION lib/dap/global_db/
        PUBLIC_HEADER DESTINATION include/dap/global_db/
)
endif()
