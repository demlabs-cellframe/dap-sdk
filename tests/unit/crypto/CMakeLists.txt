cmake_minimum_required(VERSION 3.10)

# Include DAP Mock AutoWrap helper (corrected path)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../module/test/mocks/DAPMockAutoWrap.cmake)

# ============================================================================
# Crypto Module Unit Tests
# ============================================================================

add_subdirectory(enc)
add_subdirectory(cert)
add_subdirectory(falcon)


