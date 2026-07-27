cmake_minimum_required(VERSION 3.10)

project(test_chipmunk)

macro(add_chipmunk_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE dap_sdk dap_test)
    target_include_directories(${NAME} PRIVATE
        ../../../../../tests/fixtures/utilities
    )
endmacro()

add_chipmunk_test(test_chipmunk_performance test_chipmunk_performance.c)
add_chipmunk_test(test_chipmunk_hots test_chipmunk_hots.c)
add_chipmunk_test(test_chipmunk_large_scale test_chipmunk_large_scale.c)
add_chipmunk_test(test_chipmunk_ntt_comparison test_ntt_comparison.c)
add_chipmunk_test(test_chipmunk_tree test_chipmunk_tree.c)

add_chipmunk_test(test_chipmunk_multithread test_multithread_crypto.c)
target_link_libraries(test_chipmunk_multithread PRIVATE pthread)

add_chipmunk_test(test_chipmunk_invntt_fix test_chipmunk_invntt_fix.c)

add_chipmunk_test(test_chipmunk_bdlop test_chipmunk_bdlop.c)

add_test(NAME test_chipmunk_performance COMMAND test_chipmunk_performance)
add_test(NAME test_chipmunk_hots COMMAND test_chipmunk_hots)
add_test(NAME test_chipmunk_large_scale COMMAND test_chipmunk_large_scale)
add_test(NAME test_chipmunk_ntt_comparison COMMAND test_chipmunk_ntt_comparison)
add_test(NAME test_chipmunk_tree COMMAND test_chipmunk_tree)
add_test(NAME test_chipmunk_multithread COMMAND test_chipmunk_multithread)
add_test(NAME test_chipmunk_invntt_fix COMMAND test_chipmunk_invntt_fix)
add_test(NAME test_chipmunk_bdlop COMMAND test_chipmunk_bdlop)
