cmake_minimum_required(VERSION 3.10)
project(dap_plugin C)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE   ON)
set(CMAKE_C_STANDARD 11)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LibraryHelpers.cmake)

file(GLOB DAP_PLUGIN_SRCS src/*.c)
file(GLOB DAP_PLUGIN_HEADERS include/*.h)

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_PLUGIN_SRCS} HEADERS ${DAP_PLUGIN_HEADERS})

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include/)

if(NOT WIN32)
    dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_cli_server dl)
else()
    dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_cli_server)
endif()

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_PLUGIN_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib/dap/plugin/
        ARCHIVE DESTINATION lib/dap/plugin/
        PUBLIC_HEADER DESTINATION include/dap/plugin/
)
endif()
