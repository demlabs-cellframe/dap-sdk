cmake_minimum_required(VERSION 3.10)
project(dap_transport_websocket C)

file(GLOB TRANSPORT_WEBSOCKET_SRCS FILES *.c)
file(GLOB TRANSPORT_WEBSOCKET_HDRS FILES include/*.h)

if(WIN32)
    add_definitions("-DUNDEBUG")
    add_definitions("-DNDEBUG")
    add_definitions("-DWIN32")
    add_definitions("-D_WINDOWS")
    add_definitions("-D__WINDOWS__")
    add_definitions("-D_CRT_SECURE_NO_WARNINGS")
endif()

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/DapModule.cmake)

include_directories(${PROJECT_NAME} include)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${TRANSPORT_WEBSOCKET_SRCS} HEADERS ${TRANSPORT_WEBSOCKET_HDRS})

# Register module with dap_module system for automatic initialization
dap_register_module(
    MODULE_NAME "transport_websocket"
    VERSION 1
    INIT_FUNCTION "dap_net_transport_websocket_stream_register"
    DEINIT_FUNCTION "dap_net_transport_websocket_stream_unregister"
    DEPENDENCIES "dap_net_transport"
    INCLUDE_HEADER "dap_net_transport_websocket_stream.h"
)

# Include directories
target_include_directories(${PROJECT_NAME} INTERFACE . ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Add stream include directory for stream headers
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../stream/stream/include)
# Add enc_server include directory
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../server/enc_server/include)
# Add net_transport_server include directory
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Find OpenSSL for SHA-1
find_package(OpenSSL REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} INTERFACE ${OPENSSL_LIBRARIES})

if(DARWIN)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include)
endif()

# Link dependencies as INTERFACE for OBJECT library
dap_link_libraries(${PROJECT_NAME} INTERFACE 
    dap_core 
    dap_core_unix
    dap_io 
    dap_crypto 
    dap_http_server
    dap_enc_server
    dap_stream
    dap_stream_ch
    dap_net_transport
    dap_net_server_common
)

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${TRANSPORT_WEBSOCKET_HDRS}")
    INSTALL(TARGETS ${PROJECT_NAME} 
            LIBRARY DESTINATION lib/dap/net/transport/websocket/
            ARCHIVE DESTINATION lib/dap/net/transport/websocket/
            PUBLIC_HEADER DESTINATION include/dap/net/transport/websocket/
    )
endif()

