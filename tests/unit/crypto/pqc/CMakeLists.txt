cmake_minimum_required(VERSION 3.10)
project(test_pqc_algorithms C)

add_executable(${PROJECT_NAME} test_pqc_algorithms.c)

target_link_libraries(${PROJECT_NAME}
    dap_sdk dap_test
)

add_test(NAME test_pqc_algorithms COMMAND ${PROJECT_NAME})
set_tests_properties(test_pqc_algorithms PROPERTIES
    LABELS "unit;crypto;pqc"
    TIMEOUT ${DAP_T300}
)

# ----------------------------------------------------------------------
# SHAKE / Dilithium MODE_2 byte-for-byte regression KAT.  Anchors:
#   * canonical FIPS 202 SHAKE128/256("", 64)
#   * legacy SHAKE128/256("", 64)              — master pre-FIPS bytes
#   * vanilla Dilithium-2 deterministic pk/sk  — master compat
add_executable(test_shake_legacy_kat test_shake_legacy_kat.c)
target_link_libraries(test_shake_legacy_kat dap_sdk dap_test)
add_test(NAME test_shake_legacy_kat COMMAND test_shake_legacy_kat)
set_tests_properties(test_shake_legacy_kat PROPERTIES
    LABELS "unit;crypto;pqc;kat;regression"
    TIMEOUT 30
)
