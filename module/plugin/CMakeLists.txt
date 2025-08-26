cmake_minimum_required(VERSION 3.10)
project(dap_plugin C)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE   ON)
set(CMAKE_C_STANDARD 11)

file(GLOB DAP_PLUGIN_SRCS src/*.c)
file(GLOB DAP_PLUGIN_HEADERS include/*.h)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${DAP_PLUGIN_SRCS} HEADERS ${DAP_PLUGIN_HEADERS})

# Store dependencies for final library - no linking of OBJECT targets
set(PLUGIN_LIBS "dap_cli_server")
if(NOT WIN32)
    list(APPEND PLUGIN_LIBS "dl")
endif()

set(DAP_PLUGIN_DEPS "${PLUGIN_LIBS}" CACHE INTERNAL "dap_plugin external dependencies")

target_include_directories(${PROJECT_NAME} PUBLIC 
    include/ 
    ../core/include/
    ../../3rdparty/uthash/src/
    ../net/server/cli_server/include/
    ../io/include/
    ../../3rdparty/json-c/
)

# Include paths for json_config.h - support both standalone and embedded builds  
# Removed hardcoded path - now handled via DAP_SDK_PATH_BUILD logic below
if(DEFINED DAP_SDK_PATH_BUILD)
    # When built as submodule (e.g., in python-cellframe)
    target_include_directories(${PROJECT_NAME} PUBLIC ${DAP_SDK_PATH_BUILD}/module/core/json-c)
    message("[+] DAP Plugin: Using external build path for json-c: ${DAP_SDK_PATH_BUILD}/module/core/json-c")
else()
    # Fallback paths for standalone builds
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_BINARY_DIR}/module/core/json-c)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_BINARY_DIR}/dap-sdk/module/core/json-c)
endif()

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_PLUGIN_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/plugin/
        ARCHIVE DESTINATION lib/dap/plugin/
        PUBLIC_HEADER DESTINATION include/dap/plugin/
)
endif()
