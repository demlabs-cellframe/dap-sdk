project(dap_json_rpc_core C)
cmake_minimum_required(VERSION 3.10)

add_definitions ("-D_GNU_SOURCE")
add_definitions("-Dfpic")

file(GLOB JSON_RPC_CORE_HEADERS 
    include/*.h
    )
    file(GLOB JSON_RPC_CORE_SRCS 
    src/*.c
    )

include_directories(${PROJECT_NAME} include/)

add_library(${PROJECT_NAME} STATIC ${JSON_RPC_CORE_HEADERS} ${JSON_RPC_CORE_SRCS})
target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)

target_link_libraries(${PROJECT_NAME} dap_core dap_json-c dap_client dap_cli_server)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${JSON_RPC_CORE_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/json_rpc/rpc_core/
        ARCHIVE DESTINATION lib/dap/net/server/json_rpc/rpc_core/
        PUBLIC_HEADER DESTINATION include/dap/net/server/json_rpc/rpc_core/
)
endif()