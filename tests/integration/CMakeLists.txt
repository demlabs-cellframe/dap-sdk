cmake_minimum_required(VERSION 3.10)

# DAP SDK Integration Tests
project(dap_sdk_integration_tests)

# Enable testing
enable_testing()

# ============================================================================
# Network Module Integration Tests
# ============================================================================

# HTTP Client Integration Tests
add_subdirectory(net/client)

# HTTP Server Integration Tests
add_subdirectory(net/server)

# Stream Integration Tests
add_subdirectory(net/stream)

