cmake_minimum_required(VERSION 3.10)

# Base58 Encoding Unit Tests
project(test_base58_encoding)

# Test executable
add_executable(${PROJECT_NAME}
    test_base58_encoding.c
)

# Include directories are set at the parent level CMakeLists.txt

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
