cmake_minimum_required(VERSION 3.10)
project(dap_net_server_common)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

file(GLOB DAP_NET_SERVER_COMMON_HEADERS include/*.h)
file(GLOB DAP_NET_SERVER_COMMON_SOURCES src/*.c)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_NET_SERVER_COMMON_SOURCES} HEADERS ${DAP_NET_SERVER_COMMON_HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link dependencies as INTERFACE
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_io)

# Setup install paths automatically
if(INSTALL_DAP_SDK)
    dap_setup_install_paths(${PROJECT_NAME} HEADERS ${DAP_NET_SERVER_COMMON_HEADERS})
endif()

