project(dap_client_test)

# Include DAP Mock AutoWrap helper
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(${PROJECT_NAME} 
    test_http_client.c
    test_http_client_mocks.c
    test_http_client_mocks.h
)

target_link_libraries(${PROJECT_NAME} dap_core dap_client dap_test dap_io dap_http_common)

# Include test framework headers (includes dap_mock)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Automatic linker wrapping - scans all target sources for DAP_MOCK_DECLARE
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

add_test(
    NAME dap_client_test
    COMMAND dap_client_test
)
