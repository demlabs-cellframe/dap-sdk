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
        "LINK_LIBRARIES"
        ${ARGN}
    )
    
    if(NOT DEFINED ${FINAL_LIB_MODULE_LIST_VAR})
        message(FATAL_ERROR "No modules registered in ${FINAL_LIB_MODULE_LIST_VAR}! Call create_object_library() first.")
    endif()
    
    # Get library filename for current platform
    get_library_filename(LIB_FILENAME ${FINAL_LIB_LIBRARY_NAME})
    
    message(STATUS "========================================")
    message(STATUS "[SDK] Creating final shared library: ${FINAL_LIB_LIBRARY_NAME}")
    message(STATUS "[SDK] OBJECT modules: ${${FINAL_LIB_MODULE_LIST_VAR}}")
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
    
    # Create final shared library
    add_library(${FINAL_LIB_LIBRARY_NAME} SHARED ${ALL_OBJECTS})
    
    # Set versioning
    if(DEFINED FINAL_LIB_VERSION)
        set_target_properties(${FINAL_LIB_LIBRARY_NAME} PROPERTIES
            VERSION ${FINAL_LIB_VERSION}
            SOVERSION ${FINAL_LIB_VERSION_MAJOR}
            OUTPUT_NAME ${FINAL_LIB_LIBRARY_NAME}
        )
    endif()
    
    # Link dependencies
    if(DEFINED FINAL_LIB_LINK_LIBRARIES)
        target_link_libraries(${FINAL_LIB_LIBRARY_NAME} PRIVATE ${FINAL_LIB_LINK_LIBRARIES})
    endif()
    
    # Link system libraries
    target_link_libraries(${FINAL_LIB_LIBRARY_NAME} PUBLIC ${CMAKE_DL_LIBS})
    
    if(UNIX AND NOT APPLE)
        target_link_libraries(${FINAL_LIB_LIBRARY_NAME} PUBLIC pthread m rt)
    elseif(APPLE)
        target_link_libraries(${FINAL_LIB_LIBRARY_NAME} PUBLIC pthread)
    elseif(WIN32)
        target_link_libraries(${FINAL_LIB_LIBRARY_NAME} PUBLIC ws2_32 mswsock)
    endif()
    
    # Export all symbols (needed for plugin system)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_link_options(${FINAL_LIB_LIBRARY_NAME} PRIVATE -Wl,--export-dynamic)
    endif()
    
    # Set include directories for consumers
    target_include_directories(${FINAL_LIB_LIBRARY_NAME} INTERFACE
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

