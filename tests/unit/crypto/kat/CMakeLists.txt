cmake_minimum_required(VERSION 3.10)
project(test_crypto_kat C)

add_executable(${PROJECT_NAME} test_crypto_kat.c)

target_link_libraries(${PROJECT_NAME}
    dap_sdk dap_test
)

add_test(NAME test_crypto_kat COMMAND ${PROJECT_NAME})
set_tests_properties(test_crypto_kat PROPERTIES
    LABELS "unit;crypto;kat"
    TIMEOUT ${DAP_T120}
)
