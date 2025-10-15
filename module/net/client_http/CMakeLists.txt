cmake_minimum_required(VERSION 3.10)
project(dap_client_http C)
add_definitions ("-D_GNU_SOURCE")

set(CMAKE_C_STANDARD 11)

file(GLOB CLIENT_HTTP_HEADERS include/*.h)
file(GLOB CLIENT_HTTP_SRCS *.c)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${CLIENT_HTTP_SRCS} HEADERS ${CLIENT_HTTP_HEADERS})

# Link with required modules - minimal dependencies, no dap_client or dap_stream
target_link_libraries(${PROJECT_NAME} dap_core dap_crypto dap_io dap_net_common)

target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${CLIENT_HTTP_HEADERS}")
    INSTALL(TARGETS ${PROJECT_NAME} 
            LIBRARY DESTINATION lib/dap/net/client_http/
            ARCHIVE DESTINATION lib/dap/net/client_http/
            PUBLIC_HEADER DESTINATION include/dap/net/client_http/
    )
endif()

message("[+] DAP Client HTTP Module: standalone HTTP client for outgoing requests")

