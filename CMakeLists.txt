cmake_minimum_required(VERSION 3.12)
project(DAP_SDK_NATIVE C)

# Отключение SSL по умолчанию - WolfSSL больше не используется
add_definitions(-DDAP_NET_CLIENT_NO_SSL)

set(DAP_SDK_NATIVE_VERSION "3.0-0")

# Define SDK root directory for use in submodules
# Works both when dap-sdk is standalone and when it's a submodule
set(DAP_SDK_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Root directory of DAP SDK")

# Build type detection
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Common includes and definitions
include(cmake/OS_Detection.cmake)
include(cmake/PackageDetection.cmake)
include(cmake/PackageComponents.cmake)

# =========================================
# INCLUDE DAP LIBRARY HELPERS
# =========================================
# Object library helpers - automatic include propagation for OBJECT libraries (CMake 3.12+)
include(cmake/ObjectLibraryHelpers.cmake)

# DAP SDK library creation macros
include(cmake/DapLibrary.cmake)

# Options
option(BUILD_SHARED "Build final DAP SDK library as shared (default)" ON)
option(BUILD_EXAMPLES "Build examples" OFF) # we don't need them by default
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_DAP_SDK_TESTS "Build with DAP SDK tests" OFF)

option(INSTALL_DAP_SDK "Install DAP SDK library and headers (disable when used as subproject)" ON)

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

# Add DAP SDK unified interface using dap_add_library
# This ensures dap_sdk_interface is properly registered in DAP_INTERNAL_MODULES
dap_add_library(dap_sdk_interface dap_sdk.c)
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

# HTTP client
add_subdirectory(module/net/client_http)

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

if (BUILD_DAP_SDK_TESTS)
    set(BUILD_TESTS ON)
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
# Only setup installation when INSTALL_DAP_SDK is enabled
# Can be disabled by parent project (e.g., cellframe-node) by setting INSTALL_DAP_SDK to OFF
if(INSTALL_DAP_SDK)
    message(STATUS "[DAP-SDK] Installation enabled")
    
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
else()
    message(STATUS "[DAP-SDK] Installation disabled (build-time dependency only)")
endif()
