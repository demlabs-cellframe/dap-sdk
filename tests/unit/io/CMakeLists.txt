cmake_minimum_required(VERSION 3.10)

# Include test framework helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../test-framework/mocks/DAPMockAutoWrap.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

# Common include directories for IO tests
set(IO_TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/io/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/test-framework
    ${CMAKE_SOURCE_DIR}/3rdparty/uthash/src
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
)

if(WIN32)
    list(APPEND IO_TEST_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/core/include/win32)
else()
    list(APPEND IO_TEST_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/core/include/unix)
endif()

# Linux-specific include directories
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND IO_TEST_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/io/linux)
endif()

# ============================================================================
# Test Fixtures Library (shared code for tier tests)
# ============================================================================
project(io_flow_test_fixtures)

add_library(${PROJECT_NAME} STATIC
    fixtures/dap_io_flow_test_fixtures.c
    fixtures/dap_io_flow_test_fixtures.h
)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})

# Include directories
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC 
    ${IO_TEST_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
)

# Link libcap for capability checking (optional)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(LIBCAP_LIBRARY cap)
    if(LIBCAP_LIBRARY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBCAP_LIBRARY})
        target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_LIBCAP=1)
    endif()
endif()

# Enable automatic mock wrapping for fixtures
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# ============================================================================
# Test: IO Flow Tier Detection (cross-platform)
# ============================================================================
project(test_io_flow_tiers)

add_executable(${PROJECT_NAME} 
    test_io_flow_tiers.c
)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${IO_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 120
    LABELS "unit;io;flow;tier"
)

# ============================================================================
# Test: Thread Pool Unit Test
# ============================================================================
project(test_thread_pool)

add_executable(${PROJECT_NAME}
    test_thread_pool.c
)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${IO_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "unit;io;thread_pool"
)

# ============================================================================
# Test: Thread Unit Test
# ============================================================================
project(test_thread)

add_executable(${PROJECT_NAME}
    test_thread.c
)

# Link dependencies
dap_test_link_libraries(${PROJECT_NAME})
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${IO_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "unit;io;thread"
)

# ============================================================================
# Test: Application Tier (all platforms)
# ============================================================================
project(test_io_flow_tier_application)

add_executable(${PROJECT_NAME}
    test_io_flow_tier_application.c
)

# Link fixtures and dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE io_flow_test_fixtures)
dap_test_link_libraries(${PROJECT_NAME})
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${IO_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "unit;io;flow;tier;application"
)

# ============================================================================
# Test: Classic BPF Tier (Linux only)
# Tests dap_io_flow_cbpf_* API functions
# ============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    project(test_io_flow_tier_cbpf)
    
    add_executable(${PROJECT_NAME}
        test_io_flow_tier_cbpf.c
    )
    
    # Link fixtures and dependencies
    # Note: dap_io is linked via dap_test_link_libraries which includes it
    target_link_libraries(${PROJECT_NAME} PRIVATE io_flow_test_fixtures)
    dap_test_link_libraries(${PROJECT_NAME})
    dap_test_add_includes(${PROJECT_NAME})
    target_include_directories(${PROJECT_NAME} PRIVATE ${IO_TEST_INCLUDE_DIRS})
    
    # Enable automatic mock wrapping
    dap_mock_autowrap(${PROJECT_NAME})
    
    if(NOT ANDROID)
        target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
    endif()
    
    # Add to CTest
    add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
    set_tests_properties(${PROJECT_NAME} PROPERTIES
        TIMEOUT 120
        LABELS "unit;io;flow;tier;cbpf;linux"
    )
    
    message("[+] Classic BPF tier test (Linux) - tests dap_io_flow_cbpf_* API")
endif()

# ============================================================================
# Test: eBPF Tier (Linux only)
# Tests dap_io_flow_ebpf_* API functions
# ============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    project(test_io_flow_tier_ebpf)
    
    add_executable(${PROJECT_NAME}
        test_io_flow_tier_ebpf.c
    )
    
    # Link fixtures and dependencies
    # Note: dap_io is linked via dap_test_link_libraries which includes it
    target_link_libraries(${PROJECT_NAME} PRIVATE io_flow_test_fixtures)
    dap_test_link_libraries(${PROJECT_NAME})
    dap_test_add_includes(${PROJECT_NAME})
    target_include_directories(${PROJECT_NAME} PRIVATE ${IO_TEST_INCLUDE_DIRS})
    
    # Link libcap for capability checking (optional)
    if(LIBCAP_LIBRARY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBCAP_LIBRARY})
        target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_LIBCAP=1)
    endif()
    
    # Enable automatic mock wrapping
    dap_mock_autowrap(${PROJECT_NAME})
    
    if(NOT ANDROID)
        target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
    endif()
    
    # Add to CTest
    add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
    set_tests_properties(${PROJECT_NAME} PROPERTIES
        TIMEOUT 120
        LABELS "unit;io;flow;tier;ebpf;linux"
        SKIP_RETURN_CODE 77
    )
    
    message("[+] eBPF tier test (Linux) - tests dap_io_flow_ebpf_* API")
endif()

# ============================================================================
# Test: IO Flow Control Unit Test (REGRESSION)
# Tests dap_io_flow_ctrl - retransmission and ACK handling
# ============================================================================
project(test_io_flow_ctrl)

add_executable(${PROJECT_NAME}
    test_io_flow_ctrl.c
)

# Link fixtures and dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE io_flow_test_fixtures)
dap_test_link_libraries(${PROJECT_NAME})
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${IO_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "unit;io;flow;ctrl;regression"
)

message("[+] IO Flow Control test - regression test for retransmission bug")

# ============================================================================
# Test: IO Flow Multi-Client Regression Test
# Tests packet routing, ordering, and client isolation
# ============================================================================
project(test_io_flow_multiclient)

add_executable(${PROJECT_NAME}
    test_io_flow_multiclient.c
)

# Link fixtures and dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE io_flow_test_fixtures)
dap_test_link_libraries(${PROJECT_NAME})
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${IO_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "unit;io;flow;multiclient;regression"
)

message("[+] IO Flow Multi-Client test - packet routing and ordering regression")

# ============================================================================
# Test: IO Flow Control Multi-Client Regression Test
# Tests flow_ctrl with multiple concurrent clients - reproduces integration bug
# ============================================================================
project(test_io_flow_ctrl_multiclient)

add_executable(${PROJECT_NAME}
    test_io_flow_ctrl_multiclient.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures/dap_client_test_fixtures.c
)

# Link fixtures and dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE io_flow_test_fixtures)
dap_test_link_libraries(${PROJECT_NAME})
dap_test_add_includes(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${IO_TEST_INCLUDE_DIRS})

# Enable automatic mock wrapping
dap_mock_autowrap(${PROJECT_NAME})

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# Add to CTest
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 300
    LABELS "unit;io;flow;ctrl;multiclient;regression"
)

message("[+] IO Flow Control Multi-Client test - integration bug regression")
