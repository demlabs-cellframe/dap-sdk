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
        # Test framework (corrected path to module/test/)
        ${DAP_SDK_ROOT}/module/test
        ${FIXTURES_DIR}
        
        # Core modules
        ${DAP_SDK_ROOT}/module/core/include
        ${DAP_SDK_ROOT}/module/core/src/unix
        ${DAP_SDK_ROOT}/module/crypto/include
        ${DAP_SDK_ROOT}/module/io/include
        
        # Network modules
        ${DAP_SDK_ROOT}/module/net/client/include
        ${DAP_SDK_ROOT}/module/net/stream/stream/include
        ${DAP_SDK_ROOT}/module/net/stream/ch/include
        ${DAP_SDK_ROOT}/module/net/stream/session/include
        ${DAP_SDK_ROOT}/module/net/server/http_server/include
        ${DAP_SDK_ROOT}/module/net/server/http_server/http_client/include
        ${DAP_SDK_ROOT}/module/net/server/enc_server/include
        ${DAP_SDK_ROOT}/module/net/common/include
        ${DAP_SDK_ROOT}/module/net/link_manager/include
        ${DAP_SDK_ROOT}/module/net/client_http/include
        
        # Global DB
        ${DAP_SDK_ROOT}/module/global-db/include
        
        # 3rd party
        ${DAP_SDK_ROOT}/3rdparty/uthash/src
        ${DAP_SDK_ROOT}/3rdparty
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
# IMPORTANT: Uses STATIC libraries (not object files) to enable --wrap for mocking
# --wrap only works with static libraries, NOT with object files added via target_sources
# Usage: dap_test_link_libraries(TARGET_NAME)
function(dap_test_link_libraries TARGET_NAME)
    # Get list of SDK modules from DAP_INTERNAL_MODULES cache variable
    get_property(SDK_MODULES CACHE DAP_INTERNAL_MODULES PROPERTY VALUE)
    
    if(NOT SDK_MODULES)
        message(FATAL_ERROR "dap_test_link_libraries: No modules found in DAP_INTERNAL_MODULES")
    endif()
    
    # Link all SDK modules as STATIC libraries ONLY
    # This is REQUIRED for --wrap to work correctly with mocking
    # Object files added via target_sources or target_link_libraries do NOT work with --wrap
    # Use ${MODULE}_static which are created from object libraries in main CMakeLists.txt
    foreach(MODULE ${SDK_MODULES})
        # ONLY use static version - fail if it doesn't exist
        if(TARGET ${MODULE}_static)
            target_link_libraries(${TARGET_NAME} PRIVATE ${MODULE}_static)
        else()
            message(WARNING "dap_test_link_libraries: Static library ${MODULE}_static not found, skipping ${MODULE}")
        endif()
    endforeach()
    
    # Link test framework if it exists
    if(TARGET dap_test)
        target_link_libraries(${TARGET_NAME} PRIVATE dap_test)
    endif()
    
    # Note: External libraries (sqlite3, json-c, ssl, etc.) are linked transitively
    # through INTERFACE_LINK_LIBRARIES of static library modules
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
        include(${CMAKE_CURRENT_SOURCE_DIR}/../../module/test/mocks/DAPMockAutoWrap.cmake)
        dap_mock_autowrap(${DAP_TEST_TARGET_NAME})
    endif()
    
    # Add to CTest
    add_test(NAME ${DAP_TEST_TARGET_NAME} COMMAND ${DAP_TEST_TARGET_NAME})
    
    message(STATUS "[TEST] Configured: ${DAP_TEST_TARGET_NAME}")
endfunction()

