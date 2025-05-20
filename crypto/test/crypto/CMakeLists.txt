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
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework ${CMAKE_CURRENT_BINARY_DIR}/dap_test)
endif()

# Common source files for tests
file(GLOB COMMON_SRC dap_enc_*.c main.c)

# Основной таргет для тестирования всего модуля crypto
add_executable(${PROJECT_NAME} ${COMMON_SRC})

# Отдельный таргет для тестирования только модуля chipmunk
add_executable(chipmunk-test chipmunk_test_main.c dap_enc_chipmunk_test.c dap_enc_chipmunk_test.h)

target_link_libraries(${PROJECT_NAME} dap_test dap_core dap_crypto m)
target_link_libraries(chipmunk-test dap_test dap_core dap_crypto m)

# Добавляем путь к dap_test.h
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework)
target_include_directories(chipmunk-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework)

add_test(
    NAME crypto-test
    COMMAND crypto-test
)

add_test(
    NAME chipmunk-test
    COMMAND chipmunk-test
)
