cmake_minimum_required(VERSION 3.10)

# Chipmunk Ring Unit Tests
set(TEST_NAMES
    test_chipmunk_ring_basic
    test_chipmunk_ring_anonymity
    test_chipmunk_ring_performance
    test_chipmunk_ring_edge_cases
#    test_chipmunk_ring_stress
    test_minimal_ring
    test_chipmunk_ring_multi_signer
    test_chipmunk_ring_threshold_coordination
    test_chipmunk_ring_lagrange_interpolation
)

set(TEST_SOURCES
    test_chipmunk_ring_basic.c
    test_chipmunk_ring_anonymity.c
    test_chipmunk_ring_performance.c
    test_chipmunk_ring_edge_cases.c
#    test_chipmunk_ring_stress.c
    test_minimal_ring.c
    test_chipmunk_ring_multi_signer.c
    test_chipmunk_ring_threshold_coordination.c
    test_chipmunk_ring_lagrange_interpolation.c
)

# Create test executables
list(LENGTH TEST_NAMES num_tests)
math(EXPR num_tests "${num_tests} - 1")

foreach(i RANGE ${num_tests})
    list(GET TEST_NAMES ${i} test_name)
    list(GET TEST_SOURCES ${i} test_source)

    add_executable(${test_name} ${test_source})

    # Include directories
    target_include_directories(${test_name} PRIVATE
        ../../../../module/core/include
        ../../../../module/crypto/include
        ../../../../module/io/include
        ../../../../module/global-db/include
        ../../../../module/net/include
        ../../../../tests/fixtures/utilities
        ../../../../module/plugin/include
        ../../../../module/test/include
    )

    # Link libraries
    target_link_libraries(${test_name} PRIVATE
        dap_core
        dap_crypto
        dap_io
        dap_global_db
        dap_net
        dap_plugin
        dap_test
    )

    # Add test
    add_test(NAME ${test_name}
             COMMAND ${test_name})
endforeach()
