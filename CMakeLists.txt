cmake_minimum_required(VERSION 3.10)
project(DAP_SDK_NATIVE C)
set(DAP_SDK_NATIVE_VERSION "2.4-0")

include(CheckFunctionExists)
include(cmake/OS_Detection.cmake)

# Check for explicit_bzero availability
check_function_exists(explicit_bzero HAVE_EXPLICIT_BZERO)
if(HAVE_EXPLICIT_BZERO)
    add_definitions(-DHAVE_EXPLICIT_BZERO)
endif()

if (NOT DEFINED DAP_VERSION)
    message(WARNING "DAP_VERSION is not defined")
    set(DAP_VERSION "0.0.0")
endif()

if (NOT DEFINED BUILD_TS)
    message(WARNING "BUILD_TS is not defined")
    set(BUILD_TS "0")
endif()

if (NOT DEFINED BUILD_HASH)
    message(WARNING "BUILD_HASH is not defined")
    set(BUILD_HASH "0")
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
    set(CELLFRAME_LIBS ${CELLFRAME_LIBS} dap_core dap_crypto dap_io dap_server dap_client m)
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
#   set(BUILD_WITH_GDB_DRIVER_MDBX ON)
    set(DAPSDK_MODULES "crypto io network-core network-server network-link_manager network-client global-db test-framework")
#    set(DAPSDK_MODULES ${DAPSDK_MODULES} global-db)
    set(SPHINCSPLUS_FLEX ON)
    message("[+] Enable dap_sdk_tests")
    add_definitions("-DDAP_SDK_TESTS")
endif ()

if (DAPSDK_MODULES MATCHES "test-framework")
    enable_testing()
    add_subdirectory(test-framework)
    message("[+] Testing framework")
endif()

# Core, add it always
add_subdirectory(core)

# DAP SDK main library (moved from core to root)
if (DAPSDK_MODULES MATCHES "core")
    add_library(dap_sdk STATIC dap_sdk.c)
    target_include_directories(dap_sdk PRIVATE 
        core/include
        io/include  
        crypto/include
        net/stream/stream/include
        net/stream/session/include
        net/stream/ch/include
        net/client/include
        net/server/http_server/include
        global-db/include
        plugin/include
    )
    target_link_libraries(dap_sdk dap_core)
    message("[+] DAP SDK main library")
endif()

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

# Common HTTP functionality for client and server
if (DAPSDK_MODULES MATCHES "network-client" OR DAPSDK_MODULES MATCHES "network-server")
    add_subdirectory(net/common/http)
    message("[+] Module 'common http'")
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
