cmake_minimum_required(VERSION 3.10)
project(dap_net_trans C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

add_subdirectory(websocket_system)
if(NOT DAP_WASM)
    add_subdirectory(http)
    add_subdirectory(websocket)
    add_subdirectory(udp)
    add_subdirectory(dns)
    add_subdirectory(tls)
endif()

set(TRANS_CORE_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_net_trans.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_transport_obfuscation.c"
)
if(NOT DAP_WASM)
    list(APPEND TRANS_CORE_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/dap_net_trans_server.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/dap_net_trans_qos.c"
    )
endif()
file(GLOB TRANS_CORE_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${TRANS_CORE_SRCS} HEADERS ${TRANS_CORE_HDRS})

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE
    websocket_system/include
)
if(NOT DAP_WASM)
    target_include_directories(${PROJECT_NAME} PRIVATE
        http/include
        websocket/include
        udp/include
        dns/include
        tls/include
    )
endif()

dap_link_libraries(${PROJECT_NAME} INTERFACE
    dap_core dap_crypto dap_io dap_hash_table dap_net_common
    dap_stream dap_trans_websocket_system)

if(NOT DAP_WASM)
    dap_link_libraries(${PROJECT_NAME} INTERFACE
        dap_trans_http dap_trans_websocket
        dap_client dap_http_server
        dap_trans_udp dap_trans_dns dap_trans_tls)
endif()

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${TRANS_CORE_HDRS}")
    INSTALL(TARGETS ${PROJECT_NAME}
            LIBRARY DESTINATION lib/dap/net/trans/
            ARCHIVE DESTINATION lib/dap/net/trans/
            PUBLIC_HEADER DESTINATION include/dap/net/trans/
    )
endif()
