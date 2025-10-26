# DAP SDK Mock Auto-Wrapper CMake Integration
# Provides function to automatically generate and apply linker wrapping
# 
# Usage:
#   include(DAPMockAutoWrap.cmake)
#   dap_mock_autowrap(test_my_module test_my_module.c)

# Find bash or powershell for running the generator script
if(UNIX)
    find_program(BASH_EXECUTABLE bash)
    set(SCRIPT_EXECUTOR ${BASH_EXECUTABLE})
    set(SCRIPT_EXT "sh")
elseif(WIN32)
    find_program(POWERSHELL_EXECUTABLE powershell)
    set(SCRIPT_EXECUTOR ${POWERSHELL_EXECUTABLE})
    set(SCRIPT_EXT "ps1")
endif()

if(NOT SCRIPT_EXECUTOR)
    message(WARNING "Neither bash nor PowerShell found - dap_mock_autowrap() will not work")
endif()

#
# dap_mock_autowrap(target_name source_file)
#
# Automatically scans source_file for DAP_MOCK_DECLARE() calls and:
# 1. Generates linker response file with --wrap options
# 2. Generates CMake fragment with configuration
# 3. Applies wrap options to target_name
# 4. Generates wrapper template if needed
#
# Example:
#   add_executable(test_vpn_tun test_vpn_tun.c)
#   dap_mock_autowrap(test_vpn_tun test_vpn_tun.c)
#
function(dap_mock_autowrap TARGET_NAME SOURCE_FILE)
    if(NOT SCRIPT_EXECUTOR)
        message(FATAL_ERROR "Bash or PowerShell required for dap_mock_autowrap()")
    endif()
    
    # Get absolute path to source
    get_filename_component(SOURCE_ABS ${SOURCE_FILE} ABSOLUTE)
    get_filename_component(SOURCE_DIR ${SOURCE_ABS} DIRECTORY)
    get_filename_component(SOURCE_BASENAME ${SOURCE_ABS} NAME_WE)
    
    # Output files go to build directory (CMAKE_CURRENT_BINARY_DIR)
    # This keeps generated files separate from source files
    set(MOCK_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/mock_gen")
    file(MAKE_DIRECTORY ${MOCK_GEN_DIR})
    
    set(WRAP_RESPONSE_FILE "${MOCK_GEN_DIR}/${SOURCE_BASENAME}_wrap.txt")
    set(CMAKE_FRAGMENT "${MOCK_GEN_DIR}/${SOURCE_BASENAME}_mocks.cmake")
    set(WRAPPER_TEMPLATE "${MOCK_GEN_DIR}/${SOURCE_BASENAME}_wrappers_template.c")
    
    # Path to generator script
    set(GENERATOR_SCRIPT "${CMAKE_SOURCE_DIR}/cellframe-node/dap-sdk/test-framework/mocks/dap_mock_autowrap.${SCRIPT_EXT}")
    
    if(NOT EXISTS ${GENERATOR_SCRIPT})
        message(FATAL_ERROR "Mock generator script not found: ${GENERATOR_SCRIPT}")
    endif()
    
    # Custom command to run generator
    # Pass MOCK_GEN_DIR as output directory to script
    if(UNIX)
        add_custom_command(
            OUTPUT ${WRAP_RESPONSE_FILE} ${CMAKE_FRAGMENT}
            COMMAND ${SCRIPT_EXECUTOR} ${GENERATOR_SCRIPT} ${SOURCE_ABS} ${MOCK_GEN_DIR}
            DEPENDS ${SOURCE_ABS}
            COMMENT "Generating mock wrappers for ${TARGET_NAME} in ${MOCK_GEN_DIR}"
            VERBATIM
        )
    elseif(WIN32)
        add_custom_command(
            OUTPUT ${WRAP_RESPONSE_FILE} ${CMAKE_FRAGMENT}
            COMMAND ${SCRIPT_EXECUTOR} -ExecutionPolicy Bypass -File ${GENERATOR_SCRIPT} ${SOURCE_ABS} ${MOCK_GEN_DIR}
            DEPENDS ${SOURCE_ABS}
            COMMENT "Generating mock wrappers for ${TARGET_NAME} in ${MOCK_GEN_DIR}"
            VERBATIM
        )
    endif()
    
    # Add as dependency
    add_custom_target(${TARGET_NAME}_mock_gen
        DEPENDS ${WRAP_RESPONSE_FILE} ${CMAKE_FRAGMENT}
    )
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}_mock_gen)
    
    # Read wrap options from generated file
    if(EXISTS ${WRAP_RESPONSE_FILE})
        # Detect if compiler supports response files
        if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR
           CMAKE_C_COMPILER_ID MATCHES "Clang" OR
           CMAKE_C_COMPILER_ID MATCHES "AppleClang")
            # GCC and Clang support -Wl,@file for response files
            # This is much cleaner than reading and parsing the file
            target_link_options(${TARGET_NAME} PRIVATE "-Wl,@${WRAP_RESPONSE_FILE}")
            message(STATUS "✅ Mock autowrap enabled for ${TARGET_NAME} (via @file)")
            message(STATUS "   Generated files in: ${MOCK_GEN_DIR}")
        else()
            # Fallback: read file and add options individually
            file(READ ${WRAP_RESPONSE_FILE} WRAP_OPTIONS_CONTENT)
            string(REPLACE "\n" ";" WRAP_OPTIONS_LIST "${WRAP_OPTIONS_CONTENT}")
            target_link_options(${TARGET_NAME} PRIVATE ${WRAP_OPTIONS_LIST})
            message(STATUS "✅ Mock autowrap enabled for ${TARGET_NAME}")
            message(STATUS "   Generated files in: ${MOCK_GEN_DIR}")
        endif()

        message(STATUS "   Wrap options: ${WRAP_RESPONSE_FILE}")
    else()
        message(STATUS "⏳ Mock autowrap will generate: ${WRAP_RESPONSE_FILE}")
        message(STATUS "   Output directory: ${MOCK_GEN_DIR}")
    endif()

    # Show template location if it will be generated
    if(NOT EXISTS ${WRAPPER_TEMPLATE})
        message(STATUS "   Wrapper template will be generated if needed in ${MOCK_GEN_DIR}")
    endif()
endfunction()

#
# dap_mock_manual_wrap(target_name function1 function2 ...)
#
# Manually specify functions to wrap (if you don't want auto-detection)
#
# Example:
#   dap_mock_manual_wrap(test_vpn 
#       dap_stream_write
#       dap_net_tun_create
#       dap_config_get_item_str
#   )
#
function(dap_mock_manual_wrap TARGET_NAME)
    set(WRAP_OPTIONS "")
    
    # Detect compiler and linker type
    if(CMAKE_C_COMPILER_ID MATCHES "MSVC" OR CMAKE_C_SIMULATE_ID MATCHES "MSVC")
        # MSVC does not support --wrap, use /ALTERNATENAME instead
        message(WARNING "MSVC does not support --wrap. Please use MinGW/Clang for mock testing.")
        message(WARNING "Alternative: Use /ALTERNATENAME:_function_name=_mock_function_name")
        foreach(FUNC ${ARGN})
            list(APPEND WRAP_OPTIONS "/ALTERNATENAME:_${FUNC}=_mock_${FUNC}")
        endforeach()
    else()
        # GCC, Clang, MinGW - all support GNU ld --wrap
        foreach(FUNC ${ARGN})
            list(APPEND WRAP_OPTIONS "-Wl,--wrap=${FUNC}")
        endforeach()
    endif()
    
    target_link_options(${TARGET_NAME} PRIVATE ${WRAP_OPTIONS})
    
    list(LENGTH ARGN FUNC_COUNT)
    message(STATUS "✅ Manual mock wrap: ${FUNC_COUNT} functions for ${TARGET_NAME}")
endfunction()

#
# dap_mock_wrap_from_file(target_name wrap_file)
#
# Apply wrap options from a text file (one function per line)
#
# Example:
#   dap_mock_wrap_from_file(test_vpn mocks/vpn_wraps.txt)
#
function(dap_mock_wrap_from_file TARGET_NAME WRAP_FILE)
    get_filename_component(WRAP_FILE_ABS ${WRAP_FILE} ABSOLUTE)
    
    if(NOT EXISTS ${WRAP_FILE_ABS})
        message(FATAL_ERROR "Wrap file not found: ${WRAP_FILE_ABS}")
    endif()
    
    # Detect if compiler supports response files directly
    if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR 
       CMAKE_C_COMPILER_ID MATCHES "Clang" OR
       CMAKE_C_COMPILER_ID MATCHES "AppleClang")
        # GCC/Clang: use -Wl,@file directly (most efficient)
        target_link_options(${TARGET_NAME} PRIVATE "-Wl,@${WRAP_FILE_ABS}")
        message(STATUS "✅ Mock wrap from file: ${WRAP_FILE} (via @file)")
        return()
    endif()
    
    # Fallback: parse file manually for other compilers
    file(READ ${WRAP_FILE_ABS} WRAP_CONTENT)
    string(REPLACE "\n" ";" WRAP_LINES "${WRAP_CONTENT}")
    
    set(WRAP_OPTIONS "")
    set(FUNC_LIST "")
    
    foreach(LINE ${WRAP_LINES})
        string(STRIP "${LINE}" LINE_TRIMMED)
        if(LINE_TRIMMED AND NOT LINE_TRIMMED MATCHES "^#")
            string(REGEX REPLACE "^-Wl,--wrap=" "" FUNC_NAME "${LINE_TRIMMED}")
            list(APPEND FUNC_LIST ${FUNC_NAME})
        endif()
    endforeach()
    
    # Build options based on compiler
    if(CMAKE_C_COMPILER_ID MATCHES "MSVC" OR CMAKE_C_SIMULATE_ID MATCHES "MSVC")
        message(WARNING "MSVC does not support --wrap. Please use MinGW/Clang for mock testing.")
        foreach(FUNC ${FUNC_LIST})
            list(APPEND WRAP_OPTIONS "/ALTERNATENAME:_${FUNC}=_mock_${FUNC}")
        endforeach()
    else()
        foreach(FUNC ${FUNC_LIST})
            list(APPEND WRAP_OPTIONS "-Wl,--wrap=${FUNC}")
        endforeach()
    endif()
    
    target_link_options(${TARGET_NAME} PRIVATE ${WRAP_OPTIONS})
    
    list(LENGTH WRAP_OPTIONS FUNC_COUNT)
    message(STATUS "✅ Mock wrap from file: ${FUNC_COUNT} functions from ${WRAP_FILE}")
endfunction()

# Print helpful info
message(STATUS "DAP Mock AutoWrap CMake module loaded")
message(STATUS "  Functions available:")
message(STATUS "    - dap_mock_autowrap(target source)")
message(STATUS "    - dap_mock_manual_wrap(target func1 func2 ...)")
message(STATUS "    - dap_mock_wrap_from_file(target wrap_file)")

