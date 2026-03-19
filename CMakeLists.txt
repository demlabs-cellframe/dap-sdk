cmake_minimum_required(VERSION 3.12)
project(DAP_SDK_NATIVE C CXX)

# Disable SSL by default - WolfSSL is no longer used
add_definitions(-DDAP_NET_CLIENT_NO_SSL)

set(DAP_SDK_NATIVE_VERSION "3.0-0")

# Define SDK root directory for use in submodules
# Works both when dap-sdk is standalone and when it's a submodule
set(DAP_SDK_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Root directory of DAP SDK")

# Build type detection
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Installation prefix detection
# Default: /usr/local (for local builds)
# For deb packages: override to /usr via cmake -DCMAKE_INSTALL_PREFIX=/usr
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation prefix" FORCE)
    message(STATUS "[Install] Using default prefix: ${CMAKE_INSTALL_PREFIX}")
else()
    message(STATUS "[Install] Using custom prefix: ${CMAKE_INSTALL_PREFIX}")
endif()

# Build type (Debug / Release / Asan) and DAP_* build variables
include(cmake/DapBuild.cmake)
# Compiler sanitizers (ASan, UBSan, TSan, MSan)
include(cmake/Sanitizers.cmake)
# Common includes and definitions (uses DAP_DEBUG, DAP_RELEASE, DAP_ASAN_BUILD from DapBuild)
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

# No global include_directories() — all includes propagate via dap_link_libraries

# Options
option(BUILD_SHARED "Build final DAP SDK library as shared (default)" ON)
option(BUILD_STATIC "Also build static version of DAP SDK library" OFF)
option(BUILD_DAP_SDK_TESTS "Build with DAP SDK tests" OFF)

option(INSTALL_DAP_SDK "Install DAP SDK library and headers (disable when used as subproject)" ON)

# Internal modules are ALWAYS static, final can be SHARED or STATIC
if(BUILD_SHARED)
    message("[+] DAP SDK: Building final library as SHARED (modules: static internally)")
    set(DAP_FINAL_LIB_TYPE SHARED)
else()
    message("[+] DAP SDK: Building final library as STATIC (modules: static internally)")
    set(DAP_FINAL_LIB_TYPE STATIC)
endif()

# Apply ENABLE_TESTING define globally BEFORE adding subdirectories
# This ensures all modules can use test-specific code when BUILD_DAP_SDK_TESTS is ON
if(BUILD_DAP_SDK_TESTS)
    add_definitions(-DENABLE_TESTING)
    message("[+] ENABLE_TESTING define enabled globally for dap-sdk")
endif()

# Set default modules if none are specified
if (NOT DEFINED DAPSDK_MODULES OR DAPSDK_MODULES STREQUAL "")
    set(DAPSDK_MODULES "crypto plugin io network-core network-server network-client network-link_manager global-db")
    message("[+] Using default modules: ${DAPSDK_MODULES}")
endif()

# Core is always required - UPDATED PATH
add_subdirectory(module/core)

# Optimization module - arch dispatch macros (header-only, depends on core)
add_subdirectory(module/optimization)
message("[+] Module 'optimization'")

# Set DAP_TPL_DIR for dap_json SIMD code generation (if not already set by parent)
if(NOT DEFINED DAP_TPL_DIR)
    set(DAP_TPL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/module/test/dap_tpl" CACHE PATH "Path to dap_tpl template engine")
    message("[+] DAP SDK: DAP_TPL_DIR set to ${DAP_TPL_DIR}")
endif()

# Arch-optimized code generation infrastructure (used by hash, json, crypto, math modules)
if(NOT DEFINED DAP_ARCH_DIR)
    set(DAP_ARCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arch" CACHE PATH "Path to arch codegen infrastructure")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DapTplGenerate.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DapArchCodegen.cmake)

# Math module - uint128_t/uint256_t operations (must be before json)
add_subdirectory(module/math)
message("[+] Module 'math'")

# Hash module - hash functions (SHA3, SHA2, Keccak, etc.) and hash tables
add_subdirectory(module/hash)
message("[+] Module 'hash'")

# JSON module (depends on math, core)
add_subdirectory(module/json)
message("[+] Module 'json'")

# Timer module - interval timers
add_subdirectory(module/timer)
message("[+] Module 'timer'")

# Daemon module - daemonization, signals, PID files (not available on WASM)
if(NOT DAP_WASM)
    add_subdirectory(module/daemon)
    message("[+] Module 'daemon'")
endif()

# Config module - configuration management
add_subdirectory(module/config)
message("[+] Module 'config'")

# Bignum library — independent of dap_math, used only by crypto algorithms
if (DAPSDK_MODULES MATCHES "crypto")
    add_subdirectory(module/math/bignum)
    message("[+] Module 'math/bignum' (NTT, poly, multi-precision)")
endif()

# Optional modules with UPDATED PATHS
if (DAPSDK_MODULES MATCHES "crypto")
    add_subdirectory(module/crypto)
    message("[+] Module 'crypto'")
endif()

if (DAPSDK_MODULES MATCHES "plugin" AND NOT DAP_WASM)
    add_subdirectory(module/plugin)
    message("[+] Module 'plugin'")
endif()

if(DAPSDK_MODULES MATCHES "io" OR DAP_WASM)
    add_subdirectory(module/io)
    message("[+] Module 'io'")
    add_subdirectory(module/io/flow)
endif()

# Cluster node types — must be before net/common (net_common depends on cluster_node types)
add_subdirectory(module/cluster)
message("[+] Module 'cluster_node'")

# Network common types and functions (needed by all platforms including WASM)
add_subdirectory(module/net/common)

# Add DAP SDK unified interface — link dependencies are added later,
# after all module targets have been defined (see _DAP_SDK_LATE_LINK below)
dap_add_library(dap_sdk_interface dap_sdk.c)
target_include_directories(dap_sdk_interface PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Server module — must be before trans (trans depends on dap_http_server includes)
if (DAPSDK_MODULES MATCHES "network-server")
    add_subdirectory(module/net/server)
    message("[+] Module 'server'")
endif()

if (DAPSDK_MODULES MATCHES "network-client")
    add_subdirectory(module/net/client)
    message("[+] Module 'client'")
endif()

if (DAPSDK_MODULES MATCHES "network-link_manager")
    add_subdirectory(module/net/link_manager)
    message("[+] Module 'link manager'")
endif()

# Transport abstraction layer (after server, client, link_manager — depends on their includes)
add_subdirectory(module/net/trans)
message("[+] Module 'trans' (transport abstraction)")

if (DAPSDK_MODULES MATCHES "network-core")
    add_subdirectory(module/net/stream)
    message("[+] Module 'stream'")
endif()

# Full cluster module — depends on stream, must be after stream
create_object_library(dap_cluster DAP_INTERNAL_MODULES
    ${CMAKE_CURRENT_SOURCE_DIR}/module/cluster/dap_cluster.c
    HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/module/cluster/include/dap_cluster.h)
dap_link_libraries(dap_cluster INTERFACE
    dap_cluster_node dap_hash_table dap_json
    dap_stream)
target_include_directories(dap_cluster PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/module/cluster/include)
message("[+] Module 'cluster'")

# App CLI module
add_subdirectory(module/net/app-cli)
message("[+] Module 'app-cli'")

if (DAPSDK_MODULES MATCHES "global-db")
    add_subdirectory(module/global-db)
    if(DAP_WASM)
        message("[+] Module 'global-db' (WASM: local storage only)")
    else()
        message("[+] Module 'global-db' (native B-tree engine)")
    endif()
endif()

# Test framework - UPDATED PATH (not available on WASM)
if ((DAPSDK_MODULES MATCHES "test-framework" OR BUILD_DAP_SDK_TESTS) AND NOT DAP_WASM)
    enable_testing()
    add_subdirectory(module/test)
    message("[+] Testing framework")
endif()

# Create static libraries for testing (needed for --wrap mocking)
if(BUILD_DAP_SDK_TESTS)
    message("[+] Creating static libraries for testing...")
    foreach(MODULE ${DAP_INTERNAL_MODULES})
        if(TARGET ${MODULE} AND NOT TARGET ${MODULE}_static)
            add_library(${MODULE}_static STATIC $<TARGET_OBJECTS:${MODULE}>)
            # Inherit all properties from OBJECT library
            get_target_property(MODULE_INCLUDES ${MODULE} INTERFACE_INCLUDE_DIRECTORIES)
            if(MODULE_INCLUDES)
                target_include_directories(${MODULE}_static PUBLIC ${MODULE_INCLUDES})
            endif()
            # OBJECT libraries use INTERFACE_LINK_LIBRARIES (via dap_link_libraries),
            # not LINK_LIBRARIES. Check both to propagate transitive deps correctly.
            get_target_property(MODULE_IFACE_LINK_LIBS ${MODULE} INTERFACE_LINK_LIBRARIES)
            get_target_property(MODULE_LINK_LIBS ${MODULE} LINK_LIBRARIES)
            if(MODULE_IFACE_LINK_LIBS)
                target_link_libraries(${MODULE}_static PUBLIC ${MODULE_IFACE_LINK_LIBS})
            endif()
            if(MODULE_LINK_LIBS)
                target_link_libraries(${MODULE}_static PUBLIC ${MODULE_LINK_LIBS})
            endif()
            message("[+]   ${MODULE}_static")
        endif()
    endforeach()
endif()

# New tests structure - UPDATED PATHS
if(BUILD_DAP_SDK_TESTS)
    add_subdirectory(tests)
    message("[+] Tests enabled with new structure")
endif()

# _DAP_SDK_LATE_LINK: connect dap_sdk_interface to every module it calls into.
# Must happen AFTER all add_subdirectory() / create_object_library() calls.
set(_DAP_SDK_IFACE_DEPS dap_core dap_json dap_math dap_hash dap_hash_table
                        dap_timer dap_config dap_io dap_net_common dap_cluster_node)
if(NOT DAP_WASM)
    list(APPEND _DAP_SDK_IFACE_DEPS dap_daemon)
endif()
foreach(_mod dap_crypto dap_stream dap_client dap_cluster dap_global_db
             dap_http_server dap_enc_server dap_notify_srv dap_cli_server
             dap_dns_server dap_link_manager dap_plugin)
    if(TARGET ${_mod})
        list(APPEND _DAP_SDK_IFACE_DEPS ${_mod})
    endif()
endforeach()
dap_link_libraries(dap_sdk_interface INTERFACE ${_DAP_SDK_IFACE_DEPS})

# Post-process: propagate include directories for ALL OBJECT libraries
# Must be called AFTER all targets are created (resolves forward references)
post_process_object_libraries()

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


    # Note: Module headers are auto-registered by dap_add_library() in each module's CMakeLists.txt

    # Finalize and install all components
    finalize_package_components(
        LIBRARY_TARGET "dap-sdk"
        INCLUDE_PREFIX "dap-sdk"
        PKGCONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/dap-sdk.pc.in"
        README_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    )

    message(STATUS "[Install] Component-based installation configured")
    message(STATUS "[Install] - Runtime: libdap-sdk.so → ${CMAKE_INSTALL_PREFIX}/lib")
    message(STATUS "[Install] - Development: headers → ${CMAKE_INSTALL_PREFIX}/include/dap-sdk")

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
