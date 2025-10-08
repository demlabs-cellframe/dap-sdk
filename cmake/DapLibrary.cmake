# DAP SDK Library Creation Macros
# Based on release-6.0 approach: OBJECT modules → final shared library
# Uses universal LibraryHelpers.cmake for cross-SDK compatibility
# Author: QEVM Team
# Date: 2025-10-07

# Include universal helpers
include(${CMAKE_CURRENT_LIST_DIR}/LibraryHelpers.cmake)

# =========================================
# DAP SDK SPECIFIC WRAPPERS
# =========================================
# Wrapper around universal create_object_library for DAP SDK
# Automatically registers module headers for installation
# Usage: dap_add_library(target_name sources... HEADERS headers... [INSTALL_HEADERS include/])
macro(dap_add_library TARGET_NAME)
    cmake_parse_arguments(DAP_MOD "" "" "HEADERS;INSTALL_HEADERS" ${ARGN})
    
    # Create object library
    create_object_library(${TARGET_NAME} DAP_INTERNAL_MODULES ${DAP_MOD_UNPARSED_ARGUMENTS} HEADERS ${DAP_MOD_HEADERS})
    
    # Auto-register headers for installation if INSTALL_HEADERS specified
    if(DEFINED DAP_MOD_INSTALL_HEADERS)
        foreach(HDR_DIR ${DAP_MOD_INSTALL_HEADERS})
            # Convert relative to absolute
            if(NOT IS_ABSOLUTE ${HDR_DIR})
                set(HDR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${HDR_DIR})
            endif()
            
            # Register for component-based installation
            register_module_headers(DIRECTORIES ${HDR_DIR})
        endforeach()
        
        message(STATUS "[SDK] Module ${TARGET_NAME}: headers registered for installation")
    endif()
endmacro()

# =========================================
# FINAL LIBRARY CREATION
# =========================================
# Wrapper around universal create_final_shared_library for DAP SDK
# Call this AFTER all dap_add_library() calls
macro(dap_create_final_library)
    # Collect 3rd party libraries that need to be linked
    set(DAP_LINK_LIBS "")
    if(TARGET dap_json-c)
        list(APPEND DAP_LINK_LIBS dap_json-c)
    endif()
    if(TARGET mdbx-static)
        list(APPEND DAP_LINK_LIBS mdbx-static)
    endif()
    if(TARGET dap_crypto_XKCP)
        list(APPEND DAP_LINK_LIBS dap_crypto_XKCP)
    endif()
    if(TARGET dap_crypto_kyber512)
        list(APPEND DAP_LINK_LIBS dap_crypto_kyber512)
    endif()
    if(TARGET secp256k1)
        list(APPEND DAP_LINK_LIBS secp256k1)
    endif()
    
    # Add dap_sdk.c as additional source (contains init/deinit functions)
    set(DAP_SDK_EXTRA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/dap_sdk.c")
    
    create_final_shared_library(
        LIBRARY_NAME "dap-sdk"
        MODULE_LIST_VAR DAP_INTERNAL_MODULES
        VERSION ${DAP_SDK_VERSION}
        VERSION_MAJOR ${DAP_SDK_VERSION_MAJOR}
        LINK_LIBRARIES ${DAP_LINK_LIBS}
        ADDITIONAL_SOURCES ${DAP_SDK_EXTRA_SOURCES}
    )
endmacro()
