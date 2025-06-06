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
file(GLOB COMMON_SRC dap_enc_*.c dap_sign_test.c main.c)

# Основной таргет для тестирования всего модуля crypto
add_executable(${PROJECT_NAME} ${COMMON_SRC})

# Отдельный таргет для тестирования только модуля chipmunk
add_executable(chipmunk-test chipmunk_test_main.c dap_enc_chipmunk_test.c dap_enc_chipmunk_test.h)

# Отдельный таргет для тестирования только HOTS модуля
add_executable(chipmunk_hots_test chipmunk_hots_test.c)

# Add debug test for HOTS mathematical verification
add_executable(chipmunk_ntt_debug_test chipmunk_ntt_debug_test.c)

# Add NTT symmetry test for debugging invNTT issues
add_executable(chipmunk_ntt_symmetry_test chipmunk_ntt_symmetry_test.c)

# Add test for correct n^(-1) constant calculation
add_executable(chipmunk_invntt_fix_test chipmunk_invntt_fix_test.c)

# Add simple debug test for step-by-step NTT/invNTT analysis
add_executable(chipmunk_ntt_debug_simple chipmunk_ntt_debug_simple.c)

# Add Merkle Tree test
add_executable(chipmunk_tree_test test_chipmunk_tree.c)

# Add chipmunk standalone test
add_executable(chipmunk-test-only chipmunk_test_only.c)

# Add large-scale performance test
add_executable(chipmunk-large-scale-test chipmunk_large_scale_test.c)

# Add deterministic key test
add_executable(test_deterministic_keys test_deterministic_keys.c)

# Add universal signature API test
add_executable(test-sign-api-only test_sign_api_only.c dap_sign_test.c)

target_link_libraries(${PROJECT_NAME} dap_test dap_core dap_crypto m)
target_link_libraries(chipmunk-test dap_test dap_core dap_crypto m)
target_link_libraries(chipmunk_hots_test dap_test dap_core dap_crypto m)
target_link_libraries(chipmunk_ntt_debug_test dap_test dap_core dap_crypto m)
target_link_libraries(chipmunk_ntt_symmetry_test dap_test dap_core dap_crypto m)
target_link_libraries(chipmunk_invntt_fix_test dap_test dap_core dap_crypto m)
target_link_libraries(chipmunk_ntt_debug_simple dap_test dap_core dap_crypto m)
target_link_libraries(chipmunk_tree_test dap_test dap_core dap_crypto m)
target_link_libraries(chipmunk-test-only dap_crypto dap_core m)
target_link_libraries(chipmunk-large-scale-test dap_crypto dap_core m)
target_link_libraries(test_deterministic_keys dap_crypto dap_core m)
target_link_libraries(test-sign-api-only dap_test dap_core dap_crypto m)

# Добавляем путь к dap_test.h
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework)
target_include_directories(chipmunk-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework)
target_include_directories(chipmunk_hots_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/chipmunk
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_include_directories(chipmunk_ntt_debug_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/chipmunk
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_include_directories(chipmunk_ntt_symmetry_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/chipmunk
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_include_directories(chipmunk_ntt_debug_simple PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/chipmunk
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_include_directories(chipmunk_tree_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/chipmunk
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_include_directories(chipmunk-test-only PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_include_directories(chipmunk-large-scale-test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/chipmunk
)
target_include_directories(test_deterministic_keys PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/chipmunk
)
target_include_directories(test-sign-api-only PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

add_test(
    NAME crypto-test
    COMMAND crypto-test
)

add_test(
    NAME chipmunk-test
    COMMAND chipmunk-test
)

add_test(
    NAME chipmunk-hots-test
    COMMAND chipmunk_hots_test
)

add_test(
    NAME chipmunk-tree-test
    COMMAND chipmunk_tree_test
)
