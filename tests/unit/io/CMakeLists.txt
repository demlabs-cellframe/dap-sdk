cmake_minimum_required(VERSION 3.10)

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

# Test: dap_thread
project(test_thread)

add_executable(${PROJECT_NAME} test_thread.c)

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60)

# Test: dap_thread_pool
project(test_thread_pool)

add_executable(${PROJECT_NAME} test_thread_pool.c)

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60)


