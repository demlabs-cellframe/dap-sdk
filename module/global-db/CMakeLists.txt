cmake_minimum_required(VERSION 3.10)
project (dap_global_db C)

file(GLOB dap_global_db_SRC 
    dap_global_db.c 
    dap_global_db_ch.c 
    dap_global_db_pkt.c 
    dap_global_db_cluster.c 
    dap_global_db_btree.c
    dap_global_db_obj.c
    dap_global_db_wal.c
)
file(GLOB dap_global_db_HDR include/*.h)

# Dependencies
set(dap_global_db_LIBS dap_core dap_io dap_crypto dap_json dap_net_common dap_enc_server dap_json_rpc dap_stream dap_link_manager)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${dap_global_db_SRC} HEADERS ${dap_global_db_HDR})

target_link_libraries(${PROJECT_NAME} ${dap_global_db_LIBS})

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)

# All other includes auto-propagate via linked modules (dap_net_common provides base types)

# Export dependencies for main SDK build
set(DAP_GLOBAL_DB_DEPS "${dap_global_db_LIBS}" CACHE INTERNAL "dap_global_db external dependencies")

# CLI module for global_db
add_subdirectory(cli)
# Migration module
add_subdirectory(migrate)

if (BUILD_GLOBAL_DB_TEST)
    add_subdirectory(test)
endif()
if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${dap_global_db_HDR}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/global_db/
        ARCHIVE DESTINATION lib/dap/global_db/
        PUBLIC_HEADER DESTINATION include/dap/global_db/
)
endif()
