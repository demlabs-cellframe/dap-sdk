cmake_minimum_required(VERSION 3.12)
project(dap_optimization)

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} INTERFACE dap_core)

set(DAP_PRIMITIVES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/primitives" CACHE PATH
    "Path to shared arch-specific SIMD/asm primitive templates")
