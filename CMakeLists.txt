cmake_minimum_required(VERSION 3.10)
project(python-dap C)

set(CMAKE_C_STANDARD 11)

# Build configuration
option(BUILD_PYTHON_CELLFRAME_COMMON "Build python_cellframe_common library" ON)
option(BUILD_TESTS "Build tests" ON)

# Include dap-sdk
add_subdirectory(dap-sdk)

# Find Python
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

if(BUILD_PYTHON_CELLFRAME_COMMON)
    # Create python_dap shared library
    add_library(python_dap SHARED
        # Core DAP functions we need for Python
        src/python_dap_core.c
        src/python_dap_config.c
        src/python_dap_crypto.c
        src/python_dap_network.c
        src/python_dap_system.c
        src/python_dap_common.c
    )

    # Link with DAP SDK components - only essential ones
    target_link_libraries(python_dap
        dap_core
        ${Python3_LIBRARIES}
    )

    # Include directories
    target_include_directories(python_dap PRIVATE
        ${Python3_INCLUDE_DIRS}
        dap-sdk/core/include
        dap-sdk/crypto/include
        dap-sdk/crypto/XKCP/lib/high/Keccak/FIPS202
        dap-sdk/crypto/XKCP/lib/low/KeccakP-1600
        dap-sdk/crypto/XKCP/lib/common
        dap-sdk/io/include
        dap-sdk/global-db/include
        dap-sdk/3rdparty
        include
    )

    # Set library properties
    set_target_properties(python_dap PROPERTIES
        PREFIX ""
        OUTPUT_NAME "python_dap"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    )

    # Install library
    install(TARGETS python_dap
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/c_tests)
endif()
