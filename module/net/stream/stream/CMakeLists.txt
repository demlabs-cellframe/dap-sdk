cmake_minimum_required(VERSION 3.10)
project(dap_stream C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/LibraryHelpers.cmake)

file(GLOB STREAM_SRCS *.c)
file(GLOB STREAM_HDRS include/*.h)

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${STREAM_SRCS} HEADERS ${STREAM_HDRS})

dap_link_libraries(${PROJECT_NAME} INTERFACE
    dap_core dap_io dap_crypto dap_hash_table dap_json
    dap_stream_ch dap_session dap_net_common
    dap_net_trans dap_trans_websocket dap_trans_http)

if(NOT DAP_WASM)
    dap_link_libraries(${PROJECT_NAME} INTERFACE
        dap_http_server dap_enc_server dap_client dap_global_db
        dap_link_manager dap_trans_udp)
endif()

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${STREAM_HDRS}")
    INSTALL(TARGETS ${PROJECT_NAME}
            LIBRARY DESTINATION lib/dap/net/stream/stream/
            ARCHIVE DESTINATION lib/dap/net/stream/stream/
            PUBLIC_HEADER DESTINATION include/dap/net/stream/stream/
    )
endif()
