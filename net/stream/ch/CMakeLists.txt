cmake_minimum_required(VERSION 3.10)
project (dap_stream_ch)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
  
file(GLOB DAP_STREAM_CH_SRCS *.c)
file(GLOB DAP_STREAM_CH_HDRS include/*.h)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_STREAM_CH_SRCS} HEADERS ${DAP_STREAM_CH_HDRS})

# Link dependencies as INTERFACE
# Use dap_link_libraries for automatic include propagation with cycle detection
# dap_io is needed for dap_events_socket functions used in dap_stream_ch_pkt.c
# dap_http_client is needed for dap_http_client.h header
# dap_http_server is also needed for other HTTP server functionality
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_crypto dap_io dap_stream dap_http_common dap_http_client dap_http_server )

target_include_directories(dap_stream_ch INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)
# Explicitly add http_server and http_client include directories
# This is needed because dap_stream.h includes dap_http_server.h, and dap_stream_ch.c includes dap_http_client.h
# dap_link_libraries should propagate these, but explicit addition ensures they are available
# Use absolute paths resolved from source directory to ensure they work from build directory
get_filename_component(DAP_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../ ABSOLUTE)
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${DAP_SDK_ROOT}/net/server/http_server/include
    ${DAP_SDK_ROOT}/net/server/http_server/http_client/include
)
# All dependency include directories (including dap_http_client/include) are automatically propagated via dap_link_libraries

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_STREAM_CH_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/stream/ch/
        ARCHIVE DESTINATION lib/dap/net/stream/ch/
        PUBLIC_HEADER DESTINATION include/dap/net/stream/ch/
)
endif()