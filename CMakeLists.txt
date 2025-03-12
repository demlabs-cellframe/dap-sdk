cmake_minimum_required(VERSION 3.10)
project(DAP_SDK_NATIVE C)
set(DAP_SDK_NATIVE_VERSION "2.4-0")

include(ExternalProject)

add_subdirectory(3rdparty/json-c EXCLUDE_FROM_ALL)

set(JSON-C-CONFIG_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json-c/
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/deps/
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
    -DCMAKE_GENERATOR=${CMAKE_GENERATOR})

##TODO: customize for universal platforms
if ("${CMAKE_HOST_SYSTEM}" MATCHES ".*Windows.*")
    if (ANDROID)
        set(JSON-C-CONFIG_COMMAND ${CMAKE_COMMAND} ${JSON-C-CONFIG_COMMAND}
            -DANDROID_NATIVE_API_LEVEL=29
            -DANDROID_ABI=arm64-v8a
            -DANDROID_PLATFORM=${ANDROID_PLATFORM}
            -DCMAKE_TOOLCHAIN_FILE=D:/android-sdk/ndk/21.4.7075529/build/cmake/android.toolchain.cmake
            -G=Ninja)
    else()
        set(JSON-C-CONFIG_COMMAND ${CMAKE_COMMAND} ${JSON-C-CONFIG_COMMAND})
    endif()
elseif(ANDROID)
    set(JSON-C-CONFIG_COMMAND ${CMAKE_COMMAND} -E env PATH=$ENV{PATH}
        ${CMAKE_COMMAND} ${JSON-C-CONFIG_COMMAND}
        -DANDROID_NATIVE_API_LEVEL=${ANDROID_NATIVE_API_LEVEL}
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=${ANDROID_PLATFORM}
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
else()
    set(JSON-C-CONFIG_COMMAND ${CMAKE_COMMAND} ${JSON-C-CONFIG_COMMAND})
endif()

message("json-c build cmd ${JSON-C-CONFIG_COMMAND}")

ExternalProject_Add(dap_json-c_dep
                    SOURCE_DIR ${PROJECT_SOURCE_DIR}/3rdparty/json-c/
                    CONFIGURE_COMMAND ${JSON-C-CONFIG_COMMAND})

include(cmake/OS_Detection.cmake)
if (INSTALL_DAP_SDK)
    INSTALL(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/deps/ DESTINATION ./)
endif()

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

# We switch off SSL until its not switched on back
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
    set(BUILD_WITH_GDB_DRIVER_MDBX ON)
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

