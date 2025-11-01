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
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_crypto dap_io dap_stream)

target_include_directories(dap_stream_ch INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)
# All dependency include directories (including dap_core with 3rdparty/uthash/src) are automatically propagated via dap_link_libraries

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_STREAM_CH_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/stream/ch/
        ARCHIVE DESTINATION lib/dap/net/stream/ch/
        PUBLIC_HEADER DESTINATION include/dap/net/stream/ch/
)
endif()