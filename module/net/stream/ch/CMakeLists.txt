cmake_minimum_required(VERSION 3.10)
project(dap_stream_ch)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/LibraryHelpers.cmake)

file(GLOB DAP_STREAM_CH_SRCS *.c)
file(GLOB DAP_STREAM_CH_HDRS include/*.h)

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_STREAM_CH_SRCS} HEADERS ${DAP_STREAM_CH_HDRS})

dap_link_libraries(${PROJECT_NAME} INTERFACE
    dap_core dap_crypto dap_io dap_hash_table dap_stream dap_net_common)

if(NOT DAP_WASM)
    dap_link_libraries(${PROJECT_NAME} INTERFACE
        dap_io_flow dap_http_server)
endif()

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${DAP_STREAM_CH_HDRS}")
    INSTALL(TARGETS ${PROJECT_NAME}
            LIBRARY DESTINATION lib/dap/net/stream/ch/
            ARCHIVE DESTINATION lib/dap/net/stream/ch/
            PUBLIC_HEADER DESTINATION include/dap/net/stream/ch/
    )
endif()
