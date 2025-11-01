cmake_minimum_required(VERSION 3.10)
project(dap_http_common)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

file(GLOB DAP_HTTP_COMMON_HEADERS include/*.h)
file(GLOB DAP_HTTP_COMMON_SOURCES src/*.c)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_HTTP_COMMON_SOURCES} HEADERS ${DAP_HTTP_COMMON_HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# All dependency include directories (including dap_core with 3rdparty/uthash/src) are automatically propagated via target_link_libraries

# Link dependencies as INTERFACE
target_link_libraries(${PROJECT_NAME} INTERFACE dap_core)

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${DAP_HTTP_COMMON_HEADERS}")
    INSTALL(TARGETS ${PROJECT_NAME} 
            LIBRARY DESTINATION lib/dap/net/common/http
            ARCHIVE DESTINATION lib/dap/net/common/http
            PUBLIC_HEADER DESTINATION include/dap/net/common/http/
    )
endif() 