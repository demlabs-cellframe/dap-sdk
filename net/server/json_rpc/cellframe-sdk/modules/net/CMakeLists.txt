cmake_minimum_required(VERSION 3.10)
project (dap_json_rpc_chain_net)

file(GLOB DAP_JSON_RPC_CHAIN_NET_HEADERS include/*.h)
file(GLOB DAP_JSON_RPC_CHAIN_NET_SRCS src/*.c)

add_library(${PROJECT_NAME} STATIC ${DAP_JSON_RPC_CHAIN_NET_SRCS} ${DAP_JSON_RPC_CHAIN_NET_HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC include)

target_link_libraries(${PROJECT_NAME} dap_core dap_crypto dap_client dap_io dap_notify_srv dap_cli_server dap_chain dap_chain_wallet
                                        dap_chain_net_srv dap_chain_mempool dap_global_db dap_chain_net_srv_xchange dap_chain_cs_none
                                        dap_stream_ch_chain_net dap_stream_ch_chain_voting dap_chain_net dap_json-c dap_json_rpc_core dap_json_rpc_chain_common)

