cmake_minimum_required(VERSION 3.10)
project(DAP_SDK_NATIVE C)

# Отключение SSL по умолчанию - WolfSSL больше не используется
add_definitions(-DDAP_NET_CLIENT_NO_SSL)

set(DAP_SDK_NATIVE_VERSION "3.0-0")

# Build type detection
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Common includes and definitions
include(cmake/OS_Detection.cmake)
include(cmake/PackageDetection.cmake)
include(cmake/PackageComponents.cmake)


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
# INCLUDE DAP LIBRARY HELPERS
# =========================================
include(cmake/DapLibrary.cmake)

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

# Network common types and functions
add_subdirectory(module/net/common)
message("[+] Module 'net/common' (base types,callbacks and functions)")

# HTTP client for outgoing connections (independent of dap_client)
add_subdirectory(module/net/client_http)
message("[+] Module 'client_http' (standalone HTTP client)")

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

# App CLI module
add_subdirectory(module/net/app-cli)
message("[+] Module 'app-cli'")

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

# =========================================
# PACKAGE COMPONENTS SETUP
# =========================================
# Setup component-based packaging (runtime/dev/doc split)
setup_package_components(
    PACKAGE_BASE_NAME "dap-sdk"
    VERSION "${DAP_SDK_NATIVE_VERSION}"
    DESCRIPTION "DAP SDK - Distributed Application Platform Software Development Kit"
    VENDOR "Demlabs"
    CONTACT "support@demlabs.net"
    DEBUG_SUFFIX "${DAP_DEBUG_SUFFIX}"
    OS_SUFFIX "${DAP_PACKAGE_OS_SUFFIX}"
)

# Register 3rd party headers required by public API
register_3rdparty_headers(
    DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/uthash/src"
    DESTINATION "uthash"
)

register_3rdparty_headers(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/http_status_code.h"
)

# Note: Module headers are auto-registered by dap_add_library() in each module's CMakeLists.txt

# Finalize and install all components
finalize_package_components(
    LIBRARY_TARGET "dap-sdk"
    INCLUDE_PREFIX "dap-sdk"
    PKGCONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/dap-sdk.pc.in"
    README_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    EXAMPLES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/module/examples"
)

message(STATUS "[Install] Component-based installation configured")
message(STATUS "[Install] - Runtime: libdap-sdk.so → /usr/lib")
message(STATUS "[Install] - Development: headers → /usr/include/dap-sdk")
message(STATUS "[Install] - Documentation: examples → /usr/share/doc/dap-sdk")

# =========================================
# CPACK CONFIGURATION (handled by PackageComponents.cmake)
# =========================================
# All CPack configuration is now handled by the universal packaging system
# in cmake/PackageComponents.cmake which was included earlier

# Set final package name explicitly before CPack generation
set(PKG_NAME "${CPACK_PKG_BASE_NAME}${CPACK_PKG_DEBUG_SUFFIX}")
set(OS_SUFFIX "${CPACK_PKG_OS_SUFFIX}")
set(CPACK_PACKAGE_FILE_NAME "${PKG_NAME}_${OS_SUFFIX}")

# Enable component-based packaging
set(CPACK_COMPONENT_INSTALL ON)

message(STATUS "[CPack] Package file name: ${CPACK_PACKAGE_FILE_NAME}")
message(STATUS "[CPack] Component-based packaging enabled")
message(STATUS "[CPack] Package generation configured")

include(CPack)
