project(dap_json_rpc_core C)
cmake_minimum_required(VERSION 3.10)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/LibraryHelpers.cmake)

add_definitions ("-D_GNU_SOURCE")

file(GLOB JSON_RPC_CORE_HEADERS
    include/*.h
    )
file(GLOB JSON_RPC_CORE_SRCS
    src/*.c
    )

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${JSON_RPC_CORE_SRCS} HEADERS ${JSON_RPC_CORE_HEADERS})

target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)

dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_json-c dap_client dap_cli_server)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${JSON_RPC_CORE_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib/dap/net/server/json_rpc/rpc_core/
        ARCHIVE DESTINATION lib/dap/net/server/json_rpc/rpc_core/
        PUBLIC_HEADER DESTINATION include/dap/net/server/json_rpc/rpc_core/
)
endif()
