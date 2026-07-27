cmake_minimum_required(VERSION 3.10)
project(dap_io C)

dap_add_library(${PROJECT_NAME} dap_io_wasm_stub.c)
target_link_libraries(${PROJECT_NAME} dap_core)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
