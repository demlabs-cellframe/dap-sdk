cmake_minimum_required(VERSION 3.12)
project(dap_timer)

# Include required CMake modules
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PackageComponents.cmake)

# Timer module sources
file(GLOB TIMER_HEADERS include/*.h)
file(GLOB TIMER_SRCS src/*.c)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${TIMER_SRCS} HEADERS ${TIMER_HEADERS})

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Dependencies - needs core for dap_common.h and hash_table for dap_ht.h
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_hash_table)

# Platform-specific libraries
if(UNIX AND NOT APPLE AND NOT ANDROID)
    # rt library for timer_create on Linux (Android libc includes rt)
    target_link_libraries(${PROJECT_NAME} INTERFACE rt)
endif()

# Export headers for installation
if(INSTALL_DAP_SDK)
    install(FILES ${TIMER_HEADERS} DESTINATION include/dap/timer/)
endif()
