cmake_minimum_required(VERSION 3.10)

# DAP SDK Unit Tests

# Include DAP Mock AutoWrap helper
include(${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework/mocks/DAPMockAutoWrap.cmake)

# Include test helpers (must be after parent CMakeLists.txt is processed)
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/dap_test_helpers.cmake)

# ============================================================================
# Test: Stream Transport Layer
# ============================================================================
project(test_stream_transport)

add_executable(${PROJECT_NAME}
    test_stream_transport.c
)

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Automatic linker wrapping
dap_mock_autowrap(${PROJECT_NAME})

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test timeout (5 minutes)
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 300)

# ============================================================================
# Test: Stream Obfuscation Layer
# ============================================================================
project(test_stream_obfuscation)

add_executable(${PROJECT_NAME}
    test_stream_obfuscation.c
)

# Link all DAP SDK libraries using helper function
dap_test_link_libraries(${PROJECT_NAME})

# Add all DAP SDK includes using helper function
dap_test_add_includes(${PROJECT_NAME})

# Automatic linker wrapping
dap_mock_autowrap(${PROJECT_NAME})

# Add to CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Set test timeout (5 minutes)
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 300)



# ============================================================================
# Core Module Unit Tests
# ============================================================================
add_subdirectory(core)

# ============================================================================
# Network Module Unit Tests
# ============================================================================
add_subdirectory(net/client)
add_subdirectory(net/stream)
add_subdirectory(net/server)
add_subdirectory(net/transport)

# ============================================================================
# Crypto Module Unit Tests
# ============================================================================
add_subdirectory(crypto)
