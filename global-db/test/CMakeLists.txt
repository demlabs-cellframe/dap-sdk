cmake_minimum_required(VERSION 3.4.1)

project(global_db_test)

# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework/mocks/DAPMockAutoWrap.cmake)

add_executable(${PROJECT_NAME} dap_global_db_test.c)

target_link_libraries(${PROJECT_NAME} dap_core dap_test dap_global_db)

# Include mock framework headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework/mocks
)

# Apply mock autowrap
dap_mock_autowrap(${PROJECT_NAME})

add_test(
		NAME global_db_test
		COMMAND global_db_test
)
