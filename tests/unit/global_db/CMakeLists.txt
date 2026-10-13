cmake_minimum_required(VERSION 3.10)
project(global_db_latch_unit)

add_executable(${PROJECT_NAME}
    ../global_db_latch_unit_test.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/global-db/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/test-framework
    ${CMAKE_SOURCE_DIR}/3rdparty/uthash/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    dap_global_db_static
    dap_test
    pthread
)

add_test(NAME global_db_latch_unit COMMAND ${PROJECT_NAME})
set_tests_properties(global_db_latch_unit PROPERTIES TIMEOUT 30 LABELS "unit;global-db")
