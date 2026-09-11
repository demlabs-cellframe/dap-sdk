cmake_minimum_required(VERSION 3.10)

# DAP SDK IO Unit Tests

# Include DAP Mock AutoWrap helper (confcall W59-R7.1: this directory was
# never add_subdirectory'd before, so dap_mock_autowrap() below was only
# ever resolved by accident when another subdir happened to include it
# first in the same configure run — include it explicitly).
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../module/test/mocks/DAPMockAutoWrap.cmake)

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

# ============================================================================
# Test: DAP Context
# ============================================================================
project(test_dap_context)

add_executable(${PROJECT_NAME}
    test_dap_context.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

# Apply mock autowrap
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT ${DAP_T300}
    LABELS "unit;io;context"
)

# ============================================================================
# Test: DAP Events
# ============================================================================
project(test_dap_events)

add_executable(${PROJECT_NAME}
    test_dap_events.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT ${DAP_T300}
    LABELS "unit;io;events"
)

# ============================================================================
# Test: DAP Events Socket
# ============================================================================
project(test_dap_events_socket)

add_executable(${PROJECT_NAME}
    test_dap_events_socket.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT ${DAP_T300}
    LABELS "unit;io;events;socket"
)

# ============================================================================
# Test: DAP Net
# ============================================================================
project(test_dap_net)

add_executable(${PROJECT_NAME}
    test_dap_net.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT ${DAP_T300}
    LABELS "unit;io;net"
)

# ============================================================================
# Test: DAP Proc Thread
# ============================================================================
project(test_dap_proc_thread)

add_executable(${PROJECT_NAME}
    test_dap_proc_thread.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT ${DAP_T300}
    LABELS "unit;io;proc_thread"
)

# ============================================================================
# Test: DAP Server
# ============================================================================
project(test_dap_server)

add_executable(${PROJECT_NAME}
    test_dap_server.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT ${DAP_T300}
    LABELS "unit;io;server"
)

# ============================================================================
# Test: DAP Timerfd (Unix-specific)
# ============================================================================
if(UNIX AND NOT APPLE)
    project(test_dap_timerfd)

    add_executable(${PROJECT_NAME}
        test_dap_timerfd.c
    )

    dap_test_add_includes(${PROJECT_NAME})
    dap_test_link_libraries(${PROJECT_NAME})
    dap_mock_autowrap(${PROJECT_NAME})

    add_test(
        NAME ${PROJECT_NAME}
        COMMAND ${PROJECT_NAME}
    )

    set_tests_properties(${PROJECT_NAME} PROPERTIES 
        TIMEOUT ${DAP_T300}
        LABELS "unit;io;timerfd"
    )
endif()

# ============================================================================
# Test: Proc-thread timer cancel/cancel_then regression (confcall W59-R7.3)
# ============================================================================
project(test_proc_thread_timer_cancel)

add_executable(${PROJECT_NAME}
    test_proc_thread_timer_cancel.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT ${DAP_T300}
    LABELS "unit;io;proc_thread;timer;regression"
)

# ============================================================================
# Test: Lifecycle race harness (confcall W59-R7.2)
# ============================================================================
project(test_lifecycle_race)

add_executable(${PROJECT_NAME}
    test_lifecycle_race.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT ${DAP_T300}
    LABELS "unit;io;lifecycle;race"
)

# ============================================================================
# Test: DAP Worker
# ============================================================================
project(test_dap_worker)

add_executable(${PROJECT_NAME}
    test_dap_worker.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT ${DAP_T300}
    LABELS "unit;io;worker"
)

message(STATUS "[TESTS] Added IO unit tests")

