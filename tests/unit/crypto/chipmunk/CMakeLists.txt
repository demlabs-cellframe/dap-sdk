cmake_minimum_required(VERSION 3.10)

# Chipmunk Unit Tests
project(test_chipmunk)

# Test executable - Performance Test
add_executable(test_chipmunk_performance
    test_chipmunk_performance.c
)

target_include_directories(test_chipmunk_performance PRIVATE
    ../../../../../module/core/include
    ../../../../../module/crypto/include
    ../../../../../module/io/include
    ../../../../../module/global-db/include
    ../../../../../module/net/include
    ../../../../../tests/fixtures/utilities
    ../../../../../module/plugin/include
    ../../../../../module/test/include
)

target_link_libraries(test_chipmunk_performance PRIVATE
    dap_core
    dap_crypto
    dap_io
    dap_global_db
    dap_net
    dap_plugin
    dap_test
)

# Test executable - HOTS Test
add_executable(test_chipmunk_hots
    test_chipmunk_hots.c
)

target_include_directories(test_chipmunk_hots PRIVATE
    ../../../../../module/core/include
    ../../../../../module/crypto/include
    ../../../../../module/io/include
    ../../../../../module/global-db/include
    ../../../../../module/net/include
    ../../../../../tests/fixtures/utilities
    ../../../../../module/plugin/include
    ../../../../../module/test/include
)

target_link_libraries(test_chipmunk_hots PRIVATE
    dap_core
    dap_crypto
    dap_io
    dap_global_db
    dap_net
    dap_plugin
    dap_test
)

# Test executable - Large Scale Test
add_executable(test_chipmunk_large_scale
    test_chipmunk_large_scale.c
)

target_include_directories(test_chipmunk_large_scale PRIVATE
    ../../../../../module/core/include
    ../../../../../module/crypto/include
    ../../../../../module/io/include
    ../../../../../module/global-db/include
    ../../../../../module/net/include
    ../../../../../tests/fixtures/utilities
    ../../../../../module/plugin/include
    ../../../../../module/test/include
)

target_link_libraries(test_chipmunk_large_scale PRIVATE
    dap_core
    dap_crypto
    dap_io
    dap_global_db
    dap_net
    dap_plugin
    dap_test
)

# Test executable - NTT Comparison Test
add_executable(test_chipmunk_ntt_comparison
    test_ntt_comparison.c
)

target_include_directories(test_chipmunk_ntt_comparison PRIVATE
    ../../../../../module/core/include
    ../../../../../module/crypto/include
    ../../../../../module/io/include
    ../../../../../module/global-db/include
    ../../../../../module/net/include
    ../../../../../tests/fixtures/utilities
    ../../../../../module/plugin/include
    ../../../../../module/test/include
)

target_link_libraries(test_chipmunk_ntt_comparison PRIVATE
    dap_core
    dap_crypto
    dap_io
    dap_global_db
    dap_net
    dap_plugin
    dap_test
)

# Test executable - Tree Test
add_executable(test_chipmunk_tree
    test_chipmunk_tree.c
)

target_include_directories(test_chipmunk_tree PRIVATE
    ../../../../../module/core/include
    ../../../../../module/crypto/include
    ../../../../../module/io/include
    ../../../../../module/global-db/include
    ../../../../../module/net/include
    ../../../../../tests/fixtures/utilities
    ../../../../../module/plugin/include
    ../../../../../module/test/include
)

target_link_libraries(test_chipmunk_tree PRIVATE
    dap_core
    dap_crypto
    dap_io
    dap_global_db
    dap_net
    dap_plugin
    dap_test
)

# Test executable - Multithread Test
add_executable(test_chipmunk_multithread
    test_multithread_crypto.c
)

target_include_directories(test_chipmunk_multithread PRIVATE
    ../../../../../module/core/include
    ../../../../../module/crypto/include
    ../../../../../module/io/include
    ../../../../../module/global-db/include
    ../../../../../module/net/include
    ../../../../../tests/fixtures/utilities
    ../../../../../module/plugin/include
    ../../../../../module/test/include
)

target_link_libraries(test_chipmunk_multithread PRIVATE
    dap_core
    dap_crypto
    dap_io
    dap_global_db
    dap_net
    dap_plugin
    dap_test
    pthread
)

# Test executable - Ring Signature Test
add_executable(test_chipmunk_ring
    test_chipmunk_ring.c
)

target_include_directories(test_chipmunk_ring PRIVATE
    ../../../../../module/core/include
    ../../../../../module/crypto/include
    ../../../../../module/io/include
    ../../../../../module/global-db/include
    ../../../../../module/net/include
    ../../../../../tests/fixtures/utilities
    ../../../../../module/plugin/include
    ../../../../../module/test/include
)

target_link_libraries(test_chipmunk_ring PRIVATE
    dap_core
    dap_crypto
    dap_io
    dap_global_db
    dap_net
    dap_plugin
    dap_test
)

# Test executable - InvNTT Fix Test
add_executable(test_chipmunk_invntt_fix
    test_chipmunk_invntt_fix.c
)

target_include_directories(test_chipmunk_invntt_fix PRIVATE
    ../../../../../module/core/include
    ../../../../../module/crypto/include
    ../../../../../module/io/include
    ../../../../../module/global-db/include
    ../../../../../module/net/include
    ../../../../../tests/fixtures/utilities
    ../../../../../module/plugin/include
    ../../../../../module/test/include
)

target_link_libraries(test_chipmunk_invntt_fix PRIVATE
    dap_core
    dap_crypto
    dap_io
    dap_global_db
    dap_net
    dap_plugin
    dap_test
)