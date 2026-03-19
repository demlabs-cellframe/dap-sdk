cmake_minimum_required(VERSION 3.10)
project(dap_trans_websocket_system C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/LibraryHelpers.cmake)

if(DAP_WASM)
    set(WS_SYSTEM_SRCS dap_net_trans_ws_system_wasm.c)
else()
    set(WS_SYSTEM_SRCS dap_net_trans_ws_system_native.c)
endif()

file(GLOB WS_SYSTEM_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${WS_SYSTEM_SRCS} HEADERS ${WS_SYSTEM_HDRS})

target_include_directories(${PROJECT_NAME} PUBLIC include)

dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_io dap_net_common dap_net_trans)

if(DAP_WASM)
    dap_link_libraries(${PROJECT_NAME} INTERFACE dap_crypto dap_json dap_stream dap_stream_ch dap_session)
endif()

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${WS_SYSTEM_HDRS}")
    INSTALL(TARGETS ${PROJECT_NAME}
            LIBRARY DESTINATION lib/dap/net/trans/websocket_system/
            ARCHIVE DESTINATION lib/dap/net/trans/websocket_system/
            PUBLIC_HEADER DESTINATION include/dap/net/trans/websocket_system/
    )
endif()
