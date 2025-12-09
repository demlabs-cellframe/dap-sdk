cmake_minimum_required(VERSION 3.10)

# DAP SDK Integration Tests

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/dap_test_helpers.cmake)

# ============================================================================
# Test: Crypto Network Integration
# ============================================================================
project(test_crypto_network)

add_executable(${PROJECT_NAME}
    test_crypto_network.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 600
    LABELS "integration;crypto;network"
)

# ============================================================================
# Test: Crypto Network Clean
# ============================================================================
project(test_crypto_network_clean)

add_executable(${PROJECT_NAME}
    test_crypto_network_clean.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 600
    LABELS "integration;crypto;network;cleanup"
)

# ============================================================================
# Global DB Integration Tests
# ============================================================================
add_subdirectory(global-db)

# ============================================================================
# IO Integration Tests
# ============================================================================
# Test: Network Monitor
project(test_network_monitor)

add_executable(${PROJECT_NAME}
    io/test_network_monitor.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 600
    LABELS "integration;io;network;monitor"
)

# Test: Traffic Track
project(test_traffic_track)

add_executable(${PROJECT_NAME}
    io/test_traffic_track.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 600
    LABELS "integration;io;network;traffic"
)

message(STATUS "[TESTS] Added integration tests")

