cmake_minimum_required(VERSION 3.10)

project (dap_core_unix)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

file(GLOB CORE_UNIX_SRCS *.c)
file(GLOB CORE_UNIX_HEADERS *.h)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${CORE_UNIX_SRCS} HEADERS ${CORE_UNIX_HEADERS})

# Link dap_core as interface (propagates includes)
target_link_libraries(${PROJECT_NAME} INTERFACE dap_core)
# pthread is a system library, will be linked at final stage
if (NOT ANDROID)
    # Store for later linking at final library stage
    set(CORE_UNIX_LIBRARIES pthread)
endif()

target_include_directories(dap_core_unix INTERFACE . ${CMAKE_CURRENT_SOURCE_DIR})
# Include directories from dap_core are needed for compilation
target_include_directories(dap_core_unix PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/json-c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/json-c
)

