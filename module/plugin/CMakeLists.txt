cmake_minimum_required(VERSION 3.10)
project(dap_plugin C)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE   ON)
set(CMAKE_C_STANDARD 11)

file(GLOB DAP_PLUGIN_SRCS src/*.c)
file(GLOB DAP_PLUGIN_HEADERS include/*.h)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LibraryHelpers.cmake)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${DAP_PLUGIN_SRCS} HEADERS ${DAP_PLUGIN_HEADERS})

# Store dependencies for final library - no linking of OBJECT targets
set(PLUGIN_LIBS "dap_cli_server")
if(NOT WIN32)
    list(APPEND PLUGIN_LIBS "dl")
endif()

set(DAP_PLUGIN_DEPS "${PLUGIN_LIBS}" CACHE INTERNAL "dap_plugin external dependencies")

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

dap_link_libraries(${PROJECT_NAME} INTERFACE dap_json_rpc dap_cli_server dap_http_server dap_io dap_json dap_hash_table dap_core dap_config dap_crypto dap_net_common)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_PLUGIN_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/plugin/
        ARCHIVE DESTINATION lib/dap/plugin/
        PUBLIC_HEADER DESTINATION include/dap/plugin/
)
endif()
