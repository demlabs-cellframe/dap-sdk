cmake_minimum_required(VERSION 3.10)

# =========================================
# DAP JSON Unit Tests - Phase 0.2
# =========================================
# Comprehensive test suite for dap_json module
# Created as part of Phase 0.2 test expansion
# Target: >= 85% API coverage + all critical edge cases

# Include test helpers
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/dap_test_helpers.cmake)

# =========================================
# Test 1: Unicode & UTF-8 Tests
# =========================================
# Coverage: Escape sequences, Unicode, BOM, surrogate pairs
# Tests: 14 test cases
project(test_json_unicode)

add_executable(${PROJECT_NAME}
    test_unicode.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 60
    LABELS "unit;json;unicode;critical"
)

# =========================================
# Test 2: Numeric Edge Cases Tests
# =========================================
# Coverage: INT64_MIN/MAX, UINT64_MAX, uint256, Inf/NaN, overflow
# Tests: 13 test cases
project(test_json_numeric_edge_cases)

add_executable(${PROJECT_NAME}
    test_numeric_edge_cases.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 60
    LABELS "unit;json;numeric;critical"
)

# =========================================
# Test 3: Invalid JSON Format Tests
# =========================================
# Coverage: Trailing commas, unclosed brackets, invalid escapes
# Tests: 15 test cases
project(test_json_invalid)

add_executable(${PROJECT_NAME}
    test_invalid_json.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 60
    LABELS "unit;json;invalid;critical"
)

# =========================================
# Test 4: Boundary Conditions Tests
# =========================================
# Coverage: Long strings (>64KB), deep nesting (>1000), huge arrays
# Tests: 8 test cases
project(test_json_boundary)

add_executable(${PROJECT_NAME}
    test_boundary_conditions.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 300  # Longer timeout for large data tests
    LABELS "unit;json;boundary;critical"
)

# =========================================
# Test 5: Unicode Escape Boundary Edge Cases
# =========================================
# Coverage: Unicode \uXXXX escapes crossing SIMD chunk boundaries
# Tests: Known AVX-512/SSE2 bug with JSON >= 66 bytes
# Bug: Multiple Unicode escapes in JSON >= 66 bytes fail in AVX-512/SSE2
project(test_unicode_escape_boundary)

add_executable(${PROJECT_NAME}
    test_unicode_escape_boundary.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 60
    LABELS "unit;json;unicode;boundary;edge_case;critical"
)

# =========================================
# Test 6: Performance Baseline Benchmarks
# =========================================
# Coverage: Parsing speed, serialization, memory usage
# Benchmarks: 5 benchmark suites
project(test_json_benchmark)

add_executable(${PROJECT_NAME}
    benchmark_baseline.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 600  # Longer timeout for benchmarks
    LABELS "unit;json;benchmark;performance"
)

# =========================================
# Test 11: Manual Architecture Selection API
# =========================================
# Coverage: dap_cpu_arch_set/get, manual override, correctness per arch
# Tests: Phase 1.4.2 - Manual architecture selection API testing
project(test_manual_arch_selection)

add_executable(${PROJECT_NAME}
    test_manual_arch_selection.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 60
    LABELS "unit;json;arch;selection;api;critical"
)

# =========================================
# Test 11: Stage 1 Reference Implementation
# =========================================
# Coverage: Structural indexing, UTF-8 validation, character classification
# Tests: 25+ test cases (Phase 1.1)
project(test_json_stage1_ref)

add_executable(${PROJECT_NAME}
    test_stage1_ref.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 60
    LABELS "unit;json;stage1;reference;critical"
)

# =========================================
# Note: test_stage2_ref removed - legacy test for old 56-byte structure
# Current Phase 2.0.4 uses 8-byte offset-based dap_json_value_t
# =========================================

# =========================================
# Test 11: SIMD Correctness Tests (Phase 1.4)
# =========================================
# Coverage: Compare SIMD implementations (AVX2, SSE2, NEON, AVX-512) vs reference
# Tests: 40+ test cases × 4 implementations = 160+ comparisons
project(test_json_stage1_simd_correctness)

add_executable(${PROJECT_NAME}
    test_stage1_simd_correctness.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "unit;json;stage1;simd;correctness;critical"
)

# =========================================
# Test 11: SSE2 Debug Tests (Phase 1.4.2)
# =========================================
# Coverage: Детальная отладка SSE2 tokenization
# Tests: 3 focused debug tests with detailed token comparison
# NOTE: x86-only test (requires SSE2)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686")
    project(test_json_stage1_sse2_debug)

    add_executable(${PROJECT_NAME}
        test_stage1_sse2_debug.c
    )

    dap_test_add_includes(${PROJECT_NAME})
    dap_test_link_libraries(${PROJECT_NAME})

    add_test(
        NAME ${PROJECT_NAME}
        COMMAND ${PROJECT_NAME}
    )

    set_tests_properties(${PROJECT_NAME} PROPERTIES
        TIMEOUT 60
        LABELS "unit;json;stage1;sse2;debug"
    )
endif()

# =========================================
# Test 11: UNIVERSAL SIMD String Spanning REGRESSION (Phase 1.4.4)
# =========================================
# DO NOT MODIFY - Permanent regression test for ALL architectures
# Coverage: String tokens crossing chunk boundaries (Reference, SSE2, AVX2, AVX-512, NEON)
# Tests: 5 cases × all available architectures
project(test_simd_string_spanning)

add_executable(${PROJECT_NAME}
    test_simd_string_spanning.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "unit;json;stage1;simd;regression;critical;universal"
)

# =========================================
# Summary
# =========================================
# Phase 0.2 tests: 55+ test cases across 5 files
# Phase 1.1 tests: 25+ test cases (Stage 1 reference)
# Phase 1.2 tests: 13+ test cases (Stage 2 reference)
# Total: 93+ test cases
# Total lines: ~3400 lines of test code
# Coverage targets:
#   - Unicode/UTF-8: 100% (14 tests)
#   - Numeric edges: 100% (13 tests)
#   - Invalid JSON: 100% (15 tests)
#   - Boundary conditions: 100% (8 tests)
#   - Performance baseline: established (5 benchmarks)
#   - Stage 1 reference: 100% (25+ tests)
#   - Stage 2 reference: 100% (13+ tests)
#   - SIMD correctness: 100% (4 implementations × 40+ test cases)
#
# Run tests:
#   - All JSON tests: ctest -L json --verbose
#   - Critical only: ctest -L critical --verbose
#   - Benchmarks: ctest -L benchmark --verbose
#   - Stage 1 only: ctest -L stage1 --verbose
#   - Stage 2 only: ctest -L stage2 --verbose
#   - SIMD correctness: ctest -L simd --verbose

# =========================================
# Note: benchmark_stage1_multi_arch moved to tests/performance/dap_json/
# =========================================

# =========================================
# Test 16: SimdJSON Stage 1 Tests (Phase 2.1)
# =========================================
# Coverage: SimdJSON-style Stage 1 implementation
# Tests: Correctness tests + Performance benchmarks
# Target: 4-5 GB/s throughput (AVX2), 6-8 GB/s (AVX-512)
# NOTE: x86-only test (requires AVX2)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686")
    project(test_simdjson_stage1)

    add_executable(${PROJECT_NAME}
        test_simdjson_stage1.c
    )

    dap_test_add_includes(${PROJECT_NAME})
    dap_test_link_libraries(${PROJECT_NAME})

    # Enable AVX2 for this test
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /arch:AVX2)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -mavx2)
    endif()

    add_test(
        NAME ${PROJECT_NAME}
        COMMAND ${PROJECT_NAME}
    )

    set_tests_properties(${PROJECT_NAME} PROPERTIES
        TIMEOUT 300
        LABELS "unit;json;stage1;simdjson;phase2;performance;critical"
    )
endif()

# =========================================
# Test 17: Memory DoS Protection (Phase 1.8.1 - CRITICAL SECURITY)
# =========================================
# Coverage: Hash collision, memory exhaustion, stack exhaustion,
#           billion laughs, O(n²) attacks, arena/string pool flooding
# Tests: 7 CRITICAL security tests
project(test_memory_dos)

add_executable(${PROJECT_NAME}
    test_memory_dos.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 300  # DoS tests may take time
    LABELS "unit;json;security;dos;critical;phase1.8"
)

# =========================================
# Test 18: SIMD Chunk Boundaries (Phase 1.8.1 - CRITICAL CORRECTNESS)
# =========================================
# Coverage: Structural chars, escapes, UTF-8, numbers, literals at 16/32/64-byte boundaries
# Tests: 8 CRITICAL correctness tests
project(test_simd_chunk_boundaries)

add_executable(${PROJECT_NAME}
    test_simd_chunk_boundaries.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 120
    LABELS "unit;json;simd;boundaries;critical;phase1.8"
)

# =========================================
# Test 19: Concurrency & Thread-Safety (Phase 1.8.1 - CRITICAL SECURITY)
# =========================================
# Coverage: TSan, concurrent parsing, arena/pool races, global state
# Tests: 8 CRITICAL tests (compile with -fsanitize=thread for full coverage)
project(test_concurrency)

add_executable(${PROJECT_NAME}
    test_concurrency.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

# Link pthread (not needed on Windows/Android - built into runtime)
if(UNIX AND NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# Optional: Enable ThreadSanitizer (uncomment for TSan builds)
# target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=thread)
# target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=thread)

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 300
    LABELS "unit;json;concurrency;thread-safety;critical;phase1.8"
)

# =========================================
# Test 20: Numeric Edge Cases Refined (Phase 1.8.2)
# =========================================
project(test_numeric_refined)
add_executable(${PROJECT_NAME} test_numeric_edge_cases_refined.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60 LABELS "unit;json;numeric;refined;phase1.8")

# =========================================
# Test 21: String Edge Cases (Phase 1.8.2)
# =========================================
project(test_string_edges)
add_executable(${PROJECT_NAME} test_string_edge_cases.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60 LABELS "unit;json;string;edges;phase1.8")

# =========================================
# Test 22: Whitespace Edge Cases (Phase 1.8.2)
# =========================================
project(test_whitespace_edges)
add_executable(${PROJECT_NAME} test_whitespace_edge_cases.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 120 LABELS "unit;json;whitespace;edges;phase1.8")

# =========================================
# Test 23: Array Edge Cases (Phase 1.8.2)
# =========================================
project(test_array_edges)
add_executable(${PROJECT_NAME} test_array_edge_cases.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 120 LABELS "unit;json;array;edges;phase1.8")

# =========================================
# Test 24: Object Edge Cases (Phase 1.8.2)
# =========================================
project(test_object_edges)
add_executable(${PROJECT_NAME} test_object_edge_cases.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 120 LABELS "unit;json;object;edges;phase1.8")

# =========================================
# Test 25: Literal Edge Cases (Phase 1.8.2)
# =========================================
project(test_literal_edges)
add_executable(${PROJECT_NAME} test_literal_edge_cases.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60 LABELS "unit;json;literal;edges;phase1.8")

# =========================================
# Test 26: Serialization Round-Trip (Phase 1.8.2)
# =========================================
project(test_serialization_roundtrip)
add_executable(${PROJECT_NAME} test_serialization_round_trip.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 120 LABELS "unit;json;serialization;roundtrip;phase1.8")

# =========================================
# Test 27: Platform-Specific (Phase 1.8.2)
# =========================================
project(test_platform_specific)
add_executable(${PROJECT_NAME} test_platform_specific.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60 LABELS "unit;json;platform;specific;phase1.8")

# =========================================
# Test 28-31: Phase 1.8.3 - Features
# =========================================
project(test_json5_features)
add_executable(${PROJECT_NAME} test_json5_features.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60 LABELS "unit;json;json5;features;phase1.8")

project(test_jsonc_comments)
add_executable(${PROJECT_NAME} test_jsonc_comments.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60 LABELS "unit;json;jsonc;comments;phase1.8")

project(test_streaming_api)
add_executable(${PROJECT_NAME} test_streaming_api.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60 LABELS "unit;json;streaming;api;phase1.8")

project(test_api_mutations)
add_executable(${PROJECT_NAME} test_api_mutations.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60 LABELS "unit;json;api;mutations;phase1.8")

# =========================================
# Test 32-35: Phase 1.8.4 - Production
# =========================================
# TODO: test_error_reporting uses non-existent API (dap_json_get_last_error, dap_json_get_error_position)
# Temporarily disabled until error reporting API is implemented
#project(test_error_reporting)
#add_executable(${PROJECT_NAME} test_error_reporting.c)
#dap_test_add_includes(${PROJECT_NAME})
#dap_test_link_libraries(${PROJECT_NAME})
#add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
#set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60 LABELS "unit;json;error;reporting;phase1.8")

project(test_real_world_data)
add_executable(${PROJECT_NAME} test_real_world_data.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 120 LABELS "unit;json;real;world;phase1.8")

project(test_perf_edge_cases)
add_executable(${PROJECT_NAME} test_performance_edge_cases.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 120 LABELS "unit;json;performance;edge;phase1.8")

project(test_jsonpath_queries)
add_executable(${PROJECT_NAME} test_jsonpath_queries.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60 LABELS "unit;json;jsonpath;queries;phase1.8")

project(test_json_schema)
add_executable(${PROJECT_NAME} test_json_schema.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 60 LABELS "unit;json;schema;validation;phase1.8")

# Phase 2 Profiling Tool
project(profile_stage2)
add_executable(${PROJECT_NAME} profile_stage2.c)
target_link_libraries(${PROJECT_NAME} dap_json dap_core)
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/unit/json")

# Profile Number Parsing (Phase 2.2)
project(profile_numbers)
add_executable(${PROJECT_NAME} profile_numbers.c)
target_link_libraries(${PROJECT_NAME} dap_json dap_core)
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/unit/json")

# Deep Nesting Benchmark (Phase 2.4)
project(benchmark_deep_nesting)
add_executable(${PROJECT_NAME} benchmark_deep_nesting.c)
target_link_libraries(${PROJECT_NAME} dap_json dap_core)
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/unit/json")
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 120 LABELS "unit;json;benchmark;phase2.4")

# Reference scanner 64-byte boundary test
project(test_reference_scanner_64)
add_executable(${PROJECT_NAME} test_reference_scanner_64.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 10 
    LABELS "unit;json;debug;reference"
)

# SIMD Chunk Boundary with Escape Sequences Test
# Tests correct handling of escape sequences at SIMD chunk boundaries (16/32/64 bytes)
# Critical for all SIMD architectures (SSE2, AVX2, AVX-512, NEON, SVE)
project(test_chunk_boundary_escapes)
add_executable(${PROJECT_NAME} test_chunk_boundary_escapes.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 30 
    LABELS "unit;json;simd;boundary;escape;critical"
)

# Simple 64-byte test with debug output
project(test_64byte_simple)
add_executable(${PROJECT_NAME} test_64byte_simple.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 10 
    LABELS "unit;json;debug;64byte"
)

# Bitmap classification verification helper test
project(test_bitmap_classification)
add_executable(${PROJECT_NAME} test_bitmap_classification.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
set_tests_properties(${PROJECT_NAME} PROPERTIES 
    TIMEOUT 10 
    LABELS "unit;json;debug;verification"
)

# Small JSON Profiling Micro-benchmark (Phase 3)
project(profile_small_json)
add_executable(${PROJECT_NAME} profile_small_json.c)
target_link_libraries(${PROJECT_NAME} dap_json dap_core)
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/unit/json")
