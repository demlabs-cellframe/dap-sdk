cmake_minimum_required(VERSION 3.10)
project(dap_json_rpc C)

set(CMAKE_C_STANDARD 11)

file(GLOB JSON_RPC_HEADERS include/*.h)
file(GLOB JSON_RPC_SRCS src/*.c)




add_library(${PROJECT_NAME} STATIC ${JSON_RPC_HEADERS} ${JSON_RPC_SRCS})
target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)
# Platform-specific include paths
if(WIN32)
    set(PLATFORM_SPECIFIC_INCLUDES ../../../core/src/win32/)
else()
    set(PLATFORM_SPECIFIC_INCLUDES ../../../core/src/unix/)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    ../../../core/include/
    ${PLATFORM_SPECIFIC_INCLUDES}
    ../../../crypto/include/
    ../../../io/include/
    ../http_server/include/
    ../http_server/http_client/include/
    ../enc_server/include/
    ../../include/
    ../../client/include/
    ../../stream/stream/include/
    ../../stream/session/include/
    ../../stream/ch/include/
    ../../../../3rdparty/uthash/src/
    ../../../../3rdparty/
)

target_link_libraries(${PROJECT_NAME} dap_core dap_crypto)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${JSON_RPC_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/json_rpc/
        ARCHIVE DESTINATION lib/dap/net/server/json_rpc/
        PUBLIC_HEADER DESTINATION include/dap/net/server/json_rpc/
)
endif()
