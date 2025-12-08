cmake_minimum_required(VERSION 3.10)
project(dap_trans_dns C)

file(GLOB TRANS_DNS_SRCS FILES *.c)
file(GLOB TRANS_DNS_HDRS FILES include/*.h)
# Explicitly include DNS server and stream files
list(APPEND TRANS_DNS_SRCS dap_net_trans_dns_server.c dap_net_trans_dns_stream.c)
list(APPEND TRANS_DNS_HDRS include/dap_net_trans_dns_server.h include/dap_net_trans_dns_stream.h)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/DapModule.cmake)

include_directories(${PROJECT_NAME} include)

# Note: DNS trans stream implementation is created
# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${TRANS_DNS_SRCS} HEADERS ${TRANS_DNS_HDRS})

# Register module with dap_module system for automatic initialization
dap_register_module(
    MODULE_NAME "trans_dns"
    VERSION 1
    INIT_FUNCTION "dap_net_trans_dns_stream_register"
    DEINIT_FUNCTION "dap_net_trans_dns_stream_unregister"
    DEPENDENCIES "dap_net_trans"
    INCLUDE_HEADER "dap_net_trans_dns_stream.h"
)

# Include directories
target_include_directories(${PROJECT_NAME} INTERFACE . ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Add stream include directory for stream headers
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../stream/stream/include)
# Add net_trans_server include directory
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
# Add http_server include directory (for common structures) - PUBLIC to propagate through dependencies
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../server/http_server/include)
# Add enc_server include directory
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../server/enc_server/include)

# Link dependencies as INTERFACE for OBJECT library
# IMPORTANT: Need same dependencies as UDP trans to get all include directories
# Add dap_client, dap_http_common and other dependencies to ensure all include directories are propagated
# These dependencies provide transitive include directories for http_server
dap_link_libraries(${PROJECT_NAME} INTERFACE 
    dap_core 
    dap_core_unix
    dap_io 
    dap_crypto
    dap_http_common
    dap_http_server
    dap_enc_server
    dap_client
    dap_stream
    dap_stream_ch
    dap_net_trans
)

# IMPORTANT: Explicitly add http_server and enc_server include directories using absolute paths
# These are needed because dap_stream.h includes headers that require these directories
# The absolute paths ensure they work regardless of build directory location
# These paths match what UDP trans gets through transitive dependencies
# Use CMAKE_SOURCE_DIR to get the correct absolute path to the source directory
get_filename_component(HTTP_SERVER_DIR "${CMAKE_SOURCE_DIR}/net/server/http_server" ABSOLUTE)
get_filename_component(HTTP_COMMON_DIR "${CMAKE_SOURCE_DIR}/net/common/http" ABSOLUTE)
get_filename_component(ENC_SERVER_DIR "${CMAKE_SOURCE_DIR}/net/server/enc_server" ABSOLUTE)
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${HTTP_SERVER_DIR}
    ${HTTP_SERVER_DIR}/include
    ${HTTP_SERVER_DIR}/http_client/include
    ${HTTP_COMMON_DIR}/include
    ${ENC_SERVER_DIR}
    ${ENC_SERVER_DIR}/include
)
