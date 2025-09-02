cmake_minimum_required(VERSION 3.10)
project(DAP_SDK_NATIVE C)

# Отключение SSL по умолчанию - WolfSSL больше не используется
add_definitions(-DDAP_NET_CLIENT_NO_SSL)

# Отключение SSL по умолчанию - WolfSSL больше не используется
add_definitions(-DDAP_NET_CLIENT_NO_SSL)
set(DAP_SDK_NATIVE_VERSION "3.0-0")

# Build type detection
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Common includes and definitions
include(cmake/OS_Detection.cmake)


# =========================================  
# INTERNAL MODULE CREATION MACRO
# =========================================
# Creates internal OBJECT modules (always OBJECT for combining)
# Usage: dap_add_library(target_name sources... HEADERS headers...)
macro(dap_add_library TARGET_NAME)
    cmake_parse_arguments(DAP_LIB "" "" "HEADERS" ${ARGN})
    
    # Always create modules as OBJECT for combining into final library
    add_library(${TARGET_NAME} OBJECT ${DAP_LIB_UNPARSED_ARGUMENTS} ${DAP_LIB_HEADERS})
    
    # Track modules for final library creation
    if(NOT DEFINED DAP_INTERNAL_MODULES)
        set(DAP_INTERNAL_MODULES "")
    endif()
    list(APPEND DAP_INTERNAL_MODULES ${TARGET_NAME})
    set(DAP_INTERNAL_MODULES ${DAP_INTERNAL_MODULES} CACHE INTERNAL "List of DAP internal modules")
    
    message("[+] DAP Internal Module: ${TARGET_NAME} (object)")
endmacro()

# =========================================
# FINAL LIBRARY CREATION
# =========================================  
# Creates final combined DAP SDK library from OBJECT modules
macro(dap_create_final_library)
    if(DEFINED DAP_INTERNAL_MODULES)
        message("[+] Creating final DAP SDK library: dap_sdk")
        message("[+] Internal OBJECT modules to combine: ${DAP_INTERNAL_MODULES}")
        
        # Create object file lists for each module
        set(DAP_ALL_OBJECTS "")
        foreach(MODULE ${DAP_INTERNAL_MODULES})
            list(APPEND DAP_ALL_OBJECTS $<TARGET_OBJECTS:${MODULE}>)
        endforeach()
        
        # Add DAP SDK unified interface
        list(APPEND DAP_ALL_OBJECTS $<TARGET_OBJECTS:dap_sdk_interface>)
        
        # Create final library
        add_library(dap_sdk ${DAP_FINAL_LIB_TYPE} ${DAP_ALL_OBJECTS})
        
        # Link external static libraries into final library
        target_link_libraries(dap_sdk PRIVATE
            dap_json-c
            dap_crypto_kyber512
            dap_crypto_XKCP
            dap_crypto
            dap_io
            dap_client
            dap_link_manager
            dap_cli_server
            dap_dns_server
            dap_enc_server
            dap_http_server
            dap_json_rpc
            dap_notify_srv
            dap_stream
            dap_stream_ch
            dap_session
        )
        
        # Link MDBX only if enabled
        if(BUILD_WITH_GDB_DRIVER_MDBX)
            target_link_libraries(dap_sdk PRIVATE mdbx-static)
        endif()
        
        # Link macOS frameworks for network monitoring
        if(MACOS)
            target_link_libraries(dap_sdk PRIVATE "-framework CoreFoundation" "-framework SystemConfiguration")
            message("[+] macOS frameworks linked: CoreFoundation, SystemConfiguration")
        endif()
        
        message("[+] Final DAP SDK Library: dap_sdk created from real object files")
    else()
        message("[!] No DAP internal modules found!")
    endif()
endmacro()

# Options
option(BUILD_SHARED "Build final DAP SDK library as shared (default)" ON)
option(BUILD_EXAMPLES "Build examples" OFF) # we don't need them by default
option(BUILD_TESTS "Build tests" OFF)

# Global DB drivers - MDBX is default for all platforms
option(BUILD_WITH_GDB_DRIVER_MDBX "Build with MDBX driver for Global DB (default)" ON)
option(BUILD_WITH_GDB_DRIVER_SQLITE "Build with SQLite driver for Global DB" OFF)  
option(BUILD_WITH_GDB_DRIVER_PGSQL "Build with PostgreSQL driver for Global DB" OFF)

# Internal modules are ALWAYS static, final can be SHARED or STATIC
if(BUILD_SHARED)
    message("[+] DAP SDK: Building final library as SHARED (modules: static internally)")
    set(DAP_FINAL_LIB_TYPE SHARED)
else()
    message("[+] DAP SDK: Building final library as STATIC (modules: static internally)")
    set(DAP_FINAL_LIB_TYPE STATIC)
endif()

# Define DAP_SDK_ONLY to distinguish from full Cellframe builds
add_definitions(-DDAP_SDK_ONLY)

# Set default modules if none are specified
if (NOT DEFINED DAPSDK_MODULES OR DAPSDK_MODULES STREQUAL "")
    set(DAPSDK_MODULES "crypto plugin io network-core network-server network-client network-link_manager global-db")
    message("[+] Using default modules: ${DAPSDK_MODULES}")
endif()

# Core is always required - UPDATED PATH
add_subdirectory(module/core)

# Add DAP SDK unified interface
add_library(dap_sdk_interface OBJECT dap_sdk.c)
target_include_directories(dap_sdk_interface PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/module/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/module/crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/module/io/include
    ${CMAKE_CURRENT_SOURCE_DIR}/module/global-db/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/uthash/src
)

# Optional modules with UPDATED PATHS
if (DAPSDK_MODULES MATCHES "crypto")
    add_subdirectory(module/crypto)
    message("[+] Module 'crypto'")
endif()

if (DAPSDK_MODULES MATCHES "plugin")
    add_subdirectory(module/plugin)
    message("[+] Module 'plugin'")
endif()

if (DAPSDK_MODULES MATCHES "io")
    add_subdirectory(module/io)
    message("[+] Module 'io'")
endif()

if (DAPSDK_MODULES MATCHES "network-core")
    add_subdirectory(module/net/stream)
    message("[+] Module 'stream'")
endif()

if (DAPSDK_MODULES MATCHES "network-link_manager")
    add_subdirectory(module/net/link_manager)
    message("[+] Module 'link manager'")
endif()

if (DAPSDK_MODULES MATCHES "network-client")
    add_subdirectory(module/net/client)
    message("[+] Module 'client'")
endif()

if (DAPSDK_MODULES MATCHES "network-server")
    add_subdirectory(module/net/server)
    message("[+] Module 'server'")
endif()

if (DAPSDK_MODULES MATCHES "global-db")
    # Check if at least one Global DB driver is enabled
    if(NOT BUILD_WITH_GDB_DRIVER_MDBX AND NOT BUILD_WITH_GDB_DRIVER_SQLITE AND NOT BUILD_WITH_GDB_DRIVER_PGSQL)
        message(FATAL_ERROR "At least one Global DB driver must be enabled (MDBX, SQLite, or PostgreSQL)")
    endif()
    
    add_subdirectory(module/global-db)
    message("[+] Module 'global-db'")
    if(BUILD_WITH_GDB_DRIVER_MDBX)
        message("[+] Global DB: MDBX driver enabled")
    endif()
    if(BUILD_WITH_GDB_DRIVER_SQLITE)
        message("[+] Global DB: SQLite driver enabled")
    endif()
    if(BUILD_WITH_GDB_DRIVER_PGSQL)
        message("[+] Global DB: PostgreSQL driver enabled")
    endif()
endif()



# Examples - UPDATED PATH - DISABLED
# if(BUILD_EXAMPLES)
#     # add_subdirectory(module/examples) # Отключено - зависит от Cellframe
#     # message("[+] Module 'examples'")
# endif()

# Test framework - UPDATED PATH
if (DAPSDK_MODULES MATCHES "test-framework" OR BUILD_TESTS)
    enable_testing()
    add_subdirectory(module/test)
    message("[+] Testing framework")
endif()

# New tests structure - UPDATED PATHS
if(BUILD_TESTS)
    add_subdirectory(tests)
    message("[+] Tests enabled with new structure")
endif()

# Create final combined DAP SDK library
dap_create_final_library()

message("[+] =========================================")
message("[+] DAP SDK with module/ structure configured")
message("[+] =========================================")
