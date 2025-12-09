cmake_minimum_required(VERSION 3.10)

# DAP SDK Security Tests

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/dap_test_helpers.cmake)

# ============================================================================
# Test: Memory Safety
# ============================================================================
project(test_memory_safety)

add_executable(${PROJECT_NAME}
    test_memory_safety.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 1200
    LABELS "security;memory;valgrind"
)

# Add valgrind test variant if valgrind is available
find_program(VALGRIND_EXECUTABLE valgrind)
if(VALGRIND_EXECUTABLE)
    add_test(
        NAME ${PROJECT_NAME}_valgrind
        COMMAND ${VALGRIND_EXECUTABLE} 
            --leak-check=full 
            --show-leak-kinds=all 
            --track-origins=yes 
            --error-exitcode=1
            $<TARGET_FILE:${PROJECT_NAME}>
    )
    
    set_tests_properties(${PROJECT_NAME}_valgrind PROPERTIES 
        TIMEOUT 2400
        LABELS "security;memory;valgrind;slow"
    )
    
    message(STATUS "[TESTS] Added valgrind variant for ${PROJECT_NAME}")
endif()

message(STATUS "[TESTS] Added security tests")

