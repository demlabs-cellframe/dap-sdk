# IO Layer Integration Tests

add_executable(test_io_flow_lb_tiers
    test_io_flow_lb_tiers.c
)

target_link_libraries(test_io_flow_lb_tiers
    dap_core
    dap_crypto
    dap_io
    dap_io_static
    pthread
    m
)

target_include_directories(test_io_flow_lb_tiers PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../io/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../crypto/include
)

add_test(NAME test_io_flow_lb_tiers
    COMMAND test_io_flow_lb_tiers
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
