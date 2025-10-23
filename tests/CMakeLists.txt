cmake_minimum_required(VERSION 3.10)
project(dap_sdk_tests C)

# Test configuration
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler options for tests
add_compile_options(-Wall -Wextra -Werror)

# Include directories for DAP SDK
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dap-sdk/module/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/io/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/net/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/global-db/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/utilities
)

# Enable testing
enable_testing()

message("[+] DAP SDK Test Infrastructure:")
message("[+] - Unit tests: ${CMAKE_CURRENT_SOURCE_DIR}/unit")
message("[+] - Integration tests: ${CMAKE_CURRENT_SOURCE_DIR}/integration") 
message("[+] - E2E tests: ${CMAKE_CURRENT_SOURCE_DIR}/e2e")
message("[+] - Functional tests: ${CMAKE_CURRENT_SOURCE_DIR}/functional")
message("[+] - Regression tests: ${CMAKE_CURRENT_SOURCE_DIR}/regression")
message("[+] - Performance tests: ${CMAKE_CURRENT_SOURCE_DIR}/performance")
message("[+] - Security tests: ${CMAKE_CURRENT_SOURCE_DIR}/security")
message("[+] - Fixtures: ${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Build test fixtures library
file(GLOB FIXTURES_SOURCES 
    fixtures/utilities/*.c
)

if(FIXTURES_SOURCES)
    add_library(dap_test_fixtures STATIC ${FIXTURES_SOURCES})
    target_link_libraries(dap_test_fixtures dap_sdk)
    target_include_directories(dap_test_fixtures PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/../../dap-sdk/module/core/include
        ${CMAKE_SOURCE_DIR}/module/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/utilities
    )
    message("[+] Test fixtures library created")
endif()

# Function to add test executable with common setup
function(add_dap_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    
    # Link with DAP SDK libraries
    target_link_libraries(${TEST_NAME} 
        dap_sdk
    )
    
    # Include DAP SDK headers
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/../../dap-sdk/module/core/include
        ${CMAKE_SOURCE_DIR}/module/core/include
        ${CMAKE_SOURCE_DIR}/module/crypto/include
        ${CMAKE_SOURCE_DIR}/module/io/include
        ${CMAKE_SOURCE_DIR}/module/global-db/include
        ${CMAKE_SOURCE_DIR}/3rdparty/uthash/src
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    )
    
    # Link with test fixtures if available
    if(TARGET dap_test_fixtures)
        target_link_libraries(${TEST_NAME} dap_test_fixtures)
    endif()
    
    # Add system libraries
    if(UNIX AND NOT ANDROID)
        target_link_libraries(${TEST_NAME} pthread m)
    endif()
    
    # Set test properties
    set_target_properties(${TEST_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    )
endfunction()

# Function to register test with CTest
function(register_dap_test TEST_NAME TEST_CATEGORY)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    
    # Set test labels for filtering
    set_tests_properties(${TEST_NAME} PROPERTIES
        LABELS "${TEST_CATEGORY}"
        TIMEOUT 300  # 5 minutes timeout
        
        # ✅ Fail fast on critical errors in test output
        FAIL_REGULAR_EXPRESSION "CRITICAL ERROR|FATAL|SEGMENTATION FAULT"
        
        # ✅ Explicit working directory
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    
    # Set environment variables for tests
    # Add LD_LIBRARY_PATH to find libdap_sdk.so
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "DAP_LOG_LEVEL=DEBUG;LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}:$ENV{LD_LIBRARY_PATH}"
    )
    
    # ✅ Set higher cost for slow tests (runs them first in parallel mode)
    if(TEST_CATEGORY MATCHES "performance|e2e")
        set_tests_properties(${TEST_NAME} PROPERTIES COST 100)
    endif()
endfunction()

# Build Unit Tests
message("[+] Configuring Unit Tests")

# Debug test for SDK initialization
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/debug_test_init.c)
    add_dap_test(test_debug_init debug_test_init.c)
    register_dap_test(test_debug_init "debug;core")
endif()

# Core module unit tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/core/dap_json/dap_json_tests.c)
    add_dap_test(test_unit_dap_json unit/core/dap_json/dap_json_tests.c)
    register_dap_test(test_unit_dap_json "unit;core")
endif()

# Crypto module unit tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/hash/test_sha3.c)
    add_dap_test(test_unit_crypto_sha3 unit/crypto/hash/test_sha3.c)
    register_dap_test(test_unit_crypto_sha3 "unit;crypto;hash")
endif()

# Build Integration Tests
message("[+] Configuring Integration Tests")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_crypto_network.c)
    add_dap_test(test_integration_crypto_network integration/test_crypto_network.c)
    register_dap_test(test_integration_crypto_network "integration;crypto;network")
endif()

# Build E2E Tests
message("[+] Configuring E2E Tests")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_complete_workflow.c)
    add_dap_test(test_e2e_complete_workflow e2e/test_complete_workflow.c)
    register_dap_test(test_e2e_complete_workflow "e2e;workflow")
    
    # E2E tests need longer timeout
    set_tests_properties(test_e2e_complete_workflow PROPERTIES TIMEOUT 600)
endif()

# Build Performance Tests
message("[+] Configuring Performance Tests")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/performance/test_crypto_benchmarks.c)
    add_dap_test(test_performance_crypto performance/test_crypto_benchmarks.c)
    register_dap_test(test_performance_crypto "performance;crypto;benchmark")
    
    # Performance tests need longer timeout
    set_tests_properties(test_performance_crypto PROPERTIES TIMEOUT 1200)
endif()

# Build Functional Tests
message("[+] Configuring Functional Tests")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/functional/test_api_endpoints.c)
    add_dap_test(test_functional_api functional/test_api_endpoints.c)
    register_dap_test(test_functional_api "functional;api")
endif()

# Build Regression Tests  
message("[+] Configuring Regression Tests")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/regression/test_known_issues.c)
    add_dap_test(test_regression_known_issues regression/test_known_issues.c)
    register_dap_test(test_regression_known_issues "regression;bugfix")
endif()

# Build Security Tests
message("[+] Configuring Security Tests")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/security/test_memory_safety.c)
    add_dap_test(test_security_memory security/test_memory_safety.c)
    register_dap_test(test_security_memory "security;memory")
endif()

# Custom test targets for different categories
add_custom_target(test_unit
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "unit"
    COMMENT "Running unit tests"
)

add_custom_target(test_integration
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "integration"
    COMMENT "Running integration tests"
)

add_custom_target(test_e2e
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "e2e"
    COMMENT "Running E2E tests"
)

add_custom_target(test_performance
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "performance"
    COMMENT "Running performance tests"
)

add_custom_target(test_functional
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "functional"
    COMMENT "Running functional tests"
)

add_custom_target(test_regression
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "regression"
    COMMENT "Running regression tests"
)

add_custom_target(test_security
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "security"
    COMMENT "Running security tests"
)

add_custom_target(test_crypto
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L "crypto"
    COMMENT "Running crypto-related tests"
)

add_custom_target(test_all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all tests"
)

# Test discovery for dynamic test addition
file(GLOB_RECURSE UNIT_TEST_FILES "unit/**/*.c")
file(GLOB_RECURSE INTEGRATION_TEST_FILES "integration/*.c")
file(GLOB_RECURSE E2E_TEST_FILES "e2e/*.c")
file(GLOB_RECURSE PERFORMANCE_TEST_FILES "performance/*.c")
file(GLOB_RECURSE FUNCTIONAL_TEST_FILES "functional/*.c")
file(GLOB_RECURSE REGRESSION_TEST_FILES "regression/*.c")
file(GLOB_RECURSE SECURITY_TEST_FILES "security/*.c")

# Report test configuration
list(LENGTH UNIT_TEST_FILES UNIT_COUNT)
list(LENGTH INTEGRATION_TEST_FILES INTEGRATION_COUNT)
list(LENGTH E2E_TEST_FILES E2E_COUNT)
list(LENGTH PERFORMANCE_TEST_FILES PERFORMANCE_COUNT)
list(LENGTH FUNCTIONAL_TEST_FILES FUNCTIONAL_COUNT)
list(LENGTH REGRESSION_TEST_FILES REGRESSION_COUNT)
list(LENGTH SECURITY_TEST_FILES SECURITY_COUNT)

message("[+] Test Discovery Results:")
message("[+] - Unit test files found: ${UNIT_COUNT}")
message("[+] - Integration test files found: ${INTEGRATION_COUNT}")
message("[+] - E2E test files found: ${E2E_COUNT}")
message("[+] - Performance test files found: ${PERFORMANCE_COUNT}")
message("[+] - Functional test files found: ${FUNCTIONAL_COUNT}")
message("[+] - Regression test files found: ${REGRESSION_COUNT}")
message("[+] - Security test files found: ${SECURITY_COUNT}")

message("[+] Test targets available:")
message("[+] - make test_unit        : Run only unit tests")
message("[+] - make test_integration : Run only integration tests")
message("[+] - make test_e2e         : Run only E2E tests")
message("[+] - make test_performance : Run only performance tests")
message("[+] - make test_functional  : Run only functional tests")
message("[+] - make test_regression  : Run only regression tests")
message("[+] - make test_security    : Run only security tests")
message("[+] - make test_crypto      : Run all crypto-related tests")
message("[+] - make test_all         : Run all tests")
message("[+] - ctest                 : Run all tests with CTest")
message("[+] - ctest -L unit         : Run tests with 'unit' label")

message("[+] DAP SDK Test configuration completed successfully")