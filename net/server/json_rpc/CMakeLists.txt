cmake_minimum_required(VERSION 3.10)
project(dap_json_rpc C)

set(CMAKE_C_STANDARD 11)

file(GLOB JSON_RPC_HEADERS include/*.h)
file(GLOB JSON_RPC_SRCS src/*.c)


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/rpc_core/)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/btc_rpc/)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dap-sdk/core/)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cellframe-sdk/modules/common/)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cellframe-sdk/modules/mempool/)


add_library(${PROJECT_NAME} STATIC ${JSON_RPC_HEADERS} ${JSON_RPC_SRCS})
target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)

target_link_libraries(${PROJECT_NAME} dap_json-c dap_json_rpc_core dap_json_rpc_chain_common dap_json_rpc_chain_mempool)
