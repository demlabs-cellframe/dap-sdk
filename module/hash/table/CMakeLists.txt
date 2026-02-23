cmake_minimum_required(VERSION 3.12)
project(dap_hash_table)

# Include required CMake modules
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/PackageComponents.cmake)

# Hash table module - header-only (intrusive hash table)
file(GLOB TABLE_HEADERS include/*.h)

# Use interface library for header-only
add_library(${PROJECT_NAME} INTERFACE)

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

# Public include directories - propagate to dependents
target_include_directories(${PROJECT_NAME} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # for dap_hash_fast.h from parent hash module
)

# Depend on core for dap_common.h
target_link_libraries(${PROJECT_NAME} INTERFACE dap_core)

# Export headers for installation
if(INSTALL_DAP_SDK)
    install(FILES ${TABLE_HEADERS} DESTINATION include/dap/hash/table/)
endif()
