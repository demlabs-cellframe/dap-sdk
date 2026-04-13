cmake_minimum_required(VERSION 3.10)
project(dap_sdk_tests C)

# Test configuration
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Warning flags inherited from cmake/OS_Detection.cmake (CFLAGS_WARNINGS via CMAKE_C_FLAGS)

# Include directories for DAP SDK
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/crypto/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/io/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/net/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/global-db/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/test
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/utilities
)

# Enable testing
enable_testing()

# Auto-detect CPU count for parallel test execution
include(ProcessorCount)
ProcessorCount(DAP_NPROC)
if(NOT DAP_NPROC OR DAP_NPROC EQUAL 0)
    set(DAP_NPROC 4)
endif()

# MinGW tests run under Wine in CI: much slower per test; avoid dozens of concurrent Wine VMs.
# ThreadSanitizer: tests run 5-15x slower due to instrumentation overhead.
set(DAP_CTEST_TIMEOUT_SCALE 1)
set(DAP_CTEST_PARALLEL ${DAP_NPROC})
if(CMAKE_CROSSCOMPILING AND WIN32)
    set(DAP_CTEST_TIMEOUT_SCALE 12)
    set(DAP_CTEST_PARALLEL 4)
elseif(DAP_SANITIZE_THREAD)
    set(DAP_CTEST_TIMEOUT_SCALE 10)
    message(STATUS "[Tests] ThreadSanitizer: timeout scale ${DAP_CTEST_TIMEOUT_SCALE}")
elseif(DAP_SANITIZE_ADDRESS)
    set(DAP_CTEST_TIMEOUT_SCALE 3)
    message(STATUS "[Tests] AddressSanitizer: timeout scale ${DAP_CTEST_TIMEOUT_SCALE}")
endif()
foreach(_base 10 30 60 120 300 600 1200)
    math(EXPR DAP_T${_base} "${_base} * ${DAP_CTEST_TIMEOUT_SCALE}")
endforeach()
set(DAP_T_PERF ${DAP_T1200})
if(DAP_T_PERF GREATER 10800)
    set(DAP_T_PERF 10800)
endif()
if(CMAKE_CROSSCOMPILING AND WIN32)
    message(STATUS "[Tests] MinGW+Wine: CTest timeout scale ${DAP_CTEST_TIMEOUT_SCALE}, parallel ${DAP_CTEST_PARALLEL}")
endif()

# `make test` and `cmake --build . --target test` run CTest in parallel
set(CMAKE_CTEST_ARGUMENTS "--output-on-failure" "--parallel" "${DAP_CTEST_PARALLEL}" CACHE STRING "" FORCE)

message("[+] DAP SDK Test Infrastructure:")
message("[+] - Unit tests: ${CMAKE_CURRENT_SOURCE_DIR}/unit")
message("[+] - Integration tests: ${CMAKE_CURRENT_SOURCE_DIR}/integration") 
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
    # dap_sdk propagates all include directories from modules
    target_link_libraries(dap_test_fixtures dap_sdk)
    target_include_directories(dap_test_fixtures PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/utilities
    )
    message("[+] Test fixtures library created")
endif()

# Function to add test executable with common setup
function(add_dap_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    
    # Link with DAP SDK libraries
    target_link_libraries(${TEST_NAME} 
        dap_sdk dap_test
    )
    
    # Include DAP SDK headers
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/module/core/include
        ${CMAKE_SOURCE_DIR}/module/crypto/include
        ${CMAKE_SOURCE_DIR}/module/crypto/src
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
        TIMEOUT ${DAP_T300}
        
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

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/core/test_dap_serialize.c)
    add_dap_test(test_unit_core_dap_serialize unit/core/test_dap_serialize.c)
    register_dap_test(test_unit_core_dap_serialize "unit;core;serialize")
endif()

# Core module unit tests (Arena, String Pool, etc.)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/core/CMakeLists.txt)
    add_subdirectory(unit/core)
    message("[+] Core module unit tests configured (Arena, etc.)")
endif()

# JSON module unit tests (Phase 0.2 + Phase 1.1)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/json/CMakeLists.txt)
    add_subdirectory(unit/json)
    message("[+] JSON module unit tests configured (Phase 0.2 + Phase 1.1)")
endif()

# GlobalDB module unit tests (B-tree, Storage, Migration)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/global-db/CMakeLists.txt)
    add_subdirectory(unit/global-db)
endif()

# Crypto module unit tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/hash/test_sha3.c)
    add_dap_test(test_unit_crypto_sha3 unit/crypto/hash/test_sha3.c)
    register_dap_test(test_unit_crypto_sha3 "unit;crypto;hash")
endif()

<<<<<<< HEAD
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/test_dap_hash.c)
    add_dap_test(test_unit_crypto_dap_hash unit/crypto/test_dap_hash.c)
    register_dap_test(test_unit_crypto_dap_hash "unit;crypto;hash")
endif()

# Chipmunk Ring tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_ring_basic.c)
    add_dap_test(test_unit_crypto_chipmunk_ring_basic unit/crypto/chipmunk_ring/test_chipmunk_ring_basic.c)
    register_dap_test(test_unit_crypto_chipmunk_ring_basic "unit;crypto;ring;basic")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_ring_anonymity.c)
    add_dap_test(test_unit_crypto_chipmunk_ring_anonymity unit/crypto/chipmunk_ring/test_chipmunk_ring_anonymity.c)
    register_dap_test(test_unit_crypto_chipmunk_ring_anonymity "unit;crypto;ring;anonymity;statistical")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_ring_performance.c)
    add_dap_test(test_unit_crypto_chipmunk_ring_performance unit/crypto/chipmunk_ring/test_chipmunk_ring_performance.c)
    register_dap_test(test_unit_crypto_chipmunk_ring_performance "unit;crypto;ring;performance")
    
    # ChipmunkRing parameter comparison test
    add_dap_test(test_unit_crypto_chipmunk_ring_parameter_comparison unit/crypto/chipmunk_ring/test_chipmunk_ring_parameter_comparison.c)
    register_dap_test(test_unit_crypto_chipmunk_ring_parameter_comparison "unit;crypto;ring;parameters")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_ring_edge_cases.c)
    add_dap_test(test_unit_crypto_chipmunk_ring_edge_cases unit/crypto/chipmunk_ring/test_chipmunk_ring_edge_cases.c)
    register_dap_test(test_unit_crypto_chipmunk_ring_edge_cases "unit;crypto;ring;edge_cases")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_ring_stress.c)
    add_dap_test(test_unit_crypto_chipmunk_ring_stress unit/crypto/chipmunk_ring/test_chipmunk_ring_stress.c)
    register_dap_test(test_unit_crypto_chipmunk_ring_stress "unit;crypto;ring;stress")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_minimal_ring.c)
    add_dap_test(test_minimal_ring unit/crypto/chipmunk_ring/test_minimal_ring.c)
    register_dap_test(test_minimal_ring "unit;crypto;ring;minimal;debug")
endif()

# New comprehensive multi-signer tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_ring_multi_signer.c)
    add_dap_test(test_unit_crypto_chipmunk_ring_multi_signer unit/crypto/chipmunk_ring/test_chipmunk_ring_multi_signer.c)
    register_dap_test(test_unit_crypto_chipmunk_ring_multi_signer "unit;crypto;ring;multi_signer;threshold")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_ring_coordination.c)
    add_dap_test(test_unit_crypto_chipmunk_ring_coordination unit/crypto/chipmunk_ring/test_chipmunk_ring_coordination.c)
    register_dap_test(test_unit_crypto_chipmunk_ring_coordination "unit;crypto;ring;coordination")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_ring_error_handling.c)
    add_dap_test(test_unit_crypto_chipmunk_ring_error_handling unit/crypto/chipmunk_ring/test_chipmunk_ring_error_handling.c)
    register_dap_test(test_unit_crypto_chipmunk_ring_error_handling "unit;crypto;ring;error_handling")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_ring_input_validation.c)
    add_dap_test(test_unit_crypto_chipmunk_ring_input_validation unit/crypto/chipmunk_ring/test_chipmunk_ring_input_validation.c)
    register_dap_test(test_unit_crypto_chipmunk_ring_input_validation "unit;crypto;ring;input_validation")
endif()


if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/base58/test_base58_encoding.c)
    add_dap_test(test_unit_crypto_base58 unit/crypto/base58/test_base58_encoding.c)
    register_dap_test(test_unit_crypto_base58 "unit;crypto;encoding;base58")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/base64/test_base64_encoding.c)
    add_dap_test(test_unit_crypto_base64 unit/crypto/base64/test_base64_encoding.c)
    register_dap_test(test_unit_crypto_base64 "unit;crypto;encoding;base64")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/signatures/test_signatures.c)
    add_dap_test(test_unit_crypto_signatures unit/crypto/signatures/test_signatures.c)
    register_dap_test(test_unit_crypto_signatures "unit;crypto;signatures")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/general/test_encryption.c)
    add_dap_test(test_unit_crypto_encryption unit/crypto/general/test_encryption.c)
    register_dap_test(test_unit_crypto_encryption "unit;crypto;encryption")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/ntt/test_ntt_comparison.c)
    add_dap_test(test_unit_crypto_ntt unit/crypto/ntt/test_ntt_comparison.c)
    register_dap_test(test_unit_crypto_ntt "unit;crypto;ntt")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/tree/test_chipmunk_tree.c)
    add_dap_test(test_unit_crypto_tree unit/crypto/tree/test_chipmunk_tree.c)
    register_dap_test(test_unit_crypto_tree "unit;crypto;tree")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_performance.c)
    add_dap_test(test_unit_crypto_chipmunk_performance unit/crypto/chipmunk/test_chipmunk_performance.c)
    register_dap_test(test_unit_crypto_chipmunk_performance "unit;crypto;chipmunk")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_hots.c)
    add_dap_test(test_unit_crypto_chipmunk_hots unit/crypto/chipmunk/test_chipmunk_hots.c)
    register_dap_test(test_unit_crypto_chipmunk_hots "unit;crypto;chipmunk")

    # Extended HOTS test for ring signature context debugging
    add_dap_test(test_unit_crypto_hots_extended unit/crypto/chipmunk_ring/test_hots_extended.c)
    register_dap_test(test_unit_crypto_hots_extended "unit;crypto;hots;extended")
endif()

#if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_large_scale.c)
#    add_dap_test(test_unit_crypto_chipmunk_large_scale unit/crypto/chipmunk/test_chipmunk_large_scale.c)
#    register_dap_test(test_unit_crypto_chipmunk_large_scale "unit;crypto;chipmunk")
#endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_invntt_fix.c)
    add_dap_test(test_unit_crypto_chipmunk_invntt_fix unit/crypto/chipmunk/test_chipmunk_invntt_fix.c)
    register_dap_test(test_unit_crypto_chipmunk_invntt_fix "unit;crypto;chipmunk")
endif()

# Keccak/SHA3 Known Answer Tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/keccak/test_sha3_kat.c)
    add_dap_test(test_unit_crypto_sha3_kat unit/crypto/keccak/test_sha3_kat.c)
    register_dap_test(test_unit_crypto_sha3_kat "unit;crypto;sha3;keccak")
endif()

# Keccak x4 (parallel SHAKE) correctness + benchmark
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/keccak/test_keccak_x4.c)
    add_dap_test(test_keccak_x4 unit/crypto/keccak/test_keccak_x4.c)
    register_dap_test(test_keccak_x4 "unit;crypto;keccak;x4")
endif()

# ECDSA Field Operations Tests
if(BUILD_WITH_ECDSA AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/test_field_mul.c)
    add_dap_test(test_field_mul unit/crypto/test_field_mul.c)
    target_include_directories(test_field_mul PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/../module/crypto/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../module/crypto/src/sig/classic/ecdsa
        ${CMAKE_CURRENT_SOURCE_DIR}/../module/crypto/src/sig/classic/ecdsa/arch
    )
    register_dap_test(test_field_mul "unit;crypto;ecdsa")
endif()

if(BUILD_WITH_ECDSA AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/test_ecdsa_debug.c)
    add_dap_test(test_ecdsa_debug unit/crypto/test_ecdsa_debug.c)
    target_include_directories(test_ecdsa_debug PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/../module/crypto/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../module/crypto/src/sig/classic/ecdsa
        ${CMAKE_CURRENT_SOURCE_DIR}/../module/crypto/src/sig/classic/ecdsa/arch
    )
    register_dap_test(test_ecdsa_debug "unit;crypto;ecdsa")
endif()


# test framework async unit tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test/test_async.c)
    add_dap_test(test_unit_dap_test_async unit/test/test_async.c)
    register_dap_test(test_unit_dap_test_async "unit;test")
endif()

# test framework mock unit tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test/test_mock.c)
    add_dap_test(test_unit_dap_test_mock unit/test/test_mock.c)
    register_dap_test(test_unit_dap_test_mock "unit;test")
endif()

# Net module unit tests (link_manager hot list)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/net/test_link_manager_hot_list.c)
    add_dap_test(test_unit_net_link_manager_hot_list unit/net/test_link_manager_hot_list.c)
    register_dap_test(test_unit_net_link_manager_hot_list "unit;net;link_manager")
endif()

# Build Integration Tests
message("[+] Configuring Integration Tests")

#if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_crypto_network.c)
#    add_dap_test(test_integration_crypto_network integration/test_crypto_network.c)
#    register_dap_test(test_integration_crypto_network "integration;crypto;network")
#endif()

# PQC & new symmetric algorithm unit tests (ML-DSA, ML-KEM, ChaCha20, NTRU Prime, AES)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/pqc/CMakeLists.txt)
    add_subdirectory(unit/crypto/pqc)
    message("[+] PQC algorithm unit tests configured")
endif()

# Crypto Known Answer Tests (RFC/NIST vectors)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/kat/CMakeLists.txt)
    add_subdirectory(unit/crypto/kat)
    message("[+] Crypto KAT tests configured")
endif()

# GPU NTT correctness tests
if(TARGET dap_gpu AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/gpu/CMakeLists.txt)
    add_subdirectory(unit/crypto/gpu)
    message("[+] GPU NTT correctness tests configured")
endif()

# Fuzz targets (libFuzzer, Clang only)
option(BUILD_FUZZING "Build libFuzzer fuzz targets (requires Clang)" OFF)
if(BUILD_FUZZING AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/CMakeLists.txt)
    add_subdirectory(fuzz)
    message("[+] Fuzz targets configured")
endif()

# Math bignum unit tests (NTT, poly, multi-precision)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/math/bignum/CMakeLists.txt)
    add_subdirectory(unit/math/bignum)
    message("[+] Math bignum unit tests configured")
endif()

# Build Performance Tests
message("[+] Configuring Performance Tests")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/performance/test_crypto_benchmarks.c)
    add_dap_test(test_performance_crypto performance/test_crypto_benchmarks.c)
    register_dap_test(test_performance_crypto "performance;crypto;benchmark")
    
    # Performance tests need longer timeout
    set_tests_properties(test_performance_crypto PROPERTIES TIMEOUT ${DAP_T_PERF})
endif()

# JSON performance benchmarks (dap_json vs competitors)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/performance/json/CMakeLists.txt)
    add_subdirectory(performance/json)
    message("[+] JSON performance benchmarks configured")
endif()

# Performance benchmarks options
option(BUILD_BENCHMARKS "Build performance benchmarks with competitor implementations" OFF)
option(BENCHMARK_COMPETITORS "Include competitor hash table implementations in benchmark" OFF)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/performance/hashtable/CMakeLists.txt)
    add_subdirectory(performance/hashtable)
    message("[+] Hash Table performance benchmarks configured")
endif()

# Keccak/SHA3 performance benchmarks (dap_keccak vs XKCP, tiny_sha3)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/performance/keccak/CMakeLists.txt)
    add_subdirectory(performance/keccak)
    message("[+] Keccak/SHA3 performance benchmarks configured")
endif()

# ECDSA performance benchmarks (DAP vs bitcoin-core/secp256k1, OpenSSL)
if(BUILD_WITH_ECDSA AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/performance/ecdsa/CMakeLists.txt)
    add_subdirectory(performance/ecdsa)
    message("[+] ECDSA performance benchmarks configured")
endif()

# GlobalDB/KV Storage performance benchmarks (DAP vs MDBX, LMDB, RocksDB, LevelDB)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/performance/global_db/CMakeLists.txt)
    add_subdirectory(performance/global_db)
    message("[+] GlobalDB/KV Storage performance benchmarks configured")
endif()

# Crypto competitive benchmarks (DAP vs liboqs, OpenSSL, libsodium)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/performance/crypto/CMakeLists.txt)
    add_subdirectory(performance/crypto)
    message("[+] Crypto competitive benchmarks configured")
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

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/security/test_ring_signature_zkp.c)
    add_dap_test(test_security_ring_zkp security/test_ring_signature_zkp.c)
    register_dap_test(test_security_ring_zkp "security;crypto;zkp")
endif()

# Custom test targets — all run in parallel by default (DAP_NPROC cores)
add_custom_target(test_unit
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j${DAP_NPROC} -L "unit"
    COMMENT "Running unit tests (${DAP_NPROC} parallel)"
)

add_custom_target(test_integration
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j${DAP_NPROC} -L "integration"
    COMMENT "Running integration tests (${DAP_NPROC} parallel)"
)

add_custom_target(test_e2e
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j${DAP_NPROC} -L "e2e"
    COMMENT "Running E2E tests (${DAP_NPROC} parallel)"
)

add_custom_target(test_performance
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j${DAP_NPROC} -L "performance"
    COMMENT "Running performance tests (${DAP_NPROC} parallel)"
)

add_custom_target(test_functional
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j${DAP_NPROC} -L "functional"
    COMMENT "Running functional tests (${DAP_NPROC} parallel)"
)

add_custom_target(test_regression
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j${DAP_NPROC} -L "regression"
    COMMENT "Running regression tests (${DAP_NPROC} parallel)"
)

add_custom_target(test_security
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j${DAP_NPROC} -L "security"
    COMMENT "Running security tests (${DAP_NPROC} parallel)"
)

add_custom_target(test_crypto
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j${DAP_NPROC} -L "crypto"
    COMMENT "Running crypto-related tests (${DAP_NPROC} parallel)"
)

add_custom_target(test_all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j${DAP_NPROC}
    COMMENT "Running all tests (${DAP_NPROC} parallel)"
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
message("[+] - Performance test files found: ${PERFORMANCE_COUNT}")
message("[+] - Regression test files found: ${REGRESSION_COUNT}")

message("[+] Parallel test execution: ${DAP_NPROC} jobs (auto-detected)")
message("[+] Test targets available:")
message("[+] - make test_unit        : Run only unit tests")
message("[+] - make test_integration : Run only integration tests")
message("[+] - make test_performance : Run only performance tests")
message("[+] - make test_regression  : Run only regression tests")
message("[+] - make test_crypto      : Run all crypto-related tests")
message("[+] - make test_all         : Run all tests")
message("[+] - make test             : Run all tests (CMake built-in)")
message("[+] - ctest --preset default: Run with preset (parallel auto)")
message("[+] All targets run tests in parallel by default")

message("[+] DAP SDK Test configuration completed successfully")