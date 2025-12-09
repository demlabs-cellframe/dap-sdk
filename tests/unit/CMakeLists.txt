cmake_minimum_required(VERSION 3.10)

# DAP SDK Unit Tests

# Include DAP Mock AutoWrap helper (corrected path to module/test/)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../module/test/mocks/DAPMockAutoWrap.cmake)

# Include test helpers (must be after parent CMakeLists.txt is processed)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/dap_test_helpers.cmake)

# ============================================================================
# Core Module Unit Tests
# ============================================================================
add_subdirectory(core)

# ============================================================================
# IO Module Unit Tests
# ============================================================================
add_subdirectory(io)

# ============================================================================
# Network Module Unit Tests
# ============================================================================
add_subdirectory(net/client)
add_subdirectory(net/server)

# ============================================================================
# Crypto Module Unit Tests
# ============================================================================
add_subdirectory(crypto)

# ============================================================================
# Test Framework Unit Tests
# ============================================================================
add_subdirectory(test)
