cmake_minimum_required(VERSION 3.10)
project (dap_session)
  
file(GLOB SESSION_SRCS *.c)
file(GLOB SESSION_HDRS include/*.h)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${SESSION_SRCS} HEADERS ${SESSION_HDRS})

target_link_libraries(dap_session dap_core dap_crypto dap_net_common)

include_directories(include/)
target_include_directories(dap_session INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/uthash/src)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../crypto/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../crypto/src/XKCP/lib/common)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../crypto/src/XKCP/lib/high/Keccak/FIPS202)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${SESSION_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/stream/session/
        ARCHIVE DESTINATION lib/dap/net/stream/session/
        PUBLIC_HEADER DESTINATION include/dap/net/stream/session/
)
endif()