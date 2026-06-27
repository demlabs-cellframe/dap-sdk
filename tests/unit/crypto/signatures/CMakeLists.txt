cmake_minimum_required(VERSION 3.10)

project(test_signatures)

add_executable(${PROJECT_NAME} test_signatures.c)

target_link_libraries(${PROJECT_NAME} PRIVATE
    dap_sdk
    dap_test
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ../../../../tests/fixtures/utilities
)

add_test(NAME ${PROJECT_NAME}
         COMMAND ${PROJECT_NAME})
