cmake_minimum_required(VERSION 3.10)
project(dap_net_common C)

# This is header-only module with common types
file(GLOB NET_COMMON_HEADERS include/*.h)

# Header-only library using INTERFACE
add_library(${PROJECT_NAME} INTERFACE)

# Export include directory
target_include_directories(${PROJECT_NAME} INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Install headers
if(INSTALL_DAP_SDK)
    install(FILES ${NET_COMMON_HEADERS}
        DESTINATION include/dap/net/common
    )
endif()

message("[+] DAP Net Common Module (header-only): types and callbacks")
