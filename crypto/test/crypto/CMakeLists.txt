cmake_minimum_required(VERSION 3.10)

project(crypto-test)
# init CellFrame SDK
set(SUBMODULES_NO_BUILD ON)
set(DAPSDK_MODULES "core crypto network-core network-client network-server")

if ( NOT ( TARGET dap_core ) )
    #add_subdirectory(libdap)
endif()

if ( NOT ( TARGET dap_test ) )
    #add_subdirectory(libdap-test)
endif()

file(GLOB SRC *.h *.c)

add_executable(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME} dap_test dap_core dap_crypto m)

add_test(
    NAME crypto-test
    COMMAND crypto-test
)
