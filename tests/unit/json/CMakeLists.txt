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
# Test 5: Performance Baseline Benchmarks
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
# Test 6: Stage 1 Reference Implementation
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
# Test 7: Stage 2 Reference Implementation
# =========================================
# Coverage: DOM building, value parsing, tree assembly
# Tests: 13+ test cases (Phase 1.2)
project(test_json_stage2_ref)

add_executable(${PROJECT_NAME}
    test_stage2_ref.c
)

dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT 60
    LABELS "unit;json;stage2;reference;critical"
)

# =========================================
# Test 8: SIMD Correctness Tests (Phase 1.4)
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
