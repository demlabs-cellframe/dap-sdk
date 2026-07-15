cmake_minimum_required(VERSION 3.10)
project(dap_link_manager_test)

# Include test helpers — defines dap_test_link_libraries().
# This file lives in tests/cmake/ but net/link_manager/test/ is processed
# before the tests/ subtree, so we must include it explicitly.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/cmake/dap_test_helpers.cmake)

add_executable(${PROJECT_NAME} dap_link_manager_test.c)

# Use dap_test_link_libraries to link all SDK static libraries.
# Direct target_link_libraries with OBJECT libraries misses transitive
# OBJECT dependencies (e.g. dap_crypto depends on dap_crypto_kyber512 objects).
dap_test_link_libraries(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(
    NAME dap_link_manager_test
    COMMAND dap_link_manager_test
)
