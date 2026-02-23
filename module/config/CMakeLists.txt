cmake_minimum_required(VERSION 3.12)
project(dap_config)

# Include required CMake modules
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PackageComponents.cmake)

# Config module sources
file(GLOB CONFIG_HEADERS include/*.h)
file(GLOB CONFIG_SRCS src/*.c)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${CONFIG_SRCS} HEADERS ${CONFIG_HEADERS})

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Dependencies - needs core and hash_table (for dap_ht.h)
target_link_libraries(${PROJECT_NAME} dap_core dap_hash_table)

# Export headers for installation
if(INSTALL_DAP_SDK)
    install(FILES ${CONFIG_HEADERS} DESTINATION include/dap/config/)
endif()
