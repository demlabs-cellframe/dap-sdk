cmake_minimum_required(VERSION 3.10)
project(dap_enc_server C)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)
  
file(GLOB DAP_ENC_SERVER_SRCS FILES *.c)
file(GLOB DAP_ENC_SERVER_HDRS FILES include/*.h)

if(WIN32)
    add_definitions ("-DUNDEBUG")
    add_definitions ("-DNDEBUG")
    add_definitions ("-DWIN32")
    add_definitions ("-D_WINDOWS")
    add_definitions ("-D__WINDOWS__")
    add_definitions ("-D_CRT_SECURE_NO_WARNINGS")
endif()

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_ENC_SERVER_SRCS} HEADERS ${DAP_ENC_SERVER_HDRS})
target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)
# All dependency include directories (including dap_core with 3rdparty/uthash/src) are automatically propagated via dap_link_libraries

# Link dependencies as INTERFACE for OBJECT library
# System libraries (kernel32, etc.) will be linked at final stage
# Use dap_link_libraries for automatic include propagation with cycle detection
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_crypto dap_io dap_http_server dap_json-c)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_ENC_SERVER_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/enc_server/
        ARCHIVE DESTINATION lib/dap/net/server/enc_server/
        PUBLIC_HEADER DESTINATION include/dap/net/server/enc_server/
)
endif()
