project(dap_stream_test C)

file(GLOB SOURCES *.c)
file(GLOB HEADERS *.h)

add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} dap_core dap_client dap_cli_server)
target_include_directories(${PROJECT_NAME} PUBLIC .)
