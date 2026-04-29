cmake_minimum_required(VERSION 3.10)
project(dap_chain_btc_rpc C)

add_definitions("-D_GNU_SOURCE")
add_definitions("-Dfpic")

file(GLOB BTC_RPC_HEADERS include/*.h)
file(GLOB BTC_RPC_SRCS *.c)

add_library(${PROJECT_NAME} STATIC ${BTC_RPC_HEADERS} ${BTC_RPC_SRCS})
target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_link_libraries(${PROJECT_NAME} PUBLIC dap_core dap_json_rpc_core)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${BTC_RPC_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib/dap/net/server/json_rpc/btc_rpc/
        ARCHIVE DESTINATION lib/dap/net/server/json_rpc/btc_rpc/
        PUBLIC_HEADER DESTINATION include/dap/net/server/json_rpc/btc_rpc/
)
endif()
