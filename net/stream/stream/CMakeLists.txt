cmake_minimum_required(VERSION 3.10)
project(dap_stream C)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
  
file(GLOB STREAM_SRCS *.c)
file(GLOB STREAM_HDRS include/*.h)

# HTTP and UDP transports included in build

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${STREAM_SRCS} HEADERS ${STREAM_HDRS})

# Link dependencies as INTERFACE
# XKCP include directories are automatically propagated from dap_crypto via INTERFACE_INCLUDE_DIRECTORIES
# Use dap_link_libraries for automatic include propagation with cycle detection
# Note: dap_link_manager creates a cycle (dap_stream <-> dap_link_manager), but INTERFACE linking handles this
# Note: transport modules depend on dap_stream, but dap_stream needs transport headers
# for transport registration functions. This creates a circular dependency at link time,
# but it's resolved through INTERFACE linking - dap_stream only needs headers, not implementation.
dap_link_libraries(${PROJECT_NAME} INTERFACE 
    dap_core dap_io dap_crypto
    dap_http_server dap_http_common dap_enc_server dap_session dap_stream_ch dap_client dap_global_db dap_link_manager
    dap_transport_websocket dap_transport_http dap_transport_udp)

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)
# Add transport include directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../transport/websocket/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../transport/http/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../transport/udp/include
)
# Explicitly add stream_ch include directory (not propagated through dap_link_libraries)
# All other include directories should be propagated automatically via dap_link_libraries
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../ch/include
)
# All dependency include directories are automatically propagated via dap_link_libraries

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${STREAM_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/stream/stream/
        ARCHIVE DESTINATION lib/dap/net/stream/stream/
        PUBLIC_HEADER DESTINATION include/dap/net/stream/stream/
)
endif()