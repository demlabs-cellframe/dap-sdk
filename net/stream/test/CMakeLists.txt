project(dap_stream_test C)

# Include DAP Mock Framework
include(${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks/DAPMockAutoWrap.cmake)

file(GLOB SOURCES *.c)
file(GLOB HEADERS *.h)

add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} dap_core dap_client dap_cli_server dap_test_mocks)
target_include_directories(${PROJECT_NAME} PUBLIC . ${CMAKE_SOURCE_DIR}/dap-sdk/test-framework/mocks)
