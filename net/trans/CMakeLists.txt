cmake_minimum_required(VERSION 3.10)
project(dap_trans C)

# Include LibraryHelpers and DapModule
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LibraryHelpers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/DapModule.cmake)

# Trans implementations
add_subdirectory(websocket)
add_subdirectory(http)
add_subdirectory(udp)
add_subdirectory(dns)
# TODO: add TLS trans when implemented
# add_subdirectory(tls)

# Unified trans abstraction layer (dap_net_trans.c) and server API (dap_net_trans_server.c)
file(GLOB TRANS_CORE_SRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_net_trans.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/dap_net_trans_server.c"
)

file(GLOB TRANS_CORE_HDRS 
    "${CMAKE_CURRENT_SOURCE_DIR}/include/dap_net_trans.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/dap_net_trans_server.h"
)

# Create OBJECT library for trans core
create_object_library(dap_net_trans DAP_INTERNAL_MODULES ${TRANS_CORE_SRCS} HEADERS ${TRANS_CORE_HDRS})

# Include directories - use absolute paths for INTERFACE to ensure proper propagation from build directory
get_filename_component(DAP_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../ ABSOLUTE)
target_include_directories(dap_net_trans INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(dap_net_trans PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Include trans-specific headers
target_include_directories(dap_net_trans PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/websocket/include
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/dns/include
)

# Link dependencies
dap_link_libraries(dap_net_trans INTERFACE
    dap_core
    dap_stream
    dap_trans_http
    dap_trans_websocket
    dap_trans_udp
    dap_trans_dns
)

# Register dap_net_trans module with dap_module system
dap_register_module(
    TARGET dap_net_trans
    MODULE_NAME "dap_net_trans"
    VERSION 1
    INIT_FUNCTION "dap_net_trans_init"
    DEINIT_FUNCTION "dap_net_trans_deinit"
    DEPENDENCIES ""
    INCLUDE_HEADER "dap_net_trans.h"
)

if(INSTALL_DAP_SDK)
    set_target_properties(dap_net_trans PROPERTIES PUBLIC_HEADER "${TRANS_CORE_HDRS}")
    INSTALL(TARGETS dap_net_trans
            LIBRARY DESTINATION lib/dap/net/trans/
            ARCHIVE DESTINATION lib/dap/net/trans/
            PUBLIC_HEADER DESTINATION include/dap/net/trans/
    )
endif()
