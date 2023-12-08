project(dap_json_rpc C)
cmake_minimum_required(VERSION 3.10)

add_definitions ("-D_GNU_SOURCE")
add_definitions("-Dfpic")

file(GLOB JSON_RPC_HEADERS include/*.h)
file(GLOB JSON_RPC_SRCS src/*.c)

include_directories(${PROJECT_NAME} include/)

add_library(${PROJECT_NAME} STATIC ${JSON_RPC_HEADERS} ${JSON_RPC_SRCS})
target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)

target_link_libraries(${PROJECT_NAME} dap_core dap_crypto dap_io dap_http_server dap_client dap_json-c)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${JSON_RPC_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/json_rpc/
        ARCHIVE DESTINATION lib/dap/net/server/json_rpc/
        PUBLIC_HEADER DESTINATION include/dap/net/server/json_rpc/
)
endif()