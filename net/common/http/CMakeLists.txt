cmake_minimum_required(VERSION 3.10)
project(dap_http_common)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

file(GLOB DAP_HTTP_COMMON_HEADERS include/*.h)
file(GLOB DAP_HTTP_COMMON_SOURCES src/*.c)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_HTTP_COMMON_SOURCES} HEADERS ${DAP_HTTP_COMMON_HEADERS})

# PRIVATE for own compilation - needed for dap_http_common to compile its own sources
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# INTERFACE for propagation to dependents - ensures dap_http_header.h is visible to modules that link dap_http_common
# Use absolute paths to ensure they work from build directory (required for proper propagation)
get_filename_component(DAP_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../ ABSOLUTE)
target_include_directories(${PROJECT_NAME} INTERFACE 
    ${DAP_SDK_ROOT}/net/common/http/include
)

# Link dependencies as INTERFACE using dap_link_libraries for proper include propagation
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core)

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${DAP_HTTP_COMMON_HEADERS}")
    INSTALL(TARGETS ${PROJECT_NAME} 
            LIBRARY DESTINATION lib/dap/net/common/http
            ARCHIVE DESTINATION lib/dap/net/common/http
            PUBLIC_HEADER DESTINATION include/dap/net/common/http/
    )
endif() 