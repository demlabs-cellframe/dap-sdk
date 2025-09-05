cmake_minimum_required(VERSION 3.10)

# Deterministic Keys Unit Tests
project(test_deterministic_keys)

# Test executable
add_executable(${PROJECT_NAME}
    test_deterministic_keys.c
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
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
target_link_libraries(${PROJECT_NAME} PRIVATE
    dap_core
    dap_crypto
    dap_io
    dap_global_db
    dap_net
    dap_plugin
    dap_test
)

# Add test
add_test(NAME ${PROJECT_NAME}
         COMMAND ${PROJECT_NAME})
