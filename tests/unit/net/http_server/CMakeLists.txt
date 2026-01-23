cmake_minimum_required(VERSION 3.10)

project(http_server_test)

file(GLOB SRC *.h *.c)

add_executable(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME} dap_test dap_core dap_http_server dap_crypto dap_json)

# Platform-specific system libraries
if(UNIX AND NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)
