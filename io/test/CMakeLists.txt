project(server_io_test)

# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework/mocks/DAPMockAutoWrap.cmake)

if ( NOT ( TARGET dap_test ) )
    add_subdirectory(libdap-test)
endif()

file(GLOB DAP_IO_TEST_SOURCES *.c)
file(GLOB DAP_IO_TEST_HEADERS *.h)

add_executable(${PROJECT_NAME} ${DAP_IO_TEST_SOURCES} ${DAP_IO_TEST_HEADERS})

target_link_libraries(${PROJECT_NAME} dap_test dap_core dap_crypto dap_io)

# Include mock framework headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework/mocks
)

# Apply mock autowrap
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME server_io_test
    COMMAND server_io_test
)
