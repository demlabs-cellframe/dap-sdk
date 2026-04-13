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
        ${DAP_SDK_ROOT}/core/src/unix
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
        
        # Transport modules
        ${DAP_SDK_ROOT}/net/transport/include
        ${DAP_SDK_ROOT}/net/transport/http/include
        ${DAP_SDK_ROOT}/net/transport/udp/include
        ${DAP_SDK_ROOT}/net/transport/websocket/include
        
        # Global DB
        ${DAP_SDK_ROOT}/global-db/include
        
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
    
    # On macOS, auto-register constructors from static libraries are stripped by ld64
    # unless we force-load the libraries that contain them.
    # DAP_MODULE_REGISTERED_TARGETS is populated by dap_register_module() in DapModule.cmake.
    set(_FORCE_LOAD_TARGETS "")
    if(APPLE AND DAP_MODULE_REGISTERED_TARGETS)
        foreach(_MOD_TARGET ${DAP_MODULE_REGISTERED_TARGETS})
            if(TARGET ${_MOD_TARGET}_static)
                list(APPEND _FORCE_LOAD_TARGETS "${_MOD_TARGET}_static")
            elseif(TARGET ${_MOD_TARGET})
                list(APPEND _FORCE_LOAD_TARGETS "${_MOD_TARGET}")
            endif()
        endforeach()
    endif()

    foreach(MODULE ${SDK_MODULES})
        if(TARGET ${MODULE}_static)
            list(FIND _FORCE_LOAD_TARGETS "${MODULE}_static" _FL_IDX)
            if(APPLE AND _FL_IDX GREATER -1)
                target_link_options(${TARGET_NAME} PRIVATE
                    "SHELL:-Wl,-force_load $<TARGET_FILE:${MODULE}_static>")
            else()
                target_link_libraries(${TARGET_NAME} PRIVATE ${MODULE}_static)
            endif()
        else()
            message(WARNING "dap_test_link_libraries: Static library ${MODULE}_static not found, skipping ${MODULE}")
        endif()
    endforeach()

    if(TARGET dap_test)
        target_link_libraries(${TARGET_NAME} PRIVATE dap_test)
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

