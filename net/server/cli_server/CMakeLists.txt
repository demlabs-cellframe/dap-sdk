cmake_minimum_required(VERSION 3.10)
project (dap_cli_server C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

file(GLOB DAP_CLI_SRV_SRCS *.c)
file(GLOB DAP_CLI_SRV_HEADERS include/*.h)

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_CLI_SRV_SRCS} HEADERS ${DAP_CLI_SRV_HEADERS})

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include/)

dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_io dap_json_rpc m)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_CLI_SRV_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib/dap/net/server/cli_server/
        ARCHIVE DESTINATION lib/dap/net/server/cli_server/
        PUBLIC_HEADER DESTINATION include/dap/net/server/cli_server/
)
endif()
