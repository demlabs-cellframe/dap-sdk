cmake_minimum_required(VERSION 3.10)
project(dap_json_rpc_crypto C)

file(GLOB JSON_RPC_CRYPTO_HEADERS include/*.h)
file(GLOB JSON_RPC_CRYPTO_SRCS src/*.c)

if(DAP_JSON_RPC)
    add_definitions("-DDAP_JSON_RPC")
endif()

include_directories(${PROJECT_NAME} include/)

add_library(${PROJECT_NAME} STATIC ${JSON_RPC_CORE_HEADERS} ${JSON_RPC_CRYPTO_SRCS})
target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)

target_link_libraries(${PROJECT_NAME} dap_core dap_crypto dap_io dap_http_server dap_client dap_json-c dap_json_rpc_core)
