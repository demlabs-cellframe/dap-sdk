cmake_minimum_required(VERSION 3.10)
project(test_pqc_algorithms C)

add_executable(${PROJECT_NAME} test_pqc_algorithms.c)

target_link_libraries(${PROJECT_NAME}
    dap_sdk dap_test
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/crypto/src/sig/dilithium
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/crypto/src/sym/chacha20
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/crypto/src/kem/ntru_prime
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/crypto/src/sig/ntru_prime
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/test
)

add_test(NAME test_pqc_algorithms COMMAND ${PROJECT_NAME})
set_tests_properties(test_pqc_algorithms PROPERTIES
    LABELS "unit;crypto;pqc"
    TIMEOUT 300
)
