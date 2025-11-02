cmake_minimum_required(VERSION 3.10)
project(dap_transport C)

# Transport implementations
add_subdirectory(websocket)
add_subdirectory(http)
add_subdirectory(udp)
add_subdirectory(dns)
# TODO: add TLS transport when implemented
# add_subdirectory(tls)

# Unified transport server API
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LibraryHelpers.cmake)

file(GLOB TRANSPORT_UNIFIED_SRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_net_transport_server.c"
)

file(GLOB TRANSPORT_UNIFIED_HDRS 
    "${CMAKE_CURRENT_SOURCE_DIR}/include/dap_net_transport_server.h"
)

# Create OBJECT library for unified API
create_object_library(dap_net_transport DAP_INTERNAL_MODULES ${TRANSPORT_UNIFIED_SRCS} HEADERS ${TRANSPORT_UNIFIED_HDRS})

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

# Link dependencies - need all transport server implementations
dap_link_libraries(dap_net_transport INTERFACE
    dap_core
    dap_stream
    dap_transport_http
    dap_transport_websocket
    dap_transport_udp
    dap_transport_dns
)

if(INSTALL_DAP_SDK)
    set_target_properties(dap_net_transport PROPERTIES PUBLIC_HEADER "${TRANSPORT_UNIFIED_HDRS}")
    INSTALL(TARGETS dap_net_transport
            LIBRARY DESTINATION lib/dap/net/transport/
            ARCHIVE DESTINATION lib/dap/net/transport/
            PUBLIC_HEADER DESTINATION include/dap/net/transport/
    )
endif()


