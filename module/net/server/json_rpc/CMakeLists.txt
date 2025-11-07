cmake_minimum_required(VERSION 3.10)
project(dap_json_rpc C)

set(CMAKE_C_STANDARD 11)

file(GLOB JSON_RPC_HEADERS include/*.h)
file(GLOB JSON_RPC_SRCS src/*.c)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${JSON_RPC_SRCS} HEADERS ${JSON_RPC_HEADERS})

# Link with required modules - no dap_client/dap_stream dependencies to break circular dependency
target_link_libraries(${PROJECT_NAME} dap_core dap_crypto dap_io dap_net_common dap_http_server dap_enc_server dap_client_http)

target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC 
        include
        ${CMAKE_CURRENT_SOURCE_DIR}/../cli_server/include
)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${JSON_RPC_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/json_rpc/
        ARCHIVE DESTINATION lib/dap/net/server/json_rpc/
        PUBLIC_HEADER DESTINATION include/dap/net/server/json_rpc/
)
endif()
