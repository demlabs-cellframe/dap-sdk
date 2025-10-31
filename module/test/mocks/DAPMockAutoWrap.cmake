# DAP SDK Mock Auto-Wrapper CMake Integration
# Provides function to automatically generate and apply linker wrapping
# 
# Usage:
#   include(DAPMockAutoWrap.cmake)
#   dap_mock_autowrap(test_my_module test_my_module.c)

# Save the directory where this module is located
set(DAP_MOCK_AUTOWRAP_MODULE_DIR ${CMAKE_CURRENT_LIST_DIR})

# Find bash or powershell for running the generator script
if(UNIX)
    find_program(BASH_EXECUTABLE bash)
    if(BASH_EXECUTABLE)
        set(SCRIPT_EXECUTOR ${BASH_EXECUTABLE} CACHE INTERNAL "Script executor for mock autowrap")
        set(SCRIPT_EXT "sh" CACHE INTERNAL "Script extension for mock autowrap")
    endif()
elseif(WIN32)
    find_program(POWERSHELL_EXECUTABLE powershell)
    if(POWERSHELL_EXECUTABLE)
        set(SCRIPT_EXECUTOR ${POWERSHELL_EXECUTABLE} CACHE INTERNAL "Script executor for mock autowrap")
        set(SCRIPT_EXT "ps1" CACHE INTERNAL "Script extension for mock autowrap")
    endif()
endif()

if(NOT SCRIPT_EXECUTOR)
    message(WARNING "Neither bash nor PowerShell found - dap_mock_autowrap() will not work")
endif()

#
# dap_mock_autowrap(target_name)
#
# Automatically scans ALL source files of target for DAP_MOCK_DECLARE() calls and:
# 1. Generates linker response file with --wrap options
# 2. Generates CMake fragment with configuration
# 3. Applies wrap options to target_name
# 4. Generates wrapper template if needed
#
# Example:
#   add_executable(test_vpn_tun test_vpn_tun.c test_mocks.c)
#   dap_mock_autowrap(test_vpn_tun)
#
function(dap_mock_autowrap TARGET_NAME)
    # Check if script executor is available (CACHE variables are accessible in functions)
    if(NOT SCRIPT_EXECUTOR)
        # Try to find bash/powershell again in case variable wasn't set
        if(UNIX)
            find_program(BASH_EXECUTABLE bash)
            if(BASH_EXECUTABLE)
                set(SCRIPT_EXECUTOR ${BASH_EXECUTABLE} CACHE INTERNAL "Script executor for mock autowrap")
                set(SCRIPT_EXT "sh" CACHE INTERNAL "Script extension for mock autowrap")
            endif()
        elseif(WIN32)
            find_program(POWERSHELL_EXECUTABLE powershell)
            if(POWERSHELL_EXECUTABLE)
                set(SCRIPT_EXECUTOR ${POWERSHELL_EXECUTABLE} CACHE INTERNAL "Script executor for mock autowrap")
                set(SCRIPT_EXT "ps1" CACHE INTERNAL "Script extension for mock autowrap")
            endif()
        endif()
    endif()
    
    if(NOT SCRIPT_EXECUTOR)
        message(FATAL_ERROR "Bash or PowerShell required for dap_mock_autowrap(). Please install bash (Unix) or PowerShell (Windows)")
    endif()
    
    # Get all source files from the target
    get_target_property(TARGET_SOURCES ${TARGET_NAME} SOURCES)
    if(NOT TARGET_SOURCES)
        #message(WARNING "No sources found for target ${TARGET_NAME}")
        return()
    endif()
    
    # Collect all .c and .h files
    set(ALL_SOURCES "")
    foreach(SOURCE_FILE ${TARGET_SOURCES})
        get_filename_component(SOURCE_ABS ${SOURCE_FILE} ABSOLUTE)
        get_filename_component(SOURCE_EXT ${SOURCE_ABS} EXT)
        if(SOURCE_EXT MATCHES "\\.(c|h)$")
            list(APPEND ALL_SOURCES ${SOURCE_ABS})
        endif()
    endforeach()
    
    if(NOT ALL_SOURCES)
        #message(WARNING "No C/H sources found for target ${TARGET_NAME}")
        return()
    endif()
    
    # Use target name as basename for output files
    set(SOURCE_BASENAME ${TARGET_NAME})
    
    # Output files go to build directory (CMAKE_CURRENT_BINARY_DIR)
    # This keeps generated files separate from source files
    set(MOCK_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/mock_gen")
    file(MAKE_DIRECTORY ${MOCK_GEN_DIR})
    
    set(WRAP_RESPONSE_FILE "${MOCK_GEN_DIR}/${SOURCE_BASENAME}_wrap.txt")
    set(CMAKE_FRAGMENT "${MOCK_GEN_DIR}/${SOURCE_BASENAME}_mocks.cmake")
    set(WRAPPER_TEMPLATE "${MOCK_GEN_DIR}/${SOURCE_BASENAME}_wrappers_template.c")
    
    # Path to generator script (use saved module directory)
    set(GENERATOR_SCRIPT "${DAP_MOCK_AUTOWRAP_MODULE_DIR}/dap_mock_autowrap.${SCRIPT_EXT}")
    
    if(NOT EXISTS ${GENERATOR_SCRIPT})
        message(FATAL_ERROR "Mock generator script not found: ${GENERATOR_SCRIPT}")
    endif()
    
    # STAGE 1: Generate wrap file at configure time
    #message(STATUS "🔧 Generating mock wrappers for ${TARGET_NAME}...")
    #message(STATUS "   Scanning ${list_length_result} source files...")
    
    execute_process(
        COMMAND ${SCRIPT_EXECUTOR} ${GENERATOR_SCRIPT} ${MOCK_GEN_DIR} ${SOURCE_BASENAME} ${ALL_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE MOCK_GEN_RESULT
        OUTPUT_VARIABLE MOCK_GEN_OUTPUT
        ERROR_VARIABLE MOCK_GEN_ERROR
    )
    
    if(NOT MOCK_GEN_RESULT EQUAL 0)
        message(WARNING "Mock generator failed: ${MOCK_GEN_ERROR}")
        message(STATUS "   This is not fatal - mock wrapping will be disabled")
        return()
    endif()
    
    # STAGE 2: Setup re-generation on source file changes
    add_custom_command(
        OUTPUT ${WRAP_RESPONSE_FILE} ${CMAKE_FRAGMENT}
        COMMAND ${SCRIPT_EXECUTOR} ${GENERATOR_SCRIPT} ${MOCK_GEN_DIR} ${SOURCE_BASENAME} ${ALL_SOURCES}
        DEPENDS ${ALL_SOURCES}
        COMMENT "Regenerating mock wrappers for ${TARGET_NAME}"
        VERBATIM
    )
    
    add_custom_target(${TARGET_NAME}_mock_gen
        DEPENDS ${WRAP_RESPONSE_FILE} ${CMAKE_FRAGMENT}
    )
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}_mock_gen)
    
    # STAGE 3: Apply wrap options (file should exist now)
    if(EXISTS ${WRAP_RESPONSE_FILE})
        # Check if file is empty (no mocks)
        file(READ ${WRAP_RESPONSE_FILE} WRAP_CONTENT)
        string(STRIP "${WRAP_CONTENT}" WRAP_CONTENT_STRIPPED)
        
        # Only apply wrap options if file is not empty
        if(WRAP_CONTENT_STRIPPED)
            # Detect if compiler supports response files
            if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR
               CMAKE_C_COMPILER_ID MATCHES "Clang" OR
               CMAKE_C_COMPILER_ID MATCHES "AppleClang")
                # GCC and Clang support -Wl,@file for response files
                target_link_options(${TARGET_NAME} PRIVATE "-Wl,@${WRAP_RESPONSE_FILE}")
                #message(STATUS "✅ Mock autowrap enabled for ${TARGET_NAME} (via @file)")
            else()
                # Fallback: read file and add options individually
                string(REPLACE "\n" ";" WRAP_OPTIONS_LIST "${WRAP_CONTENT}")
                target_link_options(${TARGET_NAME} PRIVATE ${WRAP_OPTIONS_LIST})
                #message(STATUS "✅ Mock autowrap enabled for ${TARGET_NAME}")
            endif()
            
            # Count wrapped functions
            string(REGEX MATCHALL "--wrap=" WRAP_MATCHES "${WRAP_CONTENT}")
            list(LENGTH WRAP_MATCHES WRAP_COUNT)
            
            if(WRAP_COUNT GREATER 0)
                message(STATUS " Mocked ${WRAP_COUNT} functions")
            endif()
        else()
            # File is empty - don't apply to linker
            #message(STATUS "   No mocks found - empty wrap file (not applied to linker)")
        endif()
        #message(STATUS "   Output: ${MOCK_GEN_DIR}")
    else()
        # File was not generated - create empty stub file (truly empty, no comments)
        # Linker response files cannot contain comments - they are interpreted as options
        file(WRITE ${WRAP_RESPONSE_FILE} "")
        #message(STATUS "   No mocks found for ${TARGET_NAME} - created empty wrap file")
        
        # Don't apply empty file to linker - it will cause errors
        # Empty file is created only for consistency and to avoid file-not-found errors
        #message(STATUS "✅ Mock autowrap enabled for ${TARGET_NAME} (empty wrap file - no mocks)")
    endif()
endfunction()

#
# dap_mock_autowrap_with_static(target_name library1 library2 ...)
#
# Wrap specified static libraries with --whole-archive to make --wrap work
# This forces linker to process all symbols from static libraries
#
# Example:
#   target_link_libraries(test_http dap_core dap_http_server dap_test)
#   dap_mock_autowrap_with_static(test_http dap_http_server)
#
# Result:
#   Links as: dap_core -Wl,--whole-archive dap_http_server -Wl,--no-whole-archive dap_test
#
function(dap_mock_autowrap_with_static TARGET_NAME)
    set(LIBS_TO_WRAP ${ARGN})
    
    if(NOT LIBS_TO_WRAP)
        message(WARNING "dap_mock_autowrap_with_static: No libraries specified")
        return()
    endif()
    
    # Get current link libraries
    get_target_property(CURRENT_LIBS ${TARGET_NAME} LINK_LIBRARIES)
    
    if(NOT CURRENT_LIBS)
        message(WARNING "dap_mock_autowrap_with_static: No libraries linked to ${TARGET_NAME}")
        return()
    endif()
    
    # Rebuild link libraries list with --whole-archive around specified libs
    set(NEW_LIBS "")
    foreach(LIB ${CURRENT_LIBS})
        # Check if this lib should be wrapped
        list(FIND LIBS_TO_WRAP ${LIB} LIB_INDEX)
        if(LIB_INDEX GREATER -1)
            list(APPEND NEW_LIBS "-Wl,--whole-archive")
            list(APPEND NEW_LIBS ${LIB})
            list(APPEND NEW_LIBS "-Wl,--no-whole-archive")
        else()
            list(APPEND NEW_LIBS ${LIB})
        endif()
    endforeach()
    
    # Clear and reset link libraries
    set_target_properties(${TARGET_NAME} PROPERTIES LINK_LIBRARIES "")
    target_link_libraries(${TARGET_NAME} ${NEW_LIBS})
    
    # Add --allow-multiple-definition to handle duplicate symbols from --whole-archive
    target_link_options(${TARGET_NAME} PRIVATE "-Wl,--allow-multiple-definition")
    
    #message(STATUS "✅ Enabled --whole-archive for ${LIBS_TO_WRAP} in ${TARGET_NAME}")
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
        #message(FATAL_ERROR "Wrap file not found: ${WRAP_FILE_ABS}")
    endif()
    
    # Detect if compiler supports response files directly
    if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR 
       CMAKE_C_COMPILER_ID MATCHES "Clang" OR
       CMAKE_C_COMPILER_ID MATCHES "AppleClang")
        # GCC/Clang: use -Wl,@file directly (most efficient)
        target_link_options(${TARGET_NAME} PRIVATE "-Wl,@${WRAP_FILE_ABS}")
        #message(STATUS "✅ Mock wrap from file: ${WRAP_FILE} (via @file)")
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
#message(STATUS "DAP Mock AutoWrap CMake module loaded")

