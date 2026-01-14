# DAP SDK Network Module Integration Tests

# UDP KEM Scaling Test
add_executable(test_udp_kem_scaling
    test_udp_kem_scaling.c
)

# Link required libraries
dap_test_link_libraries(test_udp_kem_scaling
    LINK_MODE STATIC
    REQUIRED_LIBRARIES dap_core dap_crypto dap_io dap_enc_server dap_net_trans dap_http_client dap_http_server
)

# Add include directories
dap_test_add_includes(test_udp_kem_scaling
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../net/trans/udp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../io/include
)

# Add to CTest
add_test(NAME test_udp_kem_scaling COMMAND test_udp_kem_scaling)

# Sub-modules
add_subdirectory(http)
add_subdirectory(stream)
add_subdirectory(trans)

