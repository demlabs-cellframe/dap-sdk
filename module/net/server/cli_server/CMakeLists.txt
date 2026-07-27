cmake_minimum_required(VERSION 3.10)
project (dap_cli_server C)

file(GLOB DAP_CLI_SRV_SRCS *.c)
file(GLOB DAP_CLI_SRV_HEADERS include/*.h)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/LibraryHelpers.cmake)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${DAP_CLI_SRV_SRCS} HEADERS ${DAP_CLI_SRV_HEADERS})

dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_io dap_json_rpc dap_json dap_net_common m)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_CLI_SRV_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/cli_server/
        ARCHIVE DESTINATION lib/dap/net/server/cli_server/
        PUBLIC_HEADER DESTINATION include/dap/net/server/cli_server/
)
endif()
