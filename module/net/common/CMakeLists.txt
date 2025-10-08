cmake_minimum_required(VERSION 3.10)
project(dap_net_common C)

# Common network types and utilities
file(GLOB NET_COMMON_HEADERS include/*.h)
file(GLOB NET_COMMON_SOURCES *.c)

# Use dap_add_library to integrate with DAP SDK build system
dap_add_library(${PROJECT_NAME} OBJECT
    ${NET_COMMON_HEADERS}
    ${NET_COMMON_SOURCES}
)

# dap_net_common depends on dap_core (for dap_common.h)
target_link_libraries(${PROJECT_NAME} PUBLIC dap_core)

# Export include directory
target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Install headers
if(INSTALL_DAP_SDK)
    install(FILES ${NET_COMMON_HEADERS}
        DESTINATION include/dap/net/common
    )
endif()

message("[+] DAP Net Common Module: network types, callbacks and utilities")
