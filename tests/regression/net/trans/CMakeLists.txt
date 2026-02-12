# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)
# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/dap_test_helpers.cmake)

# =======================================================================================
# UDP Multi-Client Regression Test
# Tests multi-client UDP handshake (previously reproduced handshake timeout bug)
# STATUS: FIXED - KEM callback now uses owner_worker_id instead of listener_es->worker
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

# Test properties
set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "regression;net;trans;udp"
)

message("[+] UDP Multi-Client Regression Test - tests multi-client handshake (bug FIXED)")

# =======================================================================================
# UDP Handshake Retransmission Test
# BUG: HANDSHAKE packets are sent via direct sendto() WITHOUT flow control retransmission.
# If HANDSHAKE is lost, there is NO automatic retransmit -> handshake timeout!
# STATUS: NOT FIXED - this test will FAIL until retransmission is implemented
# =======================================================================================

project(test_udp_handshake_retrans)

add_executable(${PROJECT_NAME}
    test_udp_handshake_retrans.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../integration/net/trans/test_trans_helpers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures/dap_client_test_fixtures.c
)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})

# Include directories
dap_test_add_includes(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../integration/net/trans
)

dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 30
    LABELS "regression;net;trans;udp;handshake"
)

message("[+] UDP Handshake Retransmission Test - verifies handshake retransmission works")
