cmake_minimum_required(VERSION 3.10)
project(DAP_SDK_NATIVE C)
set(DAP_SDK_NATIVE_VERSION "2.4-0")

include(CheckFunctionExists)
include(cmake/OS_Detection.cmake)
# Include LibraryHelpers for object library creation
# This must be included early to override target_link_libraries
include(cmake/LibraryHelpers.cmake)

# Check for secure memory clearing functions
include(CheckSymbolExists)

# explicit_bzero(): first try in libc, then in libbsd
check_function_exists(explicit_bzero HAVE_EXPLICIT_BZERO)
if(NOT HAVE_EXPLICIT_BZERO)
    set(CMAKE_REQUIRED_LIBRARIES bsd)
    check_function_exists(explicit_bzero HAVE_EXPLICIT_BZERO_IN_BSD)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(HAVE_EXPLICIT_BZERO_IN_BSD)
        set(HAVE_EXPLICIT_BZERO TRUE)
        set(USE_LIBBSD_EXPLICIT_BZERO TRUE)
    endif()
endif()
if(HAVE_EXPLICIT_BZERO)
    add_definitions(-DHAVE_EXPLICIT_BZERO)
    if(USE_LIBBSD_EXPLICIT_BZERO)
        add_definitions(-DUSE_LIBBSD_EXPLICIT_BZERO)
    endif()
endif()

# C11 Annex K memset_s: request declarations and check symbol presence
set(CMAKE_REQUIRED_DEFINITIONS -D__STDC_WANT_LIB_EXT1__=1)
check_symbol_exists(memset_s "string.h" HAVE_MEMSET_S)
unset(CMAKE_REQUIRED_DEFINITIONS)
if(HAVE_MEMSET_S)
    add_definitions(-DHAVE_MEMSET_S)
    add_definitions(-D__STDC_WANT_LIB_EXT1__=1)
endif()


# Link libbsd when explicit_bzero is provided by it
if(USE_LIBBSD_EXPLICIT_BZERO)
    set(CELLFRAME_LIBS ${CELLFRAME_LIBS} bsd)
endif()

if (NOT DEFINED DAP_VERSION)
    set(DAP_VERSION "0.0-1")
endif()

if (NOT DEFINED BUILD_TS)
    set(BUILD_TS "0")
endif()

if (NOT DEFINED BUILD_HASH)
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
    set(BUILD_WITH_GDB_DRIVER_SQLITE ON)
    set(BUILD_WITH_GDB_DRIVER_PGSQL ON)
    set(BUILD_WITH_GDB_DRIVER_MDBX ON)
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

# No STATIC library needed - all modules are OBJECT libraries now

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
    add_subdirectory(net/trans)
    message("[+] Module 'stream'")
    message("[+] Module 'transport'")
endif()

# Networking link manager
if (DAPSDK_MODULES MATCHES "network-link_manager")
    add_subdirectory(net/link_manager)
    message("[+] Module 'link manager'")
endif()

# Common HTTP functionality for client and server
if (DAPSDK_MODULES MATCHES "network-client" OR DAPSDK_MODULES MATCHES "network-server")
    add_subdirectory(net/common/server)
    message("[+] Module 'common server'")
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

# DAP SDK main library - create as OBJECT library from all modules
# This allows --wrap to work for internal calls within the SDK
# Create this AFTER all modules are added to ensure DAP_INTERNAL_MODULES is populated

    # Post-process all OBJECT libraries to propagate include directories
    # This must be done AFTER all modules are created and configured
    # This ensures transitive dependencies (like XKCP via dap_crypto) are propagated
    post_process_object_libraries()
    
if(BUILD_DAP_SDK_TESTS OR BUILD_DAP_TESTS)
    # Create single OBJECT library from all DAP SDK modules for tests
    # This enables --wrap to work for internal calls within the SDK
    create_combined_object_library(
        TARGET_NAME dap_sdk_object
        MODULE_LIST_VAR DAP_INTERNAL_MODULES
    )
    
    # Also create STATIC libraries for modules that need mocking support
    # IMPORTANT: --wrap only works with STATIC libraries, NOT with OBJECT files
    # Create static libraries for modules frequently used in mocking
    if(DAP_INTERNAL_MODULES)
        foreach(MODULE ${DAP_INTERNAL_MODULES})
            if(TARGET ${MODULE})
                # Create static library from object library
                add_library(${MODULE}_static STATIC $<TARGET_OBJECTS:${MODULE}>)
                
                # Copy all properties from object library
                get_target_property(MODULE_INCLUDES ${MODULE} INTERFACE_INCLUDE_DIRECTORIES)
                if(MODULE_INCLUDES)
                    target_include_directories(${MODULE}_static INTERFACE ${MODULE_INCLUDES})
                endif()
                
                # Copy link libraries (dependencies)
                get_target_property(MODULE_LINK_LIBS ${MODULE} INTERFACE_LINK_LIBRARIES)
                if(MODULE_LINK_LIBS)
                    # Replace object library references with static library references
                    set(STATIC_LINK_LIBS "")
                    foreach(DEP ${MODULE_LINK_LIBS})
                        # Check if dependency is an object library that will have static version
                        list(FIND DAP_INTERNAL_MODULES ${DEP} DEP_IN_MODULES)
                        if(DEP_IN_MODULES GREATER -1)
                            # Use static version of dependency
                            list(APPEND STATIC_LINK_LIBS ${DEP}_static)
                        else()
                            # Keep non-module dependencies as is
                            list(APPEND STATIC_LINK_LIBS ${DEP})
                        endif()
                    endforeach()
                    target_link_libraries(${MODULE}_static INTERFACE ${STATIC_LINK_LIBS})
                endif()
            endif()
        endforeach()
    endif()
    
    # Execute deferred include propagation for all OBJECT libraries
    # This ensures all dependencies are configured before propagation
    file(GLOB PROPAGATE_FILES "${CMAKE_BINARY_DIR}/**/propagate_*.cmake")
    foreach(PROPAGATE_FILE ${PROPAGATE_FILES})
        include(${PROPAGATE_FILE})
    endforeach()
    
    # Add DAP SDK Tests AFTER dap_sdk_object is created
    # This ensures tests can use the combined object library for proper --wrap support
    if (BUILD_DAP_TESTS)
        add_subdirectory(tests)
        message("[+] DAP SDK tests")
    endif()
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
