cmake_minimum_required(VERSION 3.10)
project(dap_json_rpc C)

set(CMAKE_C_STANDARD 11)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

file(GLOB JSON_RPC_HEADERS include/*.h)
file(GLOB JSON_RPC_SRCS src/*.c)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/rpc_core/)

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${JSON_RPC_SRCS} HEADERS ${JSON_RPC_HEADERS})

target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)

dap_link_libraries(${PROJECT_NAME} INTERFACE dap_json-c dap_json_rpc_core)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${JSON_RPC_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib/dap/net/server/json_rpc/
        ARCHIVE DESTINATION lib/dap/net/server/json_rpc/
        PUBLIC_HEADER DESTINATION include/dap/net/server/json_rpc/
)
endif()
