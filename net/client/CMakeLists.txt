cmake_minimum_required(VERSION 3.10)
project(dap_client)
add_definitions ("-D_GNU_SOURCE")

file(GLOB DAP_CLIENT_SOURCES FILES *.c)
file(GLOB DAP_CLIENT_HEADERS FILES include/*.h)

add_library(${PROJECT_NAME} STATIC ${DAP_CLIENT_HEADERS} ${DAP_CLIENT_SOURCES})

target_link_libraries(${PROJECT_NAME} dap_core dap_crypto dap_io dap_enc_server dap_stream dap_session dap_json-c dap_http_common)
if(DAPSDK_MODULES MATCHES "ssl-support")
    target_link_libraries(${PROJECT_NAME} wolfssl)
endif()
if(UNIX AND NOT ANDROID AND NOT DARWIN)
    target_link_libraries(${PROJECT_NAME} rt)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE .)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/uthash/src)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../common/http/include)

if (${BUILD_DAP_CLIENT_TESTS} MATCHES ON)
    enable_testing()
    add_subdirectory(test)
endif()

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_CLIENT_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/client/
        ARCHIVE DESTINATION lib/dap/net/client/
        PUBLIC_HEADER DESTINATION include/dap/net/client/
)
endif()
