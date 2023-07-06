set(DAP_SDK_NATIVE_VERSION "2.3-0")

include(ExternalProject)

add_subdirectory(3rdparty/json-c EXCLUDE_FROM_ALL)

set(JSON-C-CONFIG_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json-c/
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/deps/
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME})

##TODO: customize for universal platforms
if ("${CMAKE_HOST_SYSTEM}" MATCHES ".*Windows.*")
    if (ANDROID)
        set(JSON-C-CONFIG_COMMAND ${CMAKE_COMMAND} ${JSON-C-CONFIG_COMMAND}
            -DANDROID_NATIVE_API_LEVEL=29
            -DANDROID_ABI=arm64-v8a
            -DANDROID_PLATFORM=${ANDROID_PLATFORM}
            -DCMAKE_TOOLCHAIN_FILE=D:/android-sdk/ndk/21.4.7075529/build/cmake/android.toolchain.cmake
            -DCMAKE_MAKE_PROGRAM=D:/android-sdk/cmake/3.22.1/bin/ninja.exe
            -G=Ninja)
    endif()
elseif(ANDROID)
    set(JSON-C-CONFIG_COMMAND ${CMAKE_COMMAND} -E env PATH=$ENV{PATH}
        ${CMAKE_COMMAND} ${JSON-C-CONFIG_COMMAND}
        -DANDROID_NATIVE_API_LEVEL=${ANDROID_NATIVE_API_LEVEL}
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=${ANDROID_PLATFORM}
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
else()
    set(JSON-C-CONFIG_COMMAND ${CMAKE_COMMAND} -E env PATH=$ENV{PATH} ${CMAKE_COMMAND} ${JSON-C-CONFIG_COMMAND})
endif()

message("json-c build cmd ${JSON-C-CONFIG_COMMAND}")

ExternalProject_Add(dap_json-c_dep
                    SOURCE_DIR ${PROJECT_SOURCE_DIR}/dap-sdk/3rdparty/json-c/
                    CONFIGURE_COMMAND ${JSON-C-CONFIG_COMMAND})

if (CELLFRAME_MODULES MATCHES "core")
    SET(DAPSDK_MODULES "${DAPSDK_MODULES} core crypto io")
endif()

if (CELLFRAME_MODULES MATCHES "chains")
    SET(DAPSDK_MODULES "${DAPSDK_MODULES} global-db")
endif()

if (CELLFRAME_MODULES MATCHES "network")
    set(DAPSDK_MODULES "${DAPSDK_MODULES} app-cli plugin network-core network-client network-server")
endif()

if (CELLFRAME_MODULES MATCHES "dap-sdk-net-client")
    set(DAPSDK_MODULES "core crypto network-core network-client network-server")
    set(CELLFRAME_LIBS ${CELLFRAME_LIBS} dap_core dap_crypto dap_io dap_server dap_client m pthread)
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

# Core, add it always
add_subdirectory(core)
if (DAPSDK_MODULES MATCHES "crypto")
    # Cryptography
    add_subdirectory(crypto)
endif()

if (DAPSDK_MODULES MATCHES "app-cli")
    add_subdirectory(net/app-cli)
endif()

if (DAPSDK_MODULES MATCHES "plugin")
    add_subdirectory(plugin)
endif()

# I/O subsystem
if (DAPSDK_MODULES MATCHES "io")
    add_subdirectory(io)
endif()

# Global database
if (DAPSDK_MODULES MATCHES "global-db")
    add_subdirectory(global-db)
endif()

# Networking core
if (DAPSDK_MODULES MATCHES "network-core")
    add_subdirectory(net/stream)
endif()

if (DAPSDK_MODULES MATCHES "network-pure")
    add_subdirectory(io)
    add_subdirectory(net/core)
endif()

# Networking client
if (DAPSDK_MODULES MATCHES "network-client")
    add_subdirectory(net/client)
endif()

# Networking server
if (DAPSDK_MODULES MATCHES "network-server")
    add_subdirectory(net/server)
#    add_subdirectory(net/server-udp)
endif()

if(DARWIN)
    add_subdirectory(3rdparty/libmagic-darwin)
elseif(ANDROID)
    include_directories(3rdparty/)
endif()

#if(BUILD_DAP_TESTS)
#    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/main_test.py
#            DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/)
#    enable_testing()
#    add_subdirectory(test)
#endif()

add_subdirectory(examples)
