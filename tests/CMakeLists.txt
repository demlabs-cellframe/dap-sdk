cmake_minimum_required(VERSION 3.10)

# DAP SDK Tests
project(dap_sdk_tests)

# Enable testing
enable_testing()

# Add unit tests subdirectory
add_subdirectory(unit)

# Add integration tests subdirectory
add_subdirectory(integration)

