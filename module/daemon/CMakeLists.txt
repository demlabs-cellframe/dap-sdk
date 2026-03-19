cmake_minimum_required(VERSION 3.12)
project(dap_daemon)

# Include required CMake modules
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PackageComponents.cmake)

# Daemon module sources
file(GLOB DAEMON_HEADERS include/*.h)
file(GLOB DAEMON_SRCS src/*.c)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${DAEMON_SRCS} HEADERS ${DAEMON_HEADERS})

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Dependencies
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_timer)

# Export headers for installation
if(INSTALL_DAP_SDK)
    install(FILES ${DAEMON_HEADERS} DESTINATION include/dap/daemon/)
endif()
