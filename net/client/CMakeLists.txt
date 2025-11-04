cmake_minimum_required(VERSION 3.10)
project(dap_client)
add_definitions ("-D_GNU_SOURCE")

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LibraryHelpers.cmake)

file(GLOB DAP_CLIENT_SOURCES FILES *.c)
file(GLOB DAP_CLIENT_HEADERS FILES include/*.h)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_CLIENT_SOURCES} HEADERS ${DAP_CLIENT_HEADERS})

# Link dependencies as INTERFACE (propagates includes, but doesn't link yet)
# Use dap_link_libraries for automatic include propagation with cycle detection
dap_link_libraries(${PROJECT_NAME} INTERFACE 
    dap_core dap_crypto dap_io dap_enc_server dap_stream dap_session dap_json-c dap_http_common dap_net_transport)
if(DAPSDK_MODULES MATCHES "ssl-support")
    target_link_libraries(${PROJECT_NAME} INTERFACE wolfssl)
endif()
# rt is a system library, will be linked at final stage
if(UNIX AND NOT ANDROID AND NOT DARWIN)
    # Store for later linking at final library stage
    set(DAP_CLIENT_LIBRARIES rt)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE .)
# All dependency include directories (including dap_core with 3rdparty/uthash/src) are automatically propagated via dap_link_libraries

# Tests moved to ../../tests/unit/net/client/
# if (${BUILD_DAP_TESTS} MATCHES ON)
#     enable_testing()
#     add_subdirectory(test)
# endif()

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_CLIENT_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/client/
        ARCHIVE DESTINATION lib/dap/net/client/
        PUBLIC_HEADER DESTINATION include/dap/net/client/
)
endif()
