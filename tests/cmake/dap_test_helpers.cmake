# DAP SDK Test Helpers
# Functions for simplifying DAP SDK test configuration

# Store the directory where this file is located
# This will be used by functions to compute paths
get_filename_component(_DAP_TEST_HELPERS_DIR ${CMAKE_CURRENT_LIST_FILE} DIRECTORY)
get_filename_component(_DAP_TEST_HELPERS_DIR ${_DAP_TEST_HELPERS_DIR} ABSOLUTE)

# =========================================
# FUNCTION: Add all DAP SDK includes
# =========================================
# Adds all necessary include directories for DAP SDK tests
# Usage: dap_test_add_includes(TARGET_NAME)
function(dap_test_add_includes TARGET_NAME)
    # Base DAP SDK paths - use the stored directory from this file location
    get_filename_component(DAP_SDK_ROOT ${_DAP_TEST_HELPERS_DIR}/../.. ABSOLUTE)
    get_filename_component(TESTS_DIR ${_DAP_TEST_HELPERS_DIR}/.. ABSOLUTE)
    get_filename_component(FIXTURES_DIR ${TESTS_DIR}/fixtures ABSOLUTE)
    
    # Add all include directories for DAP SDK modules
    target_include_directories(${TARGET_NAME} PRIVATE
        # Test framework
        ${DAP_SDK_ROOT}/test-framework
        ${FIXTURES_DIR}
        
        # Core modules
        ${DAP_SDK_ROOT}/core/include
        ${DAP_SDK_ROOT}/crypto/include
        ${DAP_SDK_ROOT}/io/include
        
        # Network modules
        ${DAP_SDK_ROOT}/net/client/include
        ${DAP_SDK_ROOT}/net/stream/stream/include
        ${DAP_SDK_ROOT}/net/stream/ch/include
        ${DAP_SDK_ROOT}/net/stream/session/include
        ${DAP_SDK_ROOT}/net/server/http_server/include
        ${DAP_SDK_ROOT}/net/server/http_server/http_client/include
        ${DAP_SDK_ROOT}/net/server/enc_server/include
        ${DAP_SDK_ROOT}/net/common/http/include
        ${DAP_SDK_ROOT}/net/link_manager/include
        
        # Global DB
        ${DAP_SDK_ROOT}/global-db/include
        
        # 3rd party
        ${DAP_SDK_ROOT}/3rdparty/uthash/src
    )
    
    # Get XKCP include directory from dap_crypto_XKCP (if available)
    if(TARGET dap_crypto_XKCP)
        get_target_property(XKCP_INCLUDE_DIR dap_crypto_XKCP INTERFACE_INCLUDE_DIRECTORIES)
        if(XKCP_INCLUDE_DIR)
            target_include_directories(${TARGET_NAME} PRIVATE ${XKCP_INCLUDE_DIR})
        endif()
    endif()
endfunction()

# =========================================
# FUNCTION: Link all DAP SDK libraries for tests
# =========================================
# Links all necessary DAP SDK libraries for tests
# Uses the combined object library dap_sdk_object if available
# Usage: dap_test_link_libraries(TARGET_NAME)
function(dap_test_link_libraries TARGET_NAME)
    # Check if combined object library exists
    if(TARGET dap_sdk_object)
        # Use combined object library - all functions in one place
        # This allows --wrap to work for internal calls
        target_link_libraries(${TARGET_NAME} PRIVATE dap_sdk_object)
        message(STATUS "[TEST] ${TARGET_NAME}: Using dap_sdk_object (--wrap will work for internal calls)")
    else()
        # Fallback: use individual object libraries
        target_link_libraries(${TARGET_NAME} PRIVATE
            dap_core
            dap_crypto
            dap_io
            dap_stream
            dap_stream_ch
            dap_session
            dap_http_server
            dap_http_common
            dap_enc_server
            dap_client
            dap_link_manager
            dap_global_db
            dap_test
        )
        message(STATUS "[TEST] ${TARGET_NAME}: Using individual OBJECT libraries")
    endif()
    
    # External dependencies (if needed)
    if(TARGET dap_json-c)
        target_link_libraries(${TARGET_NAME} PRIVATE dap_json-c)
    endif()
    
    # System libraries
    if(NOT ANDROID)
        target_link_libraries(${TARGET_NAME} PRIVATE pthread)
    endif()
    if(UNIX AND NOT ANDROID AND NOT DARWIN)
        target_link_libraries(${TARGET_NAME} PRIVATE rt)
    endif()
endfunction()

# =========================================
# FUNCTION: Setup DAP SDK test
# =========================================
# Comprehensive function for test setup: creates executable, links libraries and includes
# Usage: dap_test_setup(
#            TARGET_NAME "test_name"
#            SOURCES source1.c source2.c
#            [MOCK_WRAP]  # Enable automatic mock wrapping
#        )
function(dap_test_setup)
    cmake_parse_arguments(
        DAP_TEST
        "MOCK_WRAP"
        "TARGET_NAME"
        "SOURCES"
        ${ARGN}
    )
    
    if(NOT DAP_TEST_TARGET_NAME)
        message(FATAL_ERROR "dap_test_setup: TARGET_NAME is required")
    endif()
    
    if(NOT DAP_TEST_SOURCES)
        message(FATAL_ERROR "dap_test_setup: SOURCES is required")
    endif()
    
    # Create executable
    add_executable(${DAP_TEST_TARGET_NAME} ${DAP_TEST_SOURCES})
    
    # Link libraries
    dap_test_link_libraries(${DAP_TEST_TARGET_NAME})
    
    # Add includes
    dap_test_add_includes(${DAP_TEST_TARGET_NAME})
    
    # Automatic mock wrapping (if enabled)
    if(DAP_TEST_MOCK_WRAP)
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework/mocks/DAPMockAutoWrap.cmake)
        dap_mock_autowrap(${DAP_TEST_TARGET_NAME})
    endif()
    
    # Add to CTest
    add_test(NAME ${DAP_TEST_TARGET_NAME} COMMAND ${DAP_TEST_TARGET_NAME})
    
    message(STATUS "[TEST] Configured: ${DAP_TEST_TARGET_NAME}")
endfunction()

