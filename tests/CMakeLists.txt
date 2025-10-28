cmake_minimum_required(VERSION 3.10)

# DAP SDK Tests
project(dap_sdk_tests)

# Enable testing
enable_testing()

# Add unit tests subdirectory
add_subdirectory(unit)

# TODO: Add integration tests subdirectory
# Integration tests should test real network connectivity, multi-component interactions
# add_subdirectory(integration)

