cmake_minimum_required(VERSION 3.10)
project (dap_http_client C)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/LibraryHelpers.cmake)

file(GLOB HTTP_CLIENT_SRCS *.c)
file(GLOB HTTP_CLIENT_HDRS include/*.h)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${HTTP_CLIENT_SRCS} HEADERS ${HTTP_CLIENT_HDRS})

# Include directories
# PRIVATE for own compilation - needed for dap_http_client to compile its own sources
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # For dap_http_server.h
)
# INTERFACE for propagation to dependents - ensures dap_http_client.h is visible to modules that link dap_http_client
target_include_directories(${PROJECT_NAME} INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link with common HTTP module and other dependencies
# Use dap_link_libraries for automatic include propagation with cycle detection
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_http_common dap_io)

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${HTTP_CLIENT_HDRS}")
    INSTALL(TARGETS ${PROJECT_NAME} 
            LIBRARY DESTINATION lib/dap/net/server/http_server/http_client/
            ARCHIVE DESTINATION lib/dap/net/server/http_server/http_client/
            PUBLIC_HEADER DESTINATION include/dap/net/server/http_server/http_client/
    )
endif()

