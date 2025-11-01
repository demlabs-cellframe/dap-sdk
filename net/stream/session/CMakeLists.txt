cmake_minimum_required(VERSION 3.10)
project (dap_session)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
  
file(GLOB SESSION_SRCS *.c)
file(GLOB SESSION_HDRS include/*.h)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${SESSION_SRCS} HEADERS ${SESSION_HDRS})

include_directories(include/)
# Link dependencies as INTERFACE
# Use dap_link_libraries for automatic include propagation with cycle detection
# XKCP include directories are automatically propagated from dap_crypto via INTERFACE_INCLUDE_DIRECTORIES
# 3rdparty/uthash/src is automatically propagated from dap_core via INTERFACE_INCLUDE_DIRECTORIES
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_crypto)
target_include_directories(dap_session INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)
# All dependency include directories (including dap_core with 3rdparty/uthash/src) are automatically propagated via dap_link_libraries

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${SESSION_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/stream/session/
        ARCHIVE DESTINATION lib/dap/net/stream/session/
        PUBLIC_HEADER DESTINATION include/dap/net/stream/session/
)
endif()