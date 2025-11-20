cmake_minimum_required(VERSION 3.10)
project (dap_http_server C)

file(GLOB HTTP_SERVER_SRCS FILES *.c )
file(GLOB HTTP_SERVER_HDRS FILES include/*.h)


# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

# Add http_client as subdirectory (creates dap_http_client OBJECT library)
add_subdirectory(http_client)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${HTTP_SERVER_SRCS} HEADERS ${HTTP_SERVER_HDRS})
# Include directories - use absolute paths for proper propagation
# PRIVATE for own compilation - needed for dap_http_server to compile its own sources
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# INTERFACE for propagation to dependents - ensures dap_http_server.h is visible to modules that link dap_http_server
# Use absolute paths to ensure they work from build directory (required for proper propagation)
get_filename_component(DAP_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../ ABSOLUTE)
target_include_directories(${PROJECT_NAME} INTERFACE 
    ${DAP_SDK_ROOT}/net/server/http_server
    ${DAP_SDK_ROOT}/net/server/http_server/include
)
# All dependency include directories (including dap_core with 3rdparty/uthash/src) are automatically propagated via dap_link_libraries



# Link dependencies as INTERFACE for OBJECT library
# System libraries (iconv, kernel32, etc.) will be linked at final stage
# Use dap_link_libraries for automatic include propagation with cycle detection
# dap_crypto is needed because dap_http_ban_list_client.h includes dap_hash.h which needs KeccakHash.h
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_crypto dap_json-c dap_io dap_http_common dap_http_client )

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${HTTP_SERVER_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/http_server/
        ARCHIVE DESTINATION lib/dap/net/server/http_server/
        PUBLIC_HEADER DESTINATION include/dap/net/server/http_server/
)
endif()
