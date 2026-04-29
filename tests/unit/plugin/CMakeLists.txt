cmake_minimum_required(VERSION 3.10)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

project(test_plugin_lifecycle)

add_library(test_plugin_no_entries MODULE
    test_plugin_no_entries.c
)

add_executable(${PROJECT_NAME}
    test_plugin_lifecycle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../plugin/src/dap_plugin.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../plugin/src/dap_plugin_binary.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../plugin/src/dap_plugin_manifest.c
    test_plugin_stubs.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE dap_core_static dap_test)
if(NOT WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE dl)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../plugin/include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    TEST_BINARY_NO_ENTRIES_PATH="$<TARGET_FILE:test_plugin_no_entries>"
)
add_dependencies(${PROJECT_NAME} test_plugin_no_entries)

dap_test_add_includes(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 30
    LABELS "unit;plugin"
)
