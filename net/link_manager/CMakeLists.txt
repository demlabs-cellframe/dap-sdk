cmake_minimum_required(VERSION 3.10)
project(dap_link_manager)
add_definitions ("-D_GNU_SOURCE")

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LibraryHelpers.cmake)

file(GLOB DAP_LINK_MANAGER_SOURCES FILES *.c)
file(GLOB DAP_LINK_MANAGER_HEADERS FILES include/*.h)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_LINK_MANAGER_SOURCES} HEADERS ${DAP_LINK_MANAGER_HEADERS})

# Link dependencies as INTERFACE
# Use dap_link_libraries for automatic include propagation with cycle detection
# dap_client is needed for dap_client.h header (used in dap_link_manager.h)
if(DAPSDK_MODULES MATCHES "ssl-support")
    dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_crypto dap_io dap_http_server dap_enc_server dap_stream dap_session dap_stream_ch dap_client dap_json-c wolfssl)
else()
    dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_crypto dap_io dap_http_server dap_enc_server dap_stream dap_session dap_stream_ch dap_client dap_json-c)
endif()
# rt is a system library, will be linked at final stage
if(UNIX AND NOT ANDROID AND NOT DARWIN)
    # Store for later linking at final library stage
    set(DAP_LINK_MANAGER_LIBRARIES rt)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE .)
# Explicitly add dap_client include directory for dap_client.h
# This is needed because dap_link_libraries may not propagate includes correctly in all cases
# Use absolute paths resolved from source directory to ensure they work from build directory
get_filename_component(DAP_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../ ABSOLUTE)
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${DAP_SDK_ROOT}/net/client/include
)
# All dependency include directories (including dap_core with 3rdparty/uthash/src) are automatically propagated via dap_link_libraries

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_LINK_MANAGER_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/link_manager/
        ARCHIVE DESTINATION lib/dap/net/link_manager/
        PUBLIC_HEADER DESTINATION include/dap/net/link_manager/
)
endif()