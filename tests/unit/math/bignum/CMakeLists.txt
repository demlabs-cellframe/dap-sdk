cmake_minimum_required(VERSION 3.12)

add_executable(test_bignum test_bignum.c)

target_link_libraries(test_bignum
    dap_math_bignum
    dap_core
    dap_test
)

target_include_directories(test_bignum PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/math/bignum/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/math/bignum/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/test
)

if(UNIX AND NOT ANDROID)
    target_link_libraries(test_bignum pthread m)
endif()

set_target_properties(test_bignum PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
)

add_test(NAME test_bignum COMMAND test_bignum)
set_tests_properties(test_bignum PROPERTIES
    LABELS "unit;math;bignum"
    TIMEOUT 120
)
