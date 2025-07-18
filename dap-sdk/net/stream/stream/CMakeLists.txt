cmake_minimum_required(VERSION 3.10)
project(dap_stream C)
  
file(GLOB STREAM_SRCS *.c)
file(GLOB STREAM_HDRS include/*.h)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${STREAM_SRCS} HEADERS ${STREAM_HDRS})

target_link_libraries(${PROJECT_NAME} dap_core dap_io dap_crypto dap_http_server dap_enc_server dap_session)

if(DAP_STREAM_TEST)
    target_link_libraries(${PROJECT_NAME} dap_stream_test)
endif()

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC ../ch/include)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include)
target_include_directories(${PROJECT_NAME} PUBLIC ../../client/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../crypto/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../io/include)
target_include_directories(${PROJECT_NAME} PUBLIC ../../link_manager/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../server/http_server/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/uthash/src)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${STREAM_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/stream/stream/
        ARCHIVE DESTINATION lib/dap/net/stream/stream/
        PUBLIC_HEADER DESTINATION include/dap/net/stream/stream/
)
endif()