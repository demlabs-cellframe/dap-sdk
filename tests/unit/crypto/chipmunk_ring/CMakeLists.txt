cmake_minimum_required(VERSION 3.10)

message(STATUS "[chipmunk_ring tests] Configuring chipmunk_ring tests")

# ============================================================================
# Chipmunk Ring Unit Tests
# ============================================================================

macro(add_chipmunk_test NAME)
    add_executable(${NAME} ${NAME}.c)
    target_link_libraries(${NAME} dap_sdk)
    set_target_properties(${NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )
endmacro()

# Existing tests
add_chipmunk_test(test_chipmunk_ring)
add_chipmunk_test(test_chipmunk_ring_kat)
add_chipmunk_test(test_chipmunk_lrs_kat)
add_chipmunk_test(test_chipmunk_mring_sign)
add_chipmunk_test(test_chipmunk_mring_wire)
add_chipmunk_test(test_chipmunk_mring_ext)
add_chipmunk_test(test_chipmunk_mring_fold)
add_chipmunk_test(test_chipmunk_mring_anonymity)
add_chipmunk_test(test_chipmunk_mring_kat)
add_chipmunk_test(test_chipmunk_mring_hardness)
add_chipmunk_test(test_chipmunk_mring_invert)
add_chipmunk_test(test_chipmunk_mring_security)
add_chipmunk_test(test_chipmunk_mring_signoff)
add_chipmunk_test(test_chipmunk_mring_statement_bind)
add_chipmunk_test(test_chipmunk_mring_statement_eval)
add_chipmunk_test(test_chipmunk_mring_statement_vcom)
add_chipmunk_test(test_chipmunk_mring_subtractive)
add_chipmunk_test(test_chipmunk_mring_transcript)
add_chipmunk_test(test_lotrs_basic)
add_chipmunk_test(test_lotrs_kat)
add_chipmunk_test(test_lotrs_security)

# New anonymous crypto tests
add_chipmunk_test(test_chipmunk_snark)
add_chipmunk_test(test_chipmunk_pedersen)
add_chipmunk_test(test_chipmunk_range_proof)
add_chipmunk_test(test_chipmunk_mixnet)
add_chipmunk_test(test_ledger_anon)

# Integration test
add_chipmunk_test(test_anon_tx_e2e)

# Minimal Pedersen test for debugging
add_chipmunk_test(test_pedersen_minimal)

# Phase 9.1: FRI field arithmetic tests
add_chipmunk_test(test_chipmunk_field)

# Phase 9.2: 2048-point NTT tests
add_chipmunk_test(test_chipmunk_fri_ntt)

# Phase 9.5: Reed-Solomon encoding tests
add_chipmunk_test(test_chipmunk_rs)

# Phase 9.6+9.7: FRI commit + query tests
add_chipmunk_test(test_chipmunk_fri)

# Phase 9.8: DEEP composition tests
add_chipmunk_test(test_chipmunk_deep)
