cmake_minimum_required(VERSION 3.10)
project(dap_transport C)

# Include LibraryHelpers and DapModule
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LibraryHelpers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/DapModule.cmake)

# Transport implementations
add_subdirectory(websocket)
add_subdirectory(http)
add_subdirectory(udp)
add_subdirectory(dns)
# TODO: add TLS transport when implemented
# add_subdirectory(tls)

# Unified transport abstraction layer (dap_net_transport.c) and server API (dap_net_transport_server.c)
file(GLOB TRANSPORT_CORE_SRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_net_transport.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_net_transport_server.c"
)

file(GLOB TRANSPORT_CORE_HDRS 
    "${CMAKE_CURRENT_SOURCE_DIR}/include/dap_net_transport.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/dap_net_transport_server.h"
)

# Create OBJECT library for transport core
create_object_library(dap_net_transport DAP_INTERNAL_MODULES ${TRANSPORT_CORE_SRCS} HEADERS ${TRANSPORT_CORE_HDRS})

# Include directories
target_include_directories(dap_net_transport INTERFACE . ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(dap_net_transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Include transport-specific headers
target_include_directories(dap_net_transport PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/websocket/include
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/dns/include
)

# Link dependencies
dap_link_libraries(dap_net_transport INTERFACE
    dap_core
    dap_stream
    dap_transport_http
    dap_transport_websocket
    dap_transport_udp
    dap_transport_dns
)

# Register dap_net_transport module with dap_module system
dap_register_module(
    TARGET dap_net_transport
    MODULE_NAME "dap_net_transport"
    VERSION 1
    INIT_FUNCTION "dap_net_transport_init"
    DEINIT_FUNCTION "dap_net_transport_deinit"
    DEPENDENCIES ""
    INCLUDE_HEADER "dap_net_transport.h"
)

if(INSTALL_DAP_SDK)
    set_target_properties(dap_net_transport PROPERTIES PUBLIC_HEADER "${TRANSPORT_CORE_HDRS}")
    INSTALL(TARGETS dap_net_transport
            LIBRARY DESTINATION lib/dap/net/transport/
            ARCHIVE DESTINATION lib/dap/net/transport/
            PUBLIC_HEADER DESTINATION include/dap/net/transport/
    )
endif()
