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
    
    # Link with DAP SDK libraries — all public include dirs propagate automatically
    target_link_libraries(${TEST_NAME} 
        dap_sdk dap_test
    )
    
    # Only non-propagated paths
    target_include_directories(${TEST_NAME} PRIVATE
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

# TODO: test_dap_hash.c needs update for new dap_hash() API (no flags/params)
#if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/test_dap_hash.c)
#    add_dap_test(test_unit_crypto_dap_hash unit/crypto/test_dap_hash.c)
#    register_dap_test(test_unit_crypto_dap_hash "unit;crypto;hash")
#endif()

# Chipmunk Ring (LRS) deterministic primitive KATs
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_lrs_kat.c)
    add_dap_test(test_unit_crypto_chipmunk_lrs_kat unit/crypto/chipmunk_ring/test_chipmunk_lrs_kat.c)
    register_dap_test(test_unit_crypto_chipmunk_lrs_kat "unit;crypto;ring;kat")
endif()

# CR-11.G Phase 7.7 / G1 gate (task_ac273cea): MRNG MSIS hardness estimator
# must meet CHIPMUNK_MRING_MSIS_BITS_MIN (= 128 bits) before M2 unblocks the
# cryptographic core.  Remaining MRNG tests (KAT/security/bench/signoff)
# come back in M7 with file names test_chipmunk_mring_*.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_hardness.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_hardness
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_hardness.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_hardness
                      "unit;crypto;ring;mring;hardness")
endif()

# CR-11.G Phase 7.7 / M3.1 (task_ac273cea): MRNG vector-commitment layer.
# Determinism + slot independence of generators, pack_b validation,
# homomorphism of vcom, bit-flip sensitivity, chknorm bounds.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_statement_vcom.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_statement_vcom
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_statement_vcom.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_statement_vcom
                      "unit;crypto;ring;mring;statement")
endif()

# CR-11.G Phase 7.7 / M3.2 (task_ac273cea): MRNG unified-statement
# Claim 1 v2.1 — augmented-dim formula, augment_witness layout,
# aggregate_X linearity, honest Claim 1 across multiple challenges, and
# two soundness sanity checks (tampered Y_pk, tampered b̃[N]).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_statement_eval.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_statement_eval
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_statement_eval.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_statement_eval
                      "unit;crypto;ring;mring;statement;claim1")
endif()

# CR-11.G Phase 7.7 / M3.3 (task_ac273cea): MRNG bind-block helpers —
# derive_A_T determinism, mask sampling bound, same-witness reconstruction
# identity, tamper-detection soundness, norm gates, EAGAIN abort.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_statement_bind.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_statement_bind
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_statement_bind.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_statement_bind
                      "unit;crypto;ring;mring;statement;bind")
endif()

# CR-11.G Phase 7.7 / M4.0a (task_ac273cea): MRNG R_q inversion +
# empirical invertibility rate (resolves the full-vs-partial ring-
# splitting question that the fold's challenge-retry design depends on).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_invert.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_invert
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_invert.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_invert
                      "unit;crypto;ring;mring;invert")
endif()

# CR-11.G Phase 7.7 / G3.1 §9.2 (task_ac273cea): MRNG ring-extension
# arithmetic R_q^(e)=R_q[Y]/Φ₉ (e=6).  Ring axioms, x·x⁻¹=1 in F_{q⁶}
# (scalar) and per-slot (general), embed∘project=id, is_in_base, and a
# CI Rabin re-check that Φ₉ is irreducible over F_q.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_ext.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_ext
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_ext.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_ext
                      "unit;crypto;ring;mring;ext")
endif()

# CR-11.G Phase 7.7 / G3.1 §9.3 (task_ac273cea): MRNG fold challenge set.
# Deterministic Fiat-Shamir sampler over the subtractive set S = F_{q⁶}\{0};
# every pairwise difference is invertible in R_q^(e) (single-shot 128-bit
# fold soundness, no parallel repetition).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_subtractive.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_subtractive
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_subtractive.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_subtractive
                      "unit;crypto;ring;mring;subtractive")
endif()

# CR-11.G Phase 7.7 / M4 (task_ac273cea): MRNG halving fold over R_q^(e).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_fold.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_fold
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_fold.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_fold
                      "unit;crypto;ring;mring;fold")
endif()

# CR-11.G Phase 7.7 / G4 (task_ac273cea): MRNG byte-exact FS transcript.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_transcript.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_transcript
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_transcript.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_transcript
                      "unit;crypto;ring;mring;transcript")
endif()

# CR-11.G Phase 7.7 / M6 (task_ac273cea): MRNG end-to-end sign/verify.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_sign.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_sign
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_sign.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_sign
                      "unit;crypto;ring;mring;sign")
endif()

# CR-11.G Phase 7.7 / M7.1 (task_ac273cea): MRNG KAT (Known-Answer Tests).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_kat.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_kat
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_kat.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_kat
                      "unit;crypto;ring;mring;kat")
endif()

# CR-11.G Phase 7.7 / M7.2 (task_ac273cea): MRNG security / adversarial tests.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_security.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_security
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_security.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_security
                      "unit;crypto;ring;mring;security")
endif()

# CR-11.G Phase 7.7 / M7.4 (task_ac273cea): MRNG production signoff.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_signoff.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_signoff
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_signoff.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_signoff
                      "unit;crypto;ring;mring;signoff")
endif()

# CR-11.G Phase 7.7 / M8 (task_ac273cea): MRNG + LRS anonymity tests.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_anonymity.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_anonymity
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_anonymity.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_anonymity
                      "unit;crypto;ring;mring;anonymity")
endif()

# MRNG malformed wire input tests.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mring_wire.c)
    add_dap_test(test_unit_crypto_chipmunk_mring_wire
                 unit/crypto/chipmunk_ring/test_chipmunk_mring_wire.c)
    register_dap_test(test_unit_crypto_chipmunk_mring_wire
                      "unit;crypto;ring;mring;wire")
endif()

# LoTRS basic keygen/sign/verify test.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_lotrs_basic.c)
    add_dap_test(test_unit_lotrs_basic
                 unit/crypto/chipmunk_ring/test_lotrs_basic.c)
    register_dap_test(test_unit_lotrs_basic
                      "unit;crypto;lotrs")
endif()

# Chipmunk Ring — non-interactive lattice ring signature.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_ring.c)
    add_dap_test(test_unit_chipmunk_ring
                 unit/crypto/chipmunk_ring/test_chipmunk_ring.c)
    register_dap_test(test_unit_chipmunk_ring
                      "unit;crypto;ring")
endif()

# Chipmunk Ring KAT + security + wire tests.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_ring_kat.c)
    add_dap_test(test_unit_chipmunk_ring_kat
                 unit/crypto/chipmunk_ring/test_chipmunk_ring_kat.c)
    register_dap_test(test_unit_chipmunk_ring_kat
                      "unit;crypto;ring;kat")
endif()

# LoTRS KAT (Known-Answer Tests).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_lotrs_kat.c)
    add_dap_test(test_unit_lotrs_kat
                 unit/crypto/chipmunk_ring/test_lotrs_kat.c)
    register_dap_test(test_unit_lotrs_kat
                      "unit;crypto;lotrs;kat")
endif()

# LoTRS security tests.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_lotrs_security.c)
    add_dap_test(test_unit_lotrs_security
                 unit/crypto/chipmunk_ring/test_lotrs_security.c)
    register_dap_test(test_unit_lotrs_security
                      "unit;crypto;lotrs;security")
endif()

# ============================================================
# New anonymous crypto tests (task_4fcb8959)
# ============================================================

# SNARK: lattice-based Ligero-style proof
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_snark.c)
    add_dap_test(test_unit_chipmunk_snark
                 unit/crypto/chipmunk_ring/test_chipmunk_snark.c)
    register_dap_test(test_unit_chipmunk_snark
                      "unit;crypto;snark;lattice")
endif()

# Pedersen: lattice-based commitments
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_pedersen.c)
    add_dap_test(test_unit_chipmunk_pedersen
                 unit/crypto/chipmunk_ring/test_chipmunk_pedersen.c)
    register_dap_test(test_unit_chipmunk_pedersen
                      "unit;crypto;pedersen;lattice")
endif()

# Range proof: Stern-like 128-challenge
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_range_proof.c)
    add_dap_test(test_unit_chipmunk_range_proof
                 unit/crypto/chipmunk_ring/test_chipmunk_range_proof.c)
    register_dap_test(test_unit_chipmunk_range_proof
                      "unit;crypto;range-proof;stern")
endif()

# Mixnet: batching + DC-net
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_chipmunk_mixnet.c)
    add_dap_test(test_unit_chipmunk_mixnet
                 unit/crypto/chipmunk_ring/test_chipmunk_mixnet.c)
    register_dap_test(test_unit_chipmunk_mixnet
                      "unit;crypto;mixnet;dcnet")
endif()

# Ledger anonymous integration
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk_ring/test_ledger_anon.c)
    add_dap_test(test_unit_ledger_anon
                 unit/crypto/chipmunk_ring/test_ledger_anon.c)
    register_dap_test(test_unit_ledger_anon
                      "unit;ledger;anonymous;integration")
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
endif()

#if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_large_scale.c)
#    add_dap_test(test_unit_crypto_chipmunk_large_scale unit/crypto/chipmunk/test_chipmunk_large_scale.c)
#    register_dap_test(test_unit_crypto_chipmunk_large_scale "unit;crypto;chipmunk")
#endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_invntt_fix.c)
    add_dap_test(test_unit_crypto_chipmunk_invntt_fix unit/crypto/chipmunk/test_chipmunk_invntt_fix.c)
    register_dap_test(test_unit_crypto_chipmunk_invntt_fix "unit;crypto;chipmunk")
endif()

# CR-D5 regression: chipmunk_hots_setup determinism against uninitialised-stack UB
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_hots_setup_determinism.c)
    add_dap_test(test_unit_crypto_chipmunk_hots_setup_determinism
        unit/crypto/chipmunk/test_chipmunk_hots_setup_determinism.c)
    register_dap_test(test_unit_crypto_chipmunk_hots_setup_determinism
        "unit;crypto;chipmunk;regression")
endif()

# CR-D3 regression: Chipmunk private keys are strictly one-time; second
# chipmunk_sign call must fail fast with CHIPMUNK_ERROR_KEY_EXHAUSTED.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_one_time_exhaustion.c)
    add_dap_test(test_unit_crypto_chipmunk_one_time_exhaustion
        unit/crypto/chipmunk/test_chipmunk_one_time_exhaustion.c)
    register_dap_test(test_unit_crypto_chipmunk_one_time_exhaustion
        "unit;crypto;chipmunk;regression;hots")
endif()

# CR-D15.A regression: honest HVC Merkle layer (Ajtai hash, leaf binding,
# full path verification, hasher-seed sensitivity).  Catches the four
# "security theatre" breaks that the pre-Round-5 implementation carried.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_hvc_honest.c)
    add_dap_test(test_unit_crypto_chipmunk_hvc_honest
        unit/crypto/chipmunk/test_chipmunk_hvc_honest.c)
    register_dap_test(test_unit_crypto_chipmunk_hvc_honest
        "unit;crypto;chipmunk;regression;hvc;tree")
endif()

# CR-D15.B regression: Chipmunk Hypertree (many-time signature via
# Merkle tree over HOTS leaves).  Exercises every leaf_index, tamper
# detection, exhaustion fail-fast, serialise round-trips.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_hypertree.c)
    add_dap_test(test_unit_crypto_chipmunk_hypertree
        unit/crypto/chipmunk/test_chipmunk_hypertree.c)
    register_dap_test(test_unit_crypto_chipmunk_hypertree
        "unit;crypto;chipmunk;regression;hypertree")
endif()

# CR-D10 regression: canonical wire codec for chipmunk_multi_signature_t
# and the dap_sign aggregate verifier bridge.  Covers round-trip, dap_sign
# wrapper happy-path, wrong-message rejection (incl. divergent messages),
# and every malformed-blob vector (bad magic / version / reserved /
# signer_count / payload length / trailing bytes / legacy raw struct /
# coefficient flip).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_multi_sig_codec.c)
    add_dap_test(test_unit_crypto_chipmunk_multi_sig_codec
        unit/crypto/chipmunk/test_chipmunk_multi_sig_codec.c)
    register_dap_test(test_unit_crypto_chipmunk_multi_sig_codec
        "unit;crypto;chipmunk;regression;cr-d10;aggregation;codec")
endif()

# CR-D10 stage 1: schema-driven wire layer (chipmunk_multi_signature_serialize_schema).
# Smoke / round-trip test that exercises the new dap_serialize schemas in
# isolation, before the production codec switches over to them.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/chipmunk/test_chipmunk_multi_sig_schema.c)
    add_dap_test(test_unit_crypto_chipmunk_multi_sig_schema
        unit/crypto/chipmunk/test_chipmunk_multi_sig_schema.c)
    register_dap_test(test_unit_crypto_chipmunk_multi_sig_schema
        "unit;crypto;chipmunk;cr-d10;aggregation;schema")
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
    register_dap_test(test_field_mul "unit;crypto;ecdsa")
endif()

if(BUILD_WITH_ECDSA AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/crypto/test_ecdsa_debug.c)
    add_dap_test(test_ecdsa_debug unit/crypto/test_ecdsa_debug.c)
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

# HTTP/2 connection handler (unit + in-memory integration)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/net/h2/test_dap_http_h2_conn.c)
    add_dap_test(test_unit_dap_http_h2_conn unit/net/h2/test_dap_http_h2_conn.c)
    target_include_directories(test_unit_dap_http_h2_conn PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../module/net/server/http_server/h2/include)
    register_dap_test(test_unit_dap_http_h2_conn "unit;net;http2")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/net/h2/test_dap_http_h2_integration.c)
    add_dap_test(test_unit_dap_http_h2_integration unit/net/h2/test_dap_http_h2_integration.c)
    target_include_directories(test_unit_dap_http_h2_integration PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../module/net/server/http_server/h2/include)
    register_dap_test(test_unit_dap_http_h2_integration "integration;unit;net;http2")
endif()

# HTTP/2 HPACK + frame layer (http_server/h2)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/net/h2/test_dap_http_h2_hpack.c)
    add_dap_test(test_unit_net_h2_hpack unit/net/h2/test_dap_http_h2_hpack.c)
    target_include_directories(test_unit_net_h2_hpack PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../module/net/server/http_server/h2/include
    )
    register_dap_test(test_unit_net_h2_hpack "unit;net;http2;hpack")
endif()

# Build Integration Tests
message("[+] Configuring Integration Tests")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_crypto_network.c)
    add_dap_test(test_integration_crypto_network integration/test_crypto_network.c)
    register_dap_test(test_integration_crypto_network "integration;crypto;network")
endif()

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