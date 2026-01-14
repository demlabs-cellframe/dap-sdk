cmake_minimum_required(VERSION 3.10)

# DAP SDK Integration Tests
project(dap_sdk_integration_tests)

# Enable testing
enable_testing()

# ============================================================================
# IO Layer Integration Tests
# ============================================================================

# Thread Pool Integration Tests
add_subdirectory(io)

# ============================================================================
# Network Module Integration Tests
# ============================================================================

# Network tests (UDP KEM scaling, HTTP, Stream, Transport)
add_subdirectory(net)

# Global DB Integration Tests
add_subdirectory(global-db)

