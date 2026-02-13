cmake_minimum_required(VERSION 3.10)
project(dap_global_db_cli C)

file(GLOB dap_global_db_cli_SRC *.c)
file(GLOB dap_global_db_cli_HDR include/*.h)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${dap_global_db_cli_SRC} HEADERS ${dap_global_db_cli_HDR})

target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_global_db
    dap_json
    dap_json_rpc
    dap_cli_server
)

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${dap_global_db_cli_HDR}")
    INSTALL(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib/dap/global_db/cli/
        ARCHIVE DESTINATION lib/dap/global_db/cli/
        PUBLIC_HEADER DESTINATION include/dap/global_db/cli/
    )
endif()
