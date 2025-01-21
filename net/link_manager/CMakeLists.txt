cmake_minimum_required(VERSION 3.10)
project(dap_link_manager)
add_definitions ("-D_GNU_SOURCE")

file(GLOB DAP_LINK_MANAGER_SOURCES FILES *.c)
file(GLOB DAP_LINK_MANAGER_HEADERS FILES include/*.h)

add_library(${PROJECT_NAME} STATIC ${DAP_LINK_MANAGER_HEADERS} ${DAP_LINK_MANAGER_SOURCES})


if(DAPSDK_MODULES MATCHES "ssl-support")
    target_link_libraries(${PROJECT_NAME} dap_core dap_crypto dap_io dap_http_server dap_enc_server dap_stream dap_session dap_stream_ch dap_json-c wolfssl)
else()
    target_link_libraries(${PROJECT_NAME} dap_core dap_crypto dap_io dap_http_server dap_enc_server dap_stream dap_session dap_stream_ch dap_json-c)
endif()
if(UNIX AND NOT ANDROID AND NOT DARWIN)
    target_link_libraries(${PROJECT_NAME} rt)
endif()


target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE .)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/uthash/src)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_LINK_MANAGER_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/link_manager/
        ARCHIVE DESTINATION lib/dap/net/link_manager/
        PUBLIC_HEADER DESTINATION include/dap/net/link_manager/
)
endif()