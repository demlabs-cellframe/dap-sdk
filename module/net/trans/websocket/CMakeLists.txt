cmake_minimum_required(VERSION 3.10)
project(dap_trans_websocket C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/LibraryHelpers.cmake)

file(GLOB SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
file(GLOB HDRS ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${SRCS} HEADERS ${HDRS})

target_include_directories(${PROJECT_NAME} PUBLIC include)

dap_link_libraries(${PROJECT_NAME} INTERFACE
    dap_core dap_crypto dap_io dap_stream dap_net_common dap_hash_table)

if(NOT DAP_WASM)
    dap_link_libraries(${PROJECT_NAME} INTERFACE dap_http_server)
endif()

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${HDRS}")
    INSTALL(TARGETS ${PROJECT_NAME}
            LIBRARY DESTINATION lib/dap/net/trans/websocket/
            ARCHIVE DESTINATION lib/dap/net/trans/websocket/
            PUBLIC_HEADER DESTINATION include/dap/net/trans/websocket/
    )
endif()
