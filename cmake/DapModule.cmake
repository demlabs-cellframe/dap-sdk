# DapModule.cmake
# Automatic module registration system for DAP SDK
# 
# This CMake module provides automatic registration of DAP modules using
# __attribute__((constructor)) mechanism. When OBJECT libraries are used,
# constructors may not be called automatically, so this system ensures
# modules are registered and initialized via dap_module system.
#
# Usage:
#   In your module's CMakeLists.txt:
#     include(DapModule)
#     dap_register_module(
#         MODULE_NAME "transport_http"
#         VERSION 1
#         INIT_FUNCTION "dap_net_transport_http_stream_register"
#         DEINIT_FUNCTION "dap_net_transport_http_stream_unregister"
#         DEPENDENCIES "dap_stream_transport"
#     )
#
#   This will generate a C source file that registers the module automatically.

# =========================================
# DAP_REGISTER_MODULE
# =========================================
# Registers a DAP module with automatic initialization
#
# Parameters:
#   MODULE_NAME - Unique module name (required)
#   VERSION - Module version number (required)
#   INIT_FUNCTION - Function to call during initialization (required)
#   DEINIT_FUNCTION - Function to call during deinitialization (optional)
#   DEPENDENCIES - Comma-separated list of dependency module names (optional)
#   INCLUDE_HEADER - Header file to include for function declarations (optional, auto-detected if not provided)
#
function(dap_register_module)
    cmake_parse_arguments(
        DAP_MODULE
        ""
        "TARGET;MODULE_NAME;VERSION;INIT_FUNCTION;DEINIT_FUNCTION;DEPENDENCIES;INCLUDE_HEADER"
        ""
        ${ARGN}
    )
    
    if(NOT DAP_MODULE_MODULE_NAME)
        message(FATAL_ERROR "dap_register_module: MODULE_NAME is required")
    endif()
    
    if(NOT DAP_MODULE_VERSION)
        message(FATAL_ERROR "dap_register_module: VERSION is required")
    endif()
    
    if(NOT DAP_MODULE_INIT_FUNCTION)
        message(FATAL_ERROR "dap_register_module: INIT_FUNCTION is required")
    endif()
    
    # Determine target name
    if(DAP_MODULE_TARGET)
        set(CURRENT_TARGET ${DAP_MODULE_TARGET})
    else()
        # Try to get from current directory property (set by create_object_library)
        get_property(CURRENT_TARGET DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY DAP_CURRENT_TARGET)
        if(NOT CURRENT_TARGET)
            # Fallback: use PROJECT_NAME
            set(CURRENT_TARGET ${PROJECT_NAME})
        endif()
    endif()
    
    if(NOT TARGET ${CURRENT_TARGET})
        message(FATAL_ERROR "dap_register_module: Target '${CURRENT_TARGET}' does not exist. Please specify TARGET parameter or ensure create_object_library was called first.")
    endif()
    
    # Ensure dap_core is linked (for dap_module functions)
    if(NOT TARGET dap_core)
        message(WARNING "dap_register_module: dap_core target not found. Module registration may fail.")
    else()
        target_link_libraries(${CURRENT_TARGET} PRIVATE dap_core)
    endif()
    
    # Generate unique source file name based on module name
    string(MAKE_C_IDENTIFIER "${DAP_MODULE_MODULE_NAME}" MODULE_ID)
    set(AUTO_REGISTER_FILE "${CMAKE_CURRENT_BINARY_DIR}/dap_module_${MODULE_ID}_auto_register.c")
    
    # Prepare dependencies string
    if(DAP_MODULE_DEPENDENCIES)
        set(DEPENDENCIES_ARG "\"${DAP_MODULE_DEPENDENCIES}\"")
    else()
        set(DEPENDENCIES_ARG "NULL")
    endif()
    
    # Prepare deinit function wrapper code
    if(DAP_MODULE_DEINIT_FUNCTION)
        set(DEINIT_WRAPPER_CODE "
/**
 * @brief Wrapper for deinit function to match dap_module_callback_deinit_t signature
 * @note dap_module expects void (*)(void), but transport functions use int (*)(void)
 */
static void s_${MODULE_ID}_deinit_wrapper(void)
{
    (void)${DAP_MODULE_DEINIT_FUNCTION}();  // Call and ignore return value
}
")
        set(DEINIT_ARG "s_${MODULE_ID}_deinit_wrapper")
    else()
        set(DEINIT_WRAPPER_CODE "")
        set(DEINIT_ARG "NULL")
    endif()
    
    # Determine include header
    if(DAP_MODULE_INCLUDE_HEADER)
        set(INCLUDE_HEADER_LINE "#include \"${DAP_MODULE_INCLUDE_HEADER}\"")
    else()
        # Try to auto-detect: look for header file matching the init function name
        # For example, dap_net_transport_http_stream_register -> dap_net_transport_http_stream.h
        string(REGEX REPLACE "_register$" "" FUNCTION_BASE "${DAP_MODULE_INIT_FUNCTION}")
        string(REGEX REPLACE "^dap_" "" HEADER_BASE "${FUNCTION_BASE}")
        
        # Try common locations
        set(HEADER_CANDIDATES
            "${CMAKE_CURRENT_SOURCE_DIR}/include/${FUNCTION_BASE}.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/${HEADER_BASE}.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/${FUNCTION_BASE}.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/${HEADER_BASE}.h"
        )
        
        set(FOUND_HEADER "")
        foreach(HEADER_CANDIDATE ${HEADER_CANDIDATES})
            if(EXISTS "${HEADER_CANDIDATE}")
                # Get relative path from CMAKE_CURRENT_SOURCE_DIR
                file(RELATIVE_PATH RELATIVE_HEADER "${CMAKE_CURRENT_SOURCE_DIR}" "${HEADER_CANDIDATE}")
                set(INCLUDE_HEADER_LINE "#include \"${RELATIVE_HEADER}\"")
                set(FOUND_HEADER TRUE)
                break()
            endif()
        endforeach()
        
        if(NOT FOUND_HEADER)
            # Fallback: try to construct include path based on module name
            # For transport modules, use include/<module_name>.h
            set(INCLUDE_HEADER_LINE "// Auto-include: Add #include for ${DAP_MODULE_INIT_FUNCTION} declaration")
        endif()
    endif()
    
    # Generate the auto-registration source file
    file(WRITE "${AUTO_REGISTER_FILE}" 
"/*
 * Auto-generated module registration file for ${DAP_MODULE_MODULE_NAME}
 * Generated by DapModule.cmake
 * DO NOT EDIT - This file is automatically generated
 */

#include \"dap_common.h\"
#include \"dap_module.h\"
${INCLUDE_HEADER_LINE}

#define LOG_TAG \"dap_module_auto_register\"

/**
 * @brief Wrapper for init function to match dap_module_callback_init_t signature
 * @note dap_module expects int (*)(void *, ...), but transport functions use int (*)(void)
 */
static int s_${MODULE_ID}_init_wrapper(void *arg0, ...)
{
    (void)arg0;  // Unused, but required by signature
    return ${DAP_MODULE_INIT_FUNCTION}();
}

${DEINIT_WRAPPER_CODE}
/**
 * @brief Auto-registration constructor for module ${DAP_MODULE_MODULE_NAME}
 * 
 * This function is called automatically when the module is loaded.
 * It registers the module with dap_module system.
 * 
 * When OBJECT libraries are used, constructors may not be called automatically.
 * In that case, dap_module_init_all() will call registered init functions.
 */
__attribute__((constructor))
static void s_${MODULE_ID}_auto_register(void)
{
    // Register module with dap_module system
    // This ensures the module is registered even if constructor isn't called
    int l_ret = dap_module_add(
        \"${DAP_MODULE_MODULE_NAME}\",
        ${DAP_MODULE_VERSION},
        ${DEPENDENCIES_ARG},
        s_${MODULE_ID}_init_wrapper,
        NULL,  // init_args
        ${DEINIT_ARG}
    );
    
    if (l_ret == 0) {
        log_it(L_DEBUG, \"Auto-registered module '${DAP_MODULE_MODULE_NAME}' (version ${DAP_MODULE_VERSION})\");
        
        // Call init function immediately via constructor (priority initialization)
        // This ensures modules are initialized as soon as libraries are loaded
        int l_init_ret = ${DAP_MODULE_INIT_FUNCTION}();
        if (l_init_ret == 0) {
            // Mark module as initialized to prevent duplicate initialization via dap_module_init_all()
            dap_module_mark_initialized(\"${DAP_MODULE_MODULE_NAME}\");
            log_it(L_DEBUG, \"Module '${DAP_MODULE_MODULE_NAME}' initialized via constructor\");
        } else if (l_init_ret == -2) {
            // -2 means already registered (idempotent), treat as success
            dap_module_mark_initialized(\"${DAP_MODULE_MODULE_NAME}\");
            log_it(L_DEBUG, \"Module '${DAP_MODULE_MODULE_NAME}' already registered (idempotent), marked as initialized\");
        } else {
            log_it(L_WARNING, \"Module '${DAP_MODULE_MODULE_NAME}' init function returned error: %d (will be retried via dap_module_init_all())\", l_init_ret);
        }
    } else {
        log_it(L_WARNING, \"Failed to auto-register module '${DAP_MODULE_MODULE_NAME}': %d\", l_ret);
    }
}
")
    
    # Add generated file to current target
    if(TARGET ${CURRENT_TARGET})
        target_sources(${CURRENT_TARGET} PRIVATE "${AUTO_REGISTER_FILE}")
        
        # The generated file will inherit include directories from the target
        # since it's added via target_sources(), so no need to set COMPILE_FLAGS
        
        message(STATUS "[DapModule] Registered module '${DAP_MODULE_MODULE_NAME}' for target ${CURRENT_TARGET}")
    else()
        message(WARNING "[DapModule] Could not find target for module '${DAP_MODULE_MODULE_NAME}', file generated at ${AUTO_REGISTER_FILE}")
    endif()
endfunction()

# =========================================
# DAP_CALL_MODULE_INIT_ALL
# =========================================
# Ensures dap_module_init_all() is called during SDK initialization
# This should be called from the main SDK initialization code
#
# Usage:
#   dap_call_module_init_all(TARGET_NAME)
#
function(dap_call_module_init_all TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "dap_call_module_init_all: Target ${TARGET_NAME} does not exist")
    endif()
    
    # This is a marker that dap_module_init_all() should be called
    # The actual call should be in the application code (dap_sdk_init, etc.)
    set_target_properties(${TARGET_NAME} PROPERTIES
        DAP_MODULE_INIT_REQUIRED TRUE
    )
    
    message(STATUS "[DapModule] Module initialization required for ${TARGET_NAME}")
endfunction()

