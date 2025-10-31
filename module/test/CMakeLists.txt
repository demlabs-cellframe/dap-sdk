cmake_minimum_required(VERSION 3.10)
if(TARGET dap_test)
    return() # The project has already been built.
endif()
project(dap_test)

add_library(${PROJECT_NAME} STATIC dap_test.h dap_test.c dap_mock.c dap_mock.h dap_mock_linker_wrapper.h dap_test_async.c dap_test_async.h dap_test_generator.h dap_test_generator.c)

target_include_directories(dap_test INTERFACE .)
target_link_libraries(dap_test dap_core)

