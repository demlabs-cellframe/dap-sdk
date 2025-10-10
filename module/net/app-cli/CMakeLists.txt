cmake_minimum_required(VERSION 3.10)
project (dap_app_cli)

file(GLOB DAP_APP_CLI_SRCS *.c)

file(GLOB DAP_APP_CLI_HEADERS include/*.h)

add_library(${PROJECT_NAME} STATIC ${DAP_APP_CLI_SRCS} ${DAP_APP_CLI_HEADERS} )

target_link_libraries(${PROJECT_NAME} dap_core dap_io dap_cli_server dap_json_rpc m)
target_include_directories(${PROJECT_NAME} PUBLIC include/ )

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_APP_CLI_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/app-cli/
        ARCHIVE DESTINATION lib/dap/net/app-cli/
        PUBLIC_HEADER DESTINATION include/dap/net/app-cli/
)
endif()