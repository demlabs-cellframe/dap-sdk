cmake_minimum_required(VERSION 3.10)

# Test: dap_thread
project(test_thread)

add_executable(${PROJECT_NAME} 
    test_thread.c
    $<TARGET_OBJECTS:dap_io>
    $<TARGET_OBJECTS:dap_core>
    $<TARGET_OBJECTS:dap_test>
)

# Link pthread
target_link_libraries(${PROJECT_NAME} PRIVATE pthread)

# Add includes
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/io/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/test-framework
)

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60)

# Test: dap_thread_pool
project(test_thread_pool)

add_executable(${PROJECT_NAME} 
    test_thread_pool.c
    $<TARGET_OBJECTS:dap_io>
    $<TARGET_OBJECTS:dap_core>
    $<TARGET_OBJECTS:dap_test>
)

# Link pthread
target_link_libraries(${PROJECT_NAME} PRIVATE pthread)

# Add includes
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/io/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/test-framework
)

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60)




