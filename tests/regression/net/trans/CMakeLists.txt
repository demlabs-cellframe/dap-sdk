# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)
# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/dap_test_helpers.cmake)

# =======================================================================================
# UDP Multi-Client Regression Test
# Reproduces handshake timeout bug with multiple UDP clients
# STATUS: EXPECTED TO FAIL - demonstrates bug in UDP transport handshake
# =======================================================================================

project(test_udp_multiclient_regression)

add_executable(${PROJECT_NAME}
    test_udp_multiclient_regression.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../integration/net/trans/test_trans_helpers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_client_test_fixtures.c
)

# Link libraries - includes propagate automatically via PUBLIC/INTERFACE
dap_test_link_libraries(${PROJECT_NAME})

# Add standard test includes (test-framework, fixtures, etc.)
dap_test_add_includes(${PROJECT_NAME})

# Only need to add test_trans_helpers.h location (not part of any library)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../integration/net/trans
)

dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

# Mark as expected to fail - this test reproduces a known bug
set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "regression;net;trans;udp;EXPECTED_FAIL"
    WILL_FAIL TRUE
)

message("[+] UDP Multi-Client Regression Test - EXPECTED TO FAIL (reproduces handshake bug)")
