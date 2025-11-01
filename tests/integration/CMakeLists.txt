cmake_minimum_required(VERSION 3.10)

# DAP SDK Integration Tests
project(dap_sdk_integration_tests)

# Enable testing
enable_testing()

# ============================================================================
# Network Module Integration Tests
# ============================================================================

# HTTP Integration Tests (Client + Server)
add_subdirectory(net/http)

# Stream Integration Tests
add_subdirectory(net/stream)

# Transport Integration Tests (HTTP, WebSocket, UDP)
add_subdirectory(net/transport)

# Global DB Integration Tests
add_subdirectory(global-db)

