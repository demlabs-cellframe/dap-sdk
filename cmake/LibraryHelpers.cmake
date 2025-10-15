# Universal Library Creation Helpers
# Reusable functions for DAP SDK, Cellframe SDK, and QEVM Plugin
# Author: QEVM Team
# Date: 2025-10-07

# =========================================
# PLATFORM-SPECIFIC LIBRARY NAME
# =========================================
# Returns the platform-specific library name
# Usage: get_library_filename(OUTPUT_VAR library_base_name)
# Example: get_library_filename(LIB_NAME "dap-sdk")
#   Linux:   libdap-sdk.so
#   macOS:   libdap-sdk.dylib
#   Windows: dap-sdk.dll
function(get_library_filename OUTPUT_VAR LIB_BASE_NAME)
    if(WIN32)
        set(${OUTPUT_VAR} "${LIB_BASE_NAME}.dll" PARENT_SCOPE)
    elseif(APPLE)
        set(${OUTPUT_VAR} "lib${LIB_BASE_NAME}.dylib" PARENT_SCOPE)
    else() # Linux and other Unix
        set(${OUTPUT_VAR} "lib${LIB_BASE_NAME}.so" PARENT_SCOPE)
    endif()
endfunction()

# =========================================
# GENERIC OBJECT LIBRARY CREATION
# =========================================
# Creates an OBJECT library for combining into final shared library
# Usage: create_object_library(target_name MODULE_LIST_VAR sources... HEADERS headers...)
# 
# Parameters:
#   target_name       - Name of the library target
#   MODULE_LIST_VAR   - Variable name to append this module to (e.g., "DAP_INTERNAL_MODULES")
#   sources...        - Source files
#   HEADERS headers   - Header files (optional)
macro(create_object_library TARGET_NAME MODULE_LIST_VAR)
    cmake_parse_arguments(OBJ_LIB "" "" "HEADERS" ${ARGN})
    
    # Create OBJECT library
    add_library(${TARGET_NAME} OBJECT ${OBJ_LIB_UNPARSED_ARGUMENTS} ${OBJ_LIB_HEADERS})
    
    # Enable position independent code for shared library
    set_property(TARGET ${TARGET_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
    
    # Track module in the provided list variable
    if(NOT DEFINED ${MODULE_LIST_VAR})
        set(${MODULE_LIST_VAR} "" CACHE INTERNAL "List of object modules for ${TARGET_NAME}")
    endif()
    list(APPEND ${MODULE_LIST_VAR} ${TARGET_NAME})
    set(${MODULE_LIST_VAR} ${${MODULE_LIST_VAR}} CACHE INTERNAL "List of object modules")
    
    message(STATUS "[SDK] Module: ${TARGET_NAME} (OBJECT)")
endmacro()

# =========================================
# GENERIC FINAL LIBRARY CREATION
# =========================================
# Creates final shared library from OBJECT modules
# Usage: create_final_shared_library(
#            LIBRARY_NAME "dap-sdk"
#            MODULE_LIST_VAR DAP_INTERNAL_MODULES
#            VERSION "2.4.0"
#            VERSION_MAJOR 2
#            [LINK_LIBRARIES lib1 lib2 ...]
#        )
function(create_final_shared_library)
    cmake_parse_arguments(
        FINAL_LIB
        ""
        "LIBRARY_NAME;MODULE_LIST_VAR;VERSION;VERSION_MAJOR"
        "LINK_LIBRARIES;ADDITIONAL_SOURCES"
        ${ARGN}
    )
    
    if(NOT DEFINED ${FINAL_LIB_MODULE_LIST_VAR})
        message(FATAL_ERROR "No modules registered in ${FINAL_LIB_MODULE_LIST_VAR}! Call create_object_library() first.")
    endif()
    
    # Get library filename for current platform
    get_library_filename(LIB_FILENAME ${FINAL_LIB_LIBRARY_NAME})
    
    message(STATUS "========================================")
    if(BUILD_SHARED)
        message(STATUS "[SDK] Creating final SHARED library: ${FINAL_LIB_LIBRARY_NAME}")
    else()
        message(STATUS "[SDK] Creating final STATIC library: ${FINAL_LIB_LIBRARY_NAME}")
    endif()
    message(STATUS "[SDK] OBJECT modules: ${${FINAL_LIB_MODULE_LIST_VAR}}")
    if(FINAL_LIB_ADDITIONAL_SOURCES)
        message(STATUS "[SDK] Additional sources: ${FINAL_LIB_ADDITIONAL_SOURCES}")
    endif()
    message(STATUS "========================================")
    
    # Collect all object files
    set(ALL_OBJECTS "")
    foreach(MODULE ${${FINAL_LIB_MODULE_LIST_VAR}})
        if(TARGET ${MODULE})
            list(APPEND ALL_OBJECTS $<TARGET_OBJECTS:${MODULE}>)
        else()
            message(WARNING "[SDK] Module ${MODULE} is registered but target does not exist")
        endif()
    endforeach()
    
    # Create final library (SHARED or STATIC based on BUILD_SHARED option)
    # CMake doesn't allow hyphens in target names, so use underscores for target
    # but set OUTPUT_NAME to the desired name with hyphens
    string(REPLACE "-" "_" TARGET_NAME "${FINAL_LIB_LIBRARY_NAME}")
    
    # Determine library type
    if(BUILD_SHARED)
        set(LIB_TYPE "SHARED")
        message(STATUS "[LibraryHelpers] Creating SHARED library target: ${TARGET_NAME}")
    else()
        set(LIB_TYPE "STATIC")
        message(STATUS "[LibraryHelpers] Creating STATIC library target: ${TARGET_NAME}")
    endif()
    
    message(STATUS "[LibraryHelpers] Target name: ${TARGET_NAME} with OUTPUT_NAME: ${FINAL_LIB_LIBRARY_NAME}")
    add_library(${TARGET_NAME} ${LIB_TYPE} ${ALL_OBJECTS} ${FINAL_LIB_ADDITIONAL_SOURCES})
    
    # If we have additional sources, inherit include directories from all modules
    if(FINAL_LIB_ADDITIONAL_SOURCES)
        foreach(MODULE ${${FINAL_LIB_MODULE_LIST_VAR}})
            if(TARGET ${MODULE})
                # Get INTERFACE_INCLUDE_DIRECTORIES from OBJECT library
                get_target_property(MODULE_INCLUDES ${MODULE} INTERFACE_INCLUDE_DIRECTORIES)
                if(MODULE_INCLUDES)
                    target_include_directories(${TARGET_NAME} PRIVATE ${MODULE_INCLUDES})
                endif()
            endif()
        endforeach()
    endif()
    
    # Set versioning and output name
    # OUTPUT_NAME should be without 'lib' prefix and without extension
    # CMake will automatically add 'lib' prefix for libraries on Unix
    set_target_properties(${TARGET_NAME} PROPERTIES
        OUTPUT_NAME "${FINAL_LIB_LIBRARY_NAME}"
    )
    
    # VERSION and SOVERSION only for SHARED libraries
    if(BUILD_SHARED AND DEFINED FINAL_LIB_VERSION)
        set_target_properties(${TARGET_NAME} PROPERTIES
            VERSION ${FINAL_LIB_VERSION}
            SOVERSION ${FINAL_LIB_VERSION_MAJOR}
        )
    endif()
    
    # Link dependencies
    if(DEFINED FINAL_LIB_LINK_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PRIVATE ${FINAL_LIB_LINK_LIBRARIES})
    endif()
    
    # Link system libraries
    target_link_libraries(${TARGET_NAME} PUBLIC ${CMAKE_DL_LIBS})
    
    if(UNIX AND NOT APPLE AND NOT ANDROID)
        # Linux: link pthread, math, and realtime libraries
        target_link_libraries(${TARGET_NAME} PUBLIC pthread m rt)
    elseif(ANDROID)
        # Android: pthread is built into libc, only link math and log
        target_link_libraries(${TARGET_NAME} PUBLIC m log)
    elseif(APPLE)
        # macOS: link pthread and required frameworks
        target_link_libraries(${TARGET_NAME} PUBLIC pthread)
        # Link macOS system frameworks (required for network monitoring and system APIs)
        target_link_libraries(${TARGET_NAME} PUBLIC 
            "-framework CoreFoundation"
            "-framework SystemConfiguration"
        )
    elseif(WIN32)
        target_link_libraries(${TARGET_NAME} PUBLIC ws2_32 mswsock)
    endif()
    
    # Export all symbols (needed for plugin system)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR (CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT APPLE AND NOT ANDROID))
        # Linux with GNU or Clang
        target_link_options(${TARGET_NAME} PRIVATE -Wl,--export-dynamic)
    elseif(APPLE)
        # macOS with Apple ld
        target_link_options(${TARGET_NAME} PRIVATE -Wl,-export_dynamic)
    elseif(ANDROID)
        # Android NDK - export-dynamic is supported but may need different flags
        target_link_options(${TARGET_NAME} PRIVATE -Wl,--export-dynamic)
    endif()
    
    # =========================================
    # COLLECT INCLUDE DIRECTORIES FROM MODULES
    # =========================================
    # Automatically collect all PUBLIC/INTERFACE include directories from OBJECT modules
    # This allows consumers (like cellframe-node) to see all headers without manual enumeration
    set(ALL_INCLUDE_DIRS "")
    foreach(MODULE ${${FINAL_LIB_MODULE_LIST_VAR}})
        if(TARGET ${MODULE})
            # Get INTERFACE_INCLUDE_DIRECTORIES from OBJECT library
            get_target_property(MODULE_INCLUDES ${MODULE} INTERFACE_INCLUDE_DIRECTORIES)
            if(MODULE_INCLUDES)
                list(APPEND ALL_INCLUDE_DIRS ${MODULE_INCLUDES})
            endif()
        endif()
    endforeach()
    
    # Remove duplicates
    if(ALL_INCLUDE_DIRS)
        list(REMOVE_DUPLICATES ALL_INCLUDE_DIRS)
        list(LENGTH ALL_INCLUDE_DIRS INCLUDE_COUNT)
        message(STATUS "[SDK] Collected ${INCLUDE_COUNT} unique include directories from modules")
    endif()
    
    # Set include directories for consumers
    # Include directories from modules are already absolute paths (CMAKE_CURRENT_SOURCE_DIR)
    # so we can add them directly for BUILD interface
    if(ALL_INCLUDE_DIRS)
        # Add collected include directories directly (they are absolute paths)
        target_include_directories(${TARGET_NAME} INTERFACE ${ALL_INCLUDE_DIRS})
        message(STATUS "[SDK] Exported ${INCLUDE_COUNT} include directories for consumers")
    else()
        message(WARNING "[SDK] No include directories collected from modules")
    endif()
    
    # Add install interface
    target_include_directories(${TARGET_NAME} INTERFACE
        $<INSTALL_INTERFACE:include/${FINAL_LIB_LIBRARY_NAME}>
    )
    
    message(STATUS "[SDK] Final library configured: ${LIB_FILENAME}")
endfunction()

# =========================================
# INSTALLATION HELPER
# =========================================
# Installs library, headers, and pkg-config profile
# Usage: install_sdk_library(
#            LIBRARY_NAME "dap-sdk"
#            HEADER_DIRECTORIES "core/include" "crypto/include" ...
#            PKGCONFIG_TEMPLATE "dap-sdk.pc.in"
#            [INSTALL_3RDPARTY_HEADERS "3rdparty/uthash/src"]
#        )
function(install_sdk_library)
    cmake_parse_arguments(
        INSTALL_SDK
        ""
        "LIBRARY_NAME;PKGCONFIG_TEMPLATE"
        "HEADER_DIRECTORIES;INSTALL_3RDPARTY_HEADERS"
        ${ARGN}
    )
    
    set(INSTALL_INCLUDEDIR "include/${INSTALL_SDK_LIBRARY_NAME}")
    set(INSTALL_LIBDIR "lib")
    
    # Install the shared library
    install(TARGETS ${INSTALL_SDK_LIBRARY_NAME}
        EXPORT ${INSTALL_SDK_LIBRARY_NAME}_targets
        LIBRARY DESTINATION ${INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${INSTALL_LIBDIR}
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION ${INSTALL_INCLUDEDIR}
    )
    
    # Install headers from specified directories
    if(DEFINED INSTALL_SDK_HEADER_DIRECTORIES)
        foreach(HEADER_DIR ${INSTALL_SDK_HEADER_DIRECTORIES})
            install(DIRECTORY ${HEADER_DIR}/
                DESTINATION ${INSTALL_INCLUDEDIR}
                FILES_MATCHING PATTERN "*.h"
            )
        endforeach()
    endif()
    
    # Install 3rd party headers if specified
    if(DEFINED INSTALL_SDK_INSTALL_3RDPARTY_HEADERS)
        foreach(THIRDPARTY_DIR ${INSTALL_SDK_INSTALL_3RDPARTY_HEADERS})
            get_filename_component(DIR_NAME ${THIRDPARTY_DIR} NAME)
            install(DIRECTORY ${THIRDPARTY_DIR}/
                DESTINATION ${INSTALL_INCLUDEDIR}/${DIR_NAME}
                FILES_MATCHING PATTERN "*.h"
            )
        endforeach()
    endif()
    
    # Install CMake config files
    install(EXPORT ${INSTALL_SDK_LIBRARY_NAME}_targets
        FILE ${INSTALL_SDK_LIBRARY_NAME}_targets.cmake
        NAMESPACE ${INSTALL_SDK_LIBRARY_NAME}::
        DESTINATION ${INSTALL_LIBDIR}/cmake/${INSTALL_SDK_LIBRARY_NAME}
    )
    
    # Generate and install pkg-config file if template provided
    if(DEFINED INSTALL_SDK_PKGCONFIG_TEMPLATE)
        configure_file(
            ${INSTALL_SDK_PKGCONFIG_TEMPLATE}
            ${CMAKE_CURRENT_BINARY_DIR}/${INSTALL_SDK_LIBRARY_NAME}.pc
            @ONLY
        )
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${INSTALL_SDK_LIBRARY_NAME}.pc
            DESTINATION ${INSTALL_LIBDIR}/pkgconfig
        )
    endif()
    
    message(STATUS "[SDK] Installation configured for ${INSTALL_SDK_LIBRARY_NAME}")
endfunction()

