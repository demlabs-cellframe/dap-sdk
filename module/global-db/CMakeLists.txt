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

set(dap_global_db_LIBS dap_core dap_io dap_crypto dap_json dap_net_common dap_enc_server dap_json_rpc dap_stream dap_stream_ch dap_link_manager dap_cluster)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${dap_global_db_SRC} HEADERS ${dap_global_db_HDR})

dap_link_libraries(${PROJECT_NAME} INTERFACE ${dap_global_db_LIBS})

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)

# Export dependencies for main SDK build
set(DAP_GLOBAL_DB_DEPS "${dap_global_db_LIBS}" CACHE INTERNAL "dap_global_db external dependencies")

if(NOT DAP_WASM)
    add_subdirectory(migrate)
endif()

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
