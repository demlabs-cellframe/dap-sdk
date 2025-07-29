cmake_minimum_required(VERSION 3.10)
project(DAP_SDK_NATIVE C)
set(DAP_SDK_NATIVE_VERSION "3.0-0")

# =========================================
# UNIVERSAL BUILD SYSTEM - DAP SDK
# =========================================
# DAP SDK always builds its modules as STATIC internally,
# then combines them into one big STATIC or SHARED library

# Default: BUILD_SHARED=ON (final library is shared)
# Override: BUILD_SHARED=OFF (final library is static)
option(BUILD_SHARED "Build final DAP SDK library as shared (default)" ON)

# Internal modules are ALWAYS static
set(DAP_INTERNAL_LIB_TYPE STATIC)

if(BUILD_SHARED)
    message("[+] DAP SDK: Building final library as SHARED (modules: static internally)")
    set(DAP_FINAL_LIB_TYPE SHARED)
    set(DAP_BUILD_MODE "shared")
else()
    message("[+] DAP SDK: Building final library as STATIC (modules: static internally)")
    set(DAP_FINAL_LIB_TYPE STATIC)
    set(DAP_BUILD_MODE "static")
endif()

# Export build mode for consumers
set(DAP_SDK_BUILD_MODE ${DAP_BUILD_MODE} CACHE STRING "DAP SDK final library mode (shared/static)")
set(DAP_SDK_INTERNAL_MODE "static" CACHE STRING "DAP SDK internal modules mode (always static)")

# =========================================
# BUILD INHERITANCE PROPAGATION
# =========================================
# When forced static, propagate to all subprojects
if(DAP_FORCED_STATIC)
    set(FORCE_SUBMODULE_STATIC ON CACHE BOOL "Propagate static build to children" FORCE)
    message("[+] DAP SDK: Propagating FORCE_SUBMODULE_STATIC to all children")
endif()

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
# FINAL LIBRARY CREATION (will be called at the end)
# =========================================
# Creates final combined DAP SDK library from OBJECT modules
macro(dap_create_final_library)
    if(DEFINED DAP_INTERNAL_MODULES)
        message("[+] Creating final DAP SDK library: dap_sdk (${DAP_BUILD_MODE})")
        message("[+] Internal OBJECT modules to combine: ${DAP_INTERNAL_MODULES}")
        
        # Collect all object files from internal modules
        set(DAP_ALL_OBJECTS "")
        foreach(module ${DAP_INTERNAL_MODULES})
            list(APPEND DAP_ALL_OBJECTS $<TARGET_OBJECTS:${module}>)
        endforeach()
        
        # Create final library from all object files (with real sources)
        add_library(dap_sdk ${DAP_FINAL_LIB_TYPE} ${DAP_ALL_OBJECTS})
        
        # Set properties for final library
        if(BUILD_SHARED)
            set_target_properties(dap_sdk PROPERTIES
                POSITION_INDEPENDENT_CODE ON
                VERSION ${DAP_SDK_NATIVE_VERSION}
                SOVERSION 2
            )
            target_compile_definitions(dap_sdk PRIVATE DAP_BUILDING_LIB)
            target_compile_definitions(dap_sdk INTERFACE DAP_USING_SHARED)
        else()
            target_compile_definitions(dap_sdk INTERFACE DAP_USING_STATIC)
        endif()
        
        # Inherit include directories from internal modules
        foreach(module ${DAP_INTERNAL_MODULES})
            # Get include directories only
            get_target_property(module_includes ${module} INTERFACE_INCLUDE_DIRECTORIES)
            if(module_includes)
                target_include_directories(dap_sdk PUBLIC ${module_includes})
            endif()
        endforeach()
        
        # Collect all external dependencies from modules
        set(ALL_EXTERNAL_DEPS "")
        if(DEFINED DAP_CORE_DEPS)
            list(APPEND ALL_EXTERNAL_DEPS ${DAP_CORE_DEPS})
        endif()
        if(DEFINED DAP_CRYPTO_DEPS)
            list(APPEND ALL_EXTERNAL_DEPS ${DAP_CRYPTO_DEPS})
        endif()
        if(DEFINED DAP_IO_DEPS)
            list(APPEND ALL_EXTERNAL_DEPS ${DAP_IO_DEPS})
        endif()
        if(DEFINED DAP_GLOBAL_DB_DEPS)
            list(APPEND ALL_EXTERNAL_DEPS ${DAP_GLOBAL_DB_DEPS})
        endif()
        if(DEFINED DAP_PLUGIN_DEPS)
            list(APPEND ALL_EXTERNAL_DEPS ${DAP_PLUGIN_DEPS})
        endif()
        if(DEFINED DAP_HTTP_SERVER_DEPS)
            list(APPEND ALL_EXTERNAL_DEPS ${DAP_HTTP_SERVER_DEPS})
        endif()
        if(DEFINED DAP_STREAM_DEPS)
            list(APPEND ALL_EXTERNAL_DEPS ${DAP_STREAM_DEPS})
        endif()
        if(DEFINED DAP_STREAM_CH_DEPS)
            list(APPEND ALL_EXTERNAL_DEPS ${DAP_STREAM_CH_DEPS})
        endif()
        if(DEFINED DAP_CLIENT_DEPS)
            list(APPEND ALL_EXTERNAL_DEPS ${DAP_CLIENT_DEPS})
        endif()
        if(DEFINED DAP_SESSION_DEPS)
            list(APPEND ALL_EXTERNAL_DEPS ${DAP_SESSION_DEPS})
        endif()
        if(DEFINED DAP_ENC_SERVER_DEPS)
            list(APPEND ALL_EXTERNAL_DEPS ${DAP_ENC_SERVER_DEPS})
        endif()
        
        # Remove duplicates and apply external dependencies
        if(ALL_EXTERNAL_DEPS)
            list(REMOVE_DUPLICATES ALL_EXTERNAL_DEPS)
            target_link_libraries(dap_sdk PUBLIC ${ALL_EXTERNAL_DEPS})
            message("[+] Final DAP SDK external dependencies: ${ALL_EXTERNAL_DEPS}")
        endif()
        
        message("[+] Final DAP SDK Library: dap_sdk created from real object files")
    endif()
endmacro()

# =========================================
# LEGACY STATIC LIBRARY HELPER  
# =========================================
# For special cases that need explicit static
macro(dap_add_static_library TARGET_NAME)
    cmake_parse_arguments(DAP_STATIC_LIB "" "" "HEADERS" ${ARGN})
    add_library(${TARGET_NAME} STATIC ${DAP_STATIC_LIB_UNPARSED_ARGUMENTS} ${DAP_STATIC_LIB_HEADERS})
    target_compile_definitions(${TARGET_NAME} INTERFACE DAP_USING_STATIC)
    message("[+] DAP Explicit Static Library: ${TARGET_NAME}")
endmacro()

# Set default values for build information if not provided
if (NOT DEFINED DAP_VERSION)
    set(DAP_VERSION "3.0.0")
endif()

if (NOT DEFINED BUILD_TS)
    string(TIMESTAMP BUILD_TS "%s" UTC)
endif()

if (NOT DEFINED BUILD_HASH)
    # Try to get git hash, fallback to unknown
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE BUILD_HASH
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif()
    if(NOT BUILD_HASH)
        set(BUILD_HASH "unknown")
    endif()
endif()

add_definitions("-DDAP_VERSION=\"${DAP_VERSION}\"")
add_definitions("-DBUILD_TS=\"${BUILD_TS}\"")
add_definitions("-DBUILD_HASH=\"${BUILD_HASH}\"")

##################################################
if (CELLFRAME_MODULES MATCHES "core")
    set(DAPSDK_MODULES "${DAPSDK_MODULES} core crypto io")
endif()

if (CELLFRAME_MODULES MATCHES "chains")
    set(DAPSDK_MODULES "${DAPSDK_MODULES} global-db")
endif()

if (CELLFRAME_MODULES MATCHES "network")
    set(DAPSDK_MODULES "${DAPSDK_MODULES} app-cli plugin network-core network-link_manager network-client network-server")
endif()

if (CELLFRAME_MODULES MATCHES "avrestream")
    set(DAPSDK_MODULES "${DAPSDK_MODULES} avrestream")
endif()

if (CELLFRAME_MODULES MATCHES "dap-sdk-net-client")
    set(DAPSDK_MODULES "core crypto io network-core network-link_manager network-client network-server")
    set(CELLFRAME_LIBS ${CELLFRAME_LIBS} dap_core dap_crypto dap_io dap_server dap_client dap_dns_server m)
    if(NOT ANDROID)
        set(CELLFRAME_LIBS ${CELLFRAME_LIBS} pthread)
    endif()
    if (SUPPORT_PYTHON_PLUGINS)
        set(CELLFRAME_MODULES "${CELLFRAME_MODULES} core chains network cs-none srv-")
    endif()
endif()

# We switch off SSL until it's switched back on
if(DAPSDK_MODULES MATCHES "ssl-support")
    add_definitions("-DDAP_NET_CLIENT_SSL")
else()
    add_definitions("-DDAP_NET_CLIENT_NO_SSL")
endif()

if (BUILD_DAP_SDK_TESTS)
    set(BUILD_DAP_TESTS ON)
    set(BUILD_CRYPTO_TESTS ON)
    set(BUILD_GLOBAL_DB_TEST ON)
    set(BUILD_WITH_GDB_DRIVER_SQLITE ON)
    set(BUILD_WITH_GDB_DRIVER_PGSQL ON)
#    set(BUILD_WITH_GDB_DRIVER_MDBX ON)
    set(BUILD_WITH_ECDSA ON)
    set(DAPSDK_MODULES "app-cli crypto io network-core network-server network-link_manager network-client global-db test-framework plugin")
    set(SPHINCSPLUS_FLEX ON)
    message("[+] Enable dap_sdk_tests")
    add_definitions("-DDAP_SDK_TESTS")
endif ()

if (DAPSDK_MODULES MATCHES "test-framework")
    enable_testing()
    add_subdirectory(test-framework)
    message("[+] Testing framework")
endif()

# Set default modules if none are specified
if (NOT DEFINED DAPSDK_MODULES OR DAPSDK_MODULES STREQUAL "")
    set(DAPSDK_MODULES "crypto plugin io network-core network-server network-client network-link_manager global-db")
    message("[+] Using default modules: ${DAPSDK_MODULES}")
endif()

# Core, add it always
add_subdirectory(core)

if (DAPSDK_MODULES MATCHES "crypto")
    # Cryptography
    add_subdirectory(crypto)
    message("[+] Module 'crypto'")
endif()

if (DAPSDK_MODULES MATCHES "app-cli")
    add_subdirectory(net/app-cli)
    message("[+] Module 'app-cli'")
endif()

if (DAPSDK_MODULES MATCHES "plugin")
    add_subdirectory(plugin)
    message("[+] Module 'plugin'")
endif()

# I/O subsystem
if (DAPSDK_MODULES MATCHES "io" OR DAPSDK_MODULES MATCHES "network-pure")
    add_subdirectory(io)
    message("[+] Module 'io'")
endif()

# Networking core
if (DAPSDK_MODULES MATCHES "network-core")
    add_subdirectory(net/stream)
    message("[+] Module 'stream'")
endif()

# Networking link manager
if (DAPSDK_MODULES MATCHES "network-link_manager")
    add_subdirectory(net/link_manager)
    message("[+] Module 'link manager'")
endif()

# Networking client
if (DAPSDK_MODULES MATCHES "network-client")
    add_subdirectory(net/client)
    message("[+] Module 'client'")
endif()

# Networking server
if (DAPSDK_MODULES MATCHES "network-server")
    add_subdirectory(net/server)
    message("[+] Module 'server'")
endif()

if(DAPSDK_MODULES MATCHES "ssl-support")
    add_subdirectory(3rdparty/wolfssl)
    message("[+] SSL supporting")
endif()

# Global database
if (DAPSDK_MODULES MATCHES "global-db")
    add_subdirectory(global-db)
    message("[+] Module 'global-db'")
endif()

if (DAPSDK_MODULES MATCHES "avrestream")
    add_subdirectory(avrestream)
    message("[+] Module 'av-restream'")
endif()

if(ANDROID)
    include_directories(3rdparty/)
endif()


# Option to enable full cppcheck analysis
option(ENABLE_CPPCHECK_ANALYSIS "Enable full cppcheck static analysis" OFF)

# Cpp check
if(ENABLE_CPPCHECK_ANALYSIS)
    find_program(CPPCHECK cppcheck)
    if(CPPCHECK)
        message(STATUS "Found cppcheck: ${CPPCHECK}")
        add_custom_target(cppcheck_analysis
            COMMAND ${CPPCHECK}
                --std=c11
                --enable=warning,style,performance,portability
                --quiet
                --xml
                --force
                --suppress=missingIncludeSystem
                --suppress=*:3rdparty/*
                --suppress=*:_deps/*
                ${CMAKE_SOURCE_DIR}/avrestream
                ${CMAKE_SOURCE_DIR}/core
                ${CMAKE_SOURCE_DIR}/crypto
                ${CMAKE_SOURCE_DIR}/examples
                ${CMAKE_SOURCE_DIR}/global-db
                ${CMAKE_SOURCE_DIR}/io
                ${CMAKE_SOURCE_DIR}/net
                ${CMAKE_SOURCE_DIR}/plugin
                ${CMAKE_SOURCE_DIR}/test-framework
                2> ${CMAKE_BINARY_DIR}/cppcheck_report.xml
            COMMAND cppcheck-htmlreport
                --file ${CMAKE_BINARY_DIR}/cppcheck_report.xml
                --report-dir ${CMAKE_BINARY_DIR}/cppcheck_html_report
                --source-dir ${CMAKE_SOURCE_DIR}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Running full cppcheck static analysis and generating HTML report in ${CMAKE_BINARY_DIR}/cppcheck_html_report"
        )
    else()
        message(WARNING "cppcheck not found, cppcheck_analysis target unavailable")
    endif()
endif()

find_program(CPPCHECK cppcheck)
if(CPPCHECK)
    message(STATUS "Found cppcheck: ${CPPCHECK}")
    add_custom_target(lint
        COMMAND ${CPPCHECK} --enable=warning,style --std=c11 ${CMAKE_SOURCE_DIR}/sources
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running cppcheck on source files"
    )
else()
    message(WARNING "cppcheck not found, lint target unavailable")
endif()

# =========================================
# CREATE FINAL DAP SDK LIBRARY
# =========================================
# Combine all internal static modules into one final library
dap_create_final_library()
