cmake_minimum_required(VERSION 3.10)
project(dap_sdk_tests C)

# Test configuration
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include directories for DAP SDK
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../module/test/include
)

message("[+] DAP SDK Tests structure:")
message("[+] - Unit tests: ${CMAKE_CURRENT_SOURCE_DIR}/unit")
message("[+] - Integration tests: ${CMAKE_CURRENT_SOURCE_DIR}/integration") 
message("[+] - Performance tests: ${CMAKE_CURRENT_SOURCE_DIR}/performance")
message("[+] - Security tests: ${CMAKE_CURRENT_SOURCE_DIR}/security")
message("[+] - Fixtures: ${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Enable testing
enable_testing()

message("[+] Tests configuration completed")
