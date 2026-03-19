cmake_minimum_required(VERSION 3.10)
project(dap_client C)
add_definitions("-D_GNU_SOURCE")

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

file(GLOB DAP_CLIENT_HEADERS FILES include/*.h)

file(GLOB DAP_CLIENT_SOURCES FILES *.c)
list(FILTER DAP_CLIENT_SOURCES EXCLUDE REGEX ".*_wasm\\.c$")

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_CLIENT_SOURCES} HEADERS ${DAP_CLIENT_HEADERS})

if(DAP_WASM)
    dap_link_libraries(${PROJECT_NAME} INTERFACE
        dap_core dap_crypto dap_io dap_io_flow dap_stream dap_stream_ch dap_session
        dap_json dap_hash_table dap_net_trans)
else()
    dap_link_libraries(${PROJECT_NAME} INTERFACE
        dap_core dap_crypto dap_io dap_io_flow dap_enc_server dap_stream dap_stream_ch dap_session
        dap_json dap_hash_table dap_http_server dap_net_trans)
    if(DAPSDK_MODULES MATCHES "ssl-support")
        target_link_libraries(${PROJECT_NAME} INTERFACE wolfssl)
    endif()
    if(UNIX AND NOT ANDROID AND NOT DARWIN)
        set(DAP_CLIENT_LIBRARIES rt)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE .)

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_CLIENT_HEADERS}")
    INSTALL(TARGETS ${PROJECT_NAME} 
            LIBRARY DESTINATION lib/dap/net/client/
            ARCHIVE DESTINATION lib/dap/net/client/
            PUBLIC_HEADER DESTINATION include/dap/net/client/
    )
endif()
