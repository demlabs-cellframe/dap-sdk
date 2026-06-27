cmake_minimum_required(VERSION 3.10)

message(STATUS "[crypto tests] Configuring crypto unit tests")

# Include DAP Mock AutoWrap helper (corrected path)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../module/test/mocks/DAPMockAutoWrap.cmake)

# ============================================================================
# Crypto Module Unit Tests
# ============================================================================

add_subdirectory(enc)
add_subdirectory(cert)
add_subdirectory(falcon)

if(TARGET dap_gpu)
    add_subdirectory(gpu)
endif()

# Chipmunk Ring unit tests
add_subdirectory(chipmunk_ring)

