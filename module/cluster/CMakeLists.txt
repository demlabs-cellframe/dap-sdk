cmake_minimum_required(VERSION 3.10)
project(dap_cluster_node C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LibraryHelpers.cmake)

# dap_cluster_node — base node address types, no stream dependency
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES
    dap_cluster_node.c HEADERS include/dap_cluster_node.h)
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_hash dap_crypto)
target_include_directories(${PROJECT_NAME} PUBLIC include)
