cmake_minimum_required(VERSION 3.10)
project(dap_http_common)

file(GLOB DAP_HTTP_COMMON_HEADERS include/*.h)
file(GLOB DAP_HTTP_COMMON_SOURCES src/*.c)

add_library(${PROJECT_NAME} STATIC ${DAP_HTTP_COMMON_SOURCES} ${DAP_HTTP_COMMON_HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/uthash/src
)

target_link_libraries(${PROJECT_NAME} dap_core)

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${DAP_HTTP_COMMON_HEADERS}")
    INSTALL(TARGETS ${PROJECT_NAME} 
            LIBRARY DESTINATION lib/dap/net/common/http
            ARCHIVE DESTINATION lib/dap/net/common/http
            PUBLIC_HEADER DESTINATION include/dap/net/common/http/
    )
endif() 