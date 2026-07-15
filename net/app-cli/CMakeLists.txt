cmake_minimum_required(VERSION 3.10)
project (dap_app_cli)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LibraryHelpers.cmake)

file(GLOB DAP_APP_CLI_SRCS *.c)
file(GLOB DAP_APP_CLI_HEADERS include/*.h)

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_APP_CLI_SRCS} HEADERS ${DAP_APP_CLI_HEADERS})

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include/)

dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_io dap_cli_server m)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_APP_CLI_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib/dap/net/app-cli/
        ARCHIVE DESTINATION lib/dap/net/app-cli/
        PUBLIC_HEADER DESTINATION include/dap/net/app-cli/
)
endif()
