# Keccak/SHA3 Unit Tests
# Known Answer Tests using NIST CAVP vectors

# ============================================================================
# SHA3 Known Answer Test
# ============================================================================
project(test_sha3_kat)
add_executable(${PROJECT_NAME} test_sha3_kat.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

# ============================================================================
# SHA3 NIST Validation System Tests
# ============================================================================
project(test_sha3_nist)
add_executable(${PROJECT_NAME} test_sha3_nist.c)
dap_test_add_includes(${PROJECT_NAME})
dap_test_link_libraries(${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
