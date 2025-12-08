# Universal Package Components Configuration
# Elegant solution for splitting runtime and development packages
# Reusable for any SDK project (DAP SDK, Cellframe SDK, QEVM Plugin, etc.)
# Author: QEVM Team
# Date: 2025-10-08
#
# This module configures CPack to generate component-based packages:
# - Runtime component: shared libraries only
# - Development component: headers, pkg-config, cmake configs
# - Documentation component: docs, examples [optional]
#
# Usage:
#   1. Set PACKAGE_BASE_NAME before including this file
#   2. Call setup_package_components(PACKAGE_BASE_NAME "my-sdk" ...)
#   3. Modules register their headers via register_module_headers()
#   4. At the end, call finalize_package_components()

# =========================================
# PACKAGE SETUP FUNCTION
# =========================================
# Configures package metadata and generators
# Usage: setup_package_components(
#            PACKAGE_BASE_NAME "dap-sdk"
#            VERSION "2.4.0"
#            DESCRIPTION "Distributed Application Platform SDK"
#            VENDOR "Demlabs"
#            CONTACT "support@demlabs.net"
#            [DEBUG_SUFFIX "-dbg"]
#            [OS_SUFFIX "debian13_x86_64"]
#        )
function(setup_package_components)
    cmake_parse_arguments(
        PKG
        ""
        "PACKAGE_BASE_NAME;VERSION;DESCRIPTION;VENDOR;CONTACT;DEBUG_SUFFIX;OS_SUFFIX"
        ""
        ${ARGN}
    )
    
    if(NOT DEFINED PKG_PACKAGE_BASE_NAME)
        message(FATAL_ERROR "PACKAGE_BASE_NAME is required for setup_package_components()")
    endif()
    
    # Store in cache for use by other functions
    set(CPACK_PKG_BASE_NAME "${PKG_PACKAGE_BASE_NAME}" CACHE INTERNAL "Base package name")
    set(CPACK_PKG_DEBUG_SUFFIX "${PKG_DEBUG_SUFFIX}" CACHE INTERNAL "Debug suffix")
    set(CPACK_PKG_OS_SUFFIX "${PKG_OS_SUFFIX}" CACHE INTERNAL "OS suffix")
    
    # Set CPack metadata
    set(CPACK_PACKAGE_NAME "${PKG_PACKAGE_BASE_NAME}${PKG_DEBUG_SUFFIX}" PARENT_SCOPE)
    set(CPACK_PACKAGE_VERSION "${PKG_VERSION}" PARENT_SCOPE)
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PKG_DESCRIPTION}" PARENT_SCOPE)
    set(CPACK_PACKAGE_VENDOR "${PKG_VENDOR}" PARENT_SCOPE)
    set(CPACK_PACKAGE_CONTACT "${PKG_CONTACT}" PARENT_SCOPE)
    
    # Set CPack generators based on detected OS
    message(STATUS "[PackageComponents] DAP_OS_NAME: '${DAP_OS_NAME}'")
    if(DAP_OS_NAME MATCHES "debian|ubuntu|mint")
        message(STATUS "[PackageComponents] Setting DEB generator for ${DAP_OS_NAME}")
        set(CPACK_GENERATOR "DEB;TGZ" PARENT_SCOPE)
        set(CPACK_BINARY_DEB "ON" PARENT_SCOPE)
    elseif(DAP_OS_NAME MATCHES "fedora|centos|rhel")
        message(STATUS "[PackageComponents] Setting RPM generator for ${DAP_OS_NAME}")
        set(CPACK_GENERATOR "RPM;TGZ" PARENT_SCOPE)
        set(CPACK_BINARY_RPM "ON" PARENT_SCOPE)
    elseif(DAP_OS_NAME MATCHES "arch|gentoo")
        message(STATUS "[PackageComponents] Setting TGZ generator for ${DAP_OS_NAME}")
        set(CPACK_GENERATOR "TGZ" PARENT_SCOPE)
    else()
        message(STATUS "[PackageComponents] Setting default TGZ generator for ${DAP_OS_NAME}")
        set(CPACK_GENERATOR "TGZ" PARENT_SCOPE)
    endif()
    
    # Define components
    set(CPACK_COMPONENTS_ALL runtime development documentation PARENT_SCOPE)
    
    # Component descriptions
    set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "${PKG_PACKAGE_BASE_NAME} Runtime" PARENT_SCOPE)
    set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "${PKG_PACKAGE_BASE_NAME} Development Files" PARENT_SCOPE)
    set(CPACK_COMPONENT_DOCUMENTATION_DISPLAY_NAME "${PKG_PACKAGE_BASE_NAME} Documentation" PARENT_SCOPE)
    
    set(CPACK_COMPONENT_RUNTIME_DESCRIPTION 
        "${PKG_PACKAGE_BASE_NAME} shared library - required for running applications" PARENT_SCOPE)
    set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION 
        "${PKG_PACKAGE_BASE_NAME} headers, pkg-config, and CMake files - required for development" PARENT_SCOPE)
    set(CPACK_COMPONENT_DOCUMENTATION_DESCRIPTION 
        "${PKG_PACKAGE_BASE_NAME} documentation, examples, and API reference" PARENT_SCOPE)
    
    # Dependencies
    set(CPACK_COMPONENT_DEVELOPMENT_DEPENDS runtime PARENT_SCOPE)
    set(CPACK_COMPONENT_DOCUMENTATION_DEPENDS runtime PARENT_SCOPE)
    
    # Platform-specific dependencies
    if(DAP_OS_NAME MATCHES "debian|ubuntu|mint")
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libssl3" PARENT_SCOPE)
        set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_DEPENDS "libc6-dev, libssl-dev" PARENT_SCOPE)
        set(CPACK_DEBIAN_DOCUMENTATION_PACKAGE_DEPENDS "libc6" PARENT_SCOPE)
    elseif(DAP_OS_NAME MATCHES "fedora|centos|rhel")
        set(CPACK_RPM_PACKAGE_REQUIRES "glibc, openssl-libs" PARENT_SCOPE)
        set(CPACK_RPM_DEVELOPMENT_PACKAGE_REQUIRES "glibc-devel, openssl-devel" PARENT_SCOPE)
        set(CPACK_RPM_DOCUMENTATION_PACKAGE_REQUIRES "glibc" PARENT_SCOPE)
    endif()
    
    # Initialize header directories list
    set(SDK_REGISTERED_HEADER_DIRS "" CACHE INTERNAL "List of header directories to install")
    set(SDK_REGISTERED_3RDPARTY_HEADERS "" CACHE INTERNAL "List of 3rd party headers to install")
    
    # Enable component-based packaging
    set(CPACK_DEB_COMPONENT_INSTALL ON PARENT_SCOPE)
    set(CPACK_DEBIAN_ENABLE_COMPONENT_DEPENDS ON PARENT_SCOPE)
    set(CPACK_RPM_COMPONENT_INSTALL ON PARENT_SCOPE)
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON PARENT_SCOPE)
    
    # Set DEB package file names
    # Format: dap-sdk[-dev|-doc]_OS_SUFFIX[-dbg]
    # Note: Must include .deb extension to override automatic component suffix
    # Note: Component names must be in UPPER CASE for CPack
    set(CPACK_DEBIAN_RUNTIME_FILE_NAME "dap-sdk_${PKG_OS_SUFFIX}${PKG_DEBUG_SUFFIX}.deb" PARENT_SCOPE)
    set(CPACK_DEBIAN_DEVELOPMENT_FILE_NAME "dap-sdk-dev_${PKG_OS_SUFFIX}${PKG_DEBUG_SUFFIX}.deb" PARENT_SCOPE)
    set(CPACK_DEBIAN_DOCUMENTATION_FILE_NAME "dap-sdk-doc_${PKG_OS_SUFFIX}${PKG_DEBUG_SUFFIX}.deb" PARENT_SCOPE)
    
    # Set Archive package file names
    set(CPACK_ARCHIVE_RUNTIME_FILE_NAME "dap-sdk_${PKG_OS_SUFFIX}${PKG_DEBUG_SUFFIX}" PARENT_SCOPE)
    set(CPACK_ARCHIVE_DEVELOPMENT_FILE_NAME "dap-sdk-dev_${PKG_OS_SUFFIX}${PKG_DEBUG_SUFFIX}" PARENT_SCOPE)
    set(CPACK_ARCHIVE_DOCUMENTATION_FILE_NAME "dap-sdk-doc_${PKG_OS_SUFFIX}${PKG_DEBUG_SUFFIX}" PARENT_SCOPE)
    
    message(STATUS "[PackageComponents] Setup: ${PKG_PACKAGE_BASE_NAME} v${PKG_VERSION}")
endfunction()

# =========================================
# DEBIAN PACKAGE CONFIGURATION
# =========================================
function(configure_debian_packages)
    if(NOT CPACK_GENERATOR MATCHES "DEB")
        return()
    endif()
    
    set(PKG_NAME "${CPACK_PKG_BASE_NAME}${CPACK_PKG_DEBUG_SUFFIX}")
    set(OS_SUFFIX "${CPACK_PKG_OS_SUFFIX}")
    
    # Runtime package - use dap-sdk naming (not libdap-sdk)
    set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME "dap-sdk${CPACK_PKG_DEBUG_SUFFIX}" PARENT_SCOPE)
    set(CPACK_DEBIAN_RUNTIME_PACKAGE_SECTION "libs" PARENT_SCOPE)
    set(CPACK_DEBIAN_RUNTIME_PACKAGE_PRIORITY "optional" PARENT_SCOPE)
    set(CPACK_DEBIAN_RUNTIME_PACKAGE_DEPENDS "libc6 (>= 2.17)" PARENT_SCOPE)
    
    # Development package - use dap-sdk-dev naming
    set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_NAME "dap-sdk${CPACK_PKG_DEBUG_SUFFIX}-dev" PARENT_SCOPE)
    set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_SECTION "libdevel" PARENT_SCOPE)
    set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_PRIORITY "optional" PARENT_SCOPE)
    set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_DEPENDS 
        "dap-sdk${CPACK_PKG_DEBUG_SUFFIX} (= \${binary:Version})" PARENT_SCOPE)
    
    # Documentation package - use dap-sdk-doc naming
    set(CPACK_DEBIAN_DOCUMENTATION_PACKAGE_NAME "dap-sdk${CPACK_PKG_DEBUG_SUFFIX}-doc" PARENT_SCOPE)
    set(CPACK_DEBIAN_DOCUMENTATION_PACKAGE_SECTION "doc" PARENT_SCOPE)
    set(CPACK_DEBIAN_DOCUMENTATION_PACKAGE_PRIORITY "optional" PARENT_SCOPE)
    set(CPACK_DEBIAN_DOCUMENTATION_PACKAGE_ARCHITECTURE "all" PARENT_SCOPE)
    
    # Enable component-based packaging for DEB
    set(CPACK_DEB_COMPONENT_INSTALL ON PARENT_SCOPE)
    set(CPACK_DEBIAN_ENABLE_COMPONENT_DEPENDS ON PARENT_SCOPE)
    
    # Set DEB package file names (matching package names: dap-sdk, dap-sdk-dev, dap-sdk-doc)
    set(CPACK_DEB_RUNTIME_FILE_NAME "dap-sdk${CPACK_PKG_DEBUG_SUFFIX}_${OS_SUFFIX}" PARENT_SCOPE)
    set(CPACK_DEB_DEVELOPMENT_FILE_NAME "dap-sdk${CPACK_PKG_DEBUG_SUFFIX}-dev_${OS_SUFFIX}" PARENT_SCOPE)
    set(CPACK_DEB_DOCUMENTATION_FILE_NAME "dap-sdk${CPACK_PKG_DEBUG_SUFFIX}-doc_${OS_SUFFIX}" PARENT_SCOPE)
    
    message(STATUS "[CPack DEB] dap-sdk${CPACK_PKG_DEBUG_SUFFIX}_${OS_SUFFIX}.deb")
    message(STATUS "[CPack DEB] dap-sdk${CPACK_PKG_DEBUG_SUFFIX}-dev_${OS_SUFFIX}.deb")
    message(STATUS "[CPack DEB] dap-sdk${CPACK_PKG_DEBUG_SUFFIX}-doc_${OS_SUFFIX}.deb")
endfunction()

# =========================================
# RPM PACKAGE CONFIGURATION
# =========================================
function(configure_rpm_packages)
    if(NOT CPACK_GENERATOR MATCHES "RPM")
        return()
    endif()
    
    set(PKG_NAME "${CPACK_PKG_BASE_NAME}${CPACK_PKG_DEBUG_SUFFIX}")
    set(OS_SUFFIX "${CPACK_PKG_OS_SUFFIX}")
    
    # Runtime package - follow RPM naming: libfoo
    set(CPACK_RPM_RUNTIME_PACKAGE_NAME "lib${PKG_NAME}" PARENT_SCOPE)
    set(CPACK_RPM_RUNTIME_FILE_NAME "lib${PKG_NAME}_${OS_SUFFIX}.rpm" PARENT_SCOPE)
    set(CPACK_RPM_RUNTIME_PACKAGE_GROUP "System Environment/Libraries" PARENT_SCOPE)
    set(CPACK_RPM_RUNTIME_PACKAGE_LICENSE "Custom" PARENT_SCOPE)
    
    # Development package - follow RPM naming: libfoo-devel
    set(CPACK_RPM_DEVELOPMENT_PACKAGE_NAME "lib${PKG_NAME}-devel" PARENT_SCOPE)
    set(CPACK_RPM_DEVELOPMENT_FILE_NAME "lib${PKG_NAME}-devel_${OS_SUFFIX}.rpm" PARENT_SCOPE)
    set(CPACK_RPM_DEVELOPMENT_PACKAGE_GROUP "Development/Libraries" PARENT_SCOPE)
    set(CPACK_RPM_DEVELOPMENT_PACKAGE_LICENSE "Custom" PARENT_SCOPE)
    set(CPACK_RPM_DEVELOPMENT_PACKAGE_REQUIRES 
        "lib${PKG_NAME} = %{version}-%{release}" PARENT_SCOPE)
    
    # Documentation package
    set(CPACK_RPM_DOCUMENTATION_PACKAGE_NAME "lib${PKG_NAME}-doc" PARENT_SCOPE)
    set(CPACK_RPM_DOCUMENTATION_FILE_NAME "lib${PKG_NAME}-doc_${OS_SUFFIX}.rpm" PARENT_SCOPE)
    set(CPACK_RPM_DOCUMENTATION_PACKAGE_GROUP "Documentation" PARENT_SCOPE)
    set(CPACK_RPM_DOCUMENTATION_PACKAGE_LICENSE "Custom" PARENT_SCOPE)
    set(CPACK_RPM_DOCUMENTATION_PACKAGE_ARCHITECTURE "noarch" PARENT_SCOPE)
    
    set(CPACK_RPM_COMPONENT_INSTALL ON PARENT_SCOPE)
    
    message(STATUS "[CPack RPM] lib${PKG_NAME}_${OS_SUFFIX}.rpm")
    message(STATUS "[CPack RPM] lib${PKG_NAME}-devel_${OS_SUFFIX}.rpm")
    message(STATUS "[CPack RPM] lib${PKG_NAME}-doc_${OS_SUFFIX}.rpm")
endfunction()

# =========================================
# ARCHIVE PACKAGE CONFIGURATION
# =========================================
function(configure_archive_packages)
    if(NOT CPACK_GENERATOR MATCHES "TGZ|TBZ2|ZIP")
        return()
    endif()
    
    set(PKG_NAME "${CPACK_PKG_BASE_NAME}${CPACK_PKG_DEBUG_SUFFIX}")
    set(OS_SUFFIX "${CPACK_PKG_OS_SUFFIX}")
    
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON PARENT_SCOPE)
    set(CPACK_ARCHIVE_RUNTIME_FILE_NAME "dap-sdk${CPACK_PKG_DEBUG_SUFFIX}_${OS_SUFFIX}" PARENT_SCOPE)
    set(CPACK_ARCHIVE_DEVELOPMENT_FILE_NAME "dap-sdk${CPACK_PKG_DEBUG_SUFFIX}-dev_${OS_SUFFIX}" PARENT_SCOPE)
    set(CPACK_ARCHIVE_DOCUMENTATION_FILE_NAME "dap-sdk${CPACK_PKG_DEBUG_SUFFIX}-doc_${OS_SUFFIX}" PARENT_SCOPE)
    
    # Set main package file name
    message(STATUS "[PackageComponents] Setting CPACK_PACKAGE_FILE_NAME to: dap-sdk${CPACK_PKG_DEBUG_SUFFIX}_${OS_SUFFIX}")
    set(CPACK_PACKAGE_FILE_NAME "dap-sdk${CPACK_PKG_DEBUG_SUFFIX}_${OS_SUFFIX}" PARENT_SCOPE)
    
    message(STATUS "[CPack Archive] dap-sdk${CPACK_PKG_DEBUG_SUFFIX}_${OS_SUFFIX}.tar.gz")
    message(STATUS "[CPack Archive] dap-sdk${CPACK_PKG_DEBUG_SUFFIX}-dev_${OS_SUFFIX}.tar.gz")
    message(STATUS "[CPack Archive] dap-sdk${CPACK_PKG_DEBUG_SUFFIX}-doc_${OS_SUFFIX}.tar.gz")
endfunction()

# =========================================
# MODULE HEADER REGISTRATION
# =========================================
# Modules call this to register their headers for installation
# Usage in module's CMakeLists.txt:
#   register_module_headers(
#       DIRECTORIES "include" "src/public"
#       DESTINATION "mymodule"
#   )
function(register_module_headers)
    cmake_parse_arguments(
        HDR
        ""
        "DESTINATION"
        "DIRECTORIES"
        ${ARGN}
    )
    
    if(NOT DEFINED HDR_DIRECTORIES)
        message(WARNING "register_module_headers() called without DIRECTORIES")
        return()
    endif()
    
    # Convert to absolute paths
    set(ABS_DIRS "")
    foreach(DIR ${HDR_DIRECTORIES})
        if(IS_ABSOLUTE ${DIR})
            list(APPEND ABS_DIRS ${DIR})
        else()
            list(APPEND ABS_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/${DIR})
        endif()
    endforeach()
    
    # Store for later installation
    set(SDK_REGISTERED_HEADER_DIRS 
        "${SDK_REGISTERED_HEADER_DIRS};${ABS_DIRS}" 
        CACHE INTERNAL "List of header directories to install")
    
    # Store destination mapping if provided
    if(DEFINED HDR_DESTINATION)
        set(SDK_HEADER_DEST_${ABS_DIRS} "${HDR_DESTINATION}" 
            CACHE INTERNAL "Destination for ${ABS_DIRS}")
    endif()
endfunction()

# =========================================
# 3RD PARTY HEADER REGISTRATION
# =========================================
# Register 3rd party headers that are part of public API
# Usage:
#   register_3rdparty_headers(
#       FILES "3rdparty/uthash/src/uthash.h"
#       DESTINATION "uthash"
#   )
function(register_3rdparty_headers)
    cmake_parse_arguments(
        TPH
        ""
        "DESTINATION"
        "FILES;DIRECTORIES"
        ${ARGN}
    )
    
    set(ITEMS "")
    if(DEFINED TPH_FILES)
        list(APPEND ITEMS ${TPH_FILES})
    endif()
    if(DEFINED TPH_DIRECTORIES)
        list(APPEND ITEMS ${TPH_DIRECTORIES})
    endif()
    
    if(NOT ITEMS)
        return()
    endif()
    
    # Convert to absolute paths and store
    foreach(ITEM ${ITEMS})
        if(NOT IS_ABSOLUTE ${ITEM})
            set(ITEM ${CMAKE_CURRENT_SOURCE_DIR}/${ITEM})
        endif()
        
        list(APPEND SDK_REGISTERED_3RDPARTY_HEADERS ${ITEM})
        
        if(DEFINED TPH_DESTINATION)
            set(SDK_3RDPARTY_DEST_${ITEM} "${TPH_DESTINATION}" 
                CACHE INTERNAL "Destination for ${ITEM}")
        endif()
    endforeach()
    
    set(SDK_REGISTERED_3RDPARTY_HEADERS "${SDK_REGISTERED_3RDPARTY_HEADERS}" 
        CACHE INTERNAL "List of 3rd party headers")
endfunction()

# =========================================
# FINALIZE AND INSTALL
# =========================================
# Call this at the end of main CMakeLists.txt to install all registered components
# Usage: finalize_package_components(
#            LIBRARY_TARGET "dap-sdk"
#            INCLUDE_PREFIX "dap-sdk"
#            [PKGCONFIG_TEMPLATE "dap-sdk.pc.in"]
#            [README_FILE "README.md"]
#            [EXAMPLES_DIR "module/examples"]
#        )
function(finalize_package_components)
    cmake_parse_arguments(
        FIN
        ""
        "LIBRARY_TARGET;INCLUDE_PREFIX;PKGCONFIG_TEMPLATE;README_FILE;EXAMPLES_DIR"
        ""
        ${ARGN}
    )
    
    if(NOT DEFINED FIN_LIBRARY_TARGET)
        message(FATAL_ERROR "LIBRARY_TARGET is required for finalize_package_components()")
    endif()
    if(NOT DEFINED FIN_INCLUDE_PREFIX)
        set(FIN_INCLUDE_PREFIX ${FIN_LIBRARY_TARGET})
    endif()
    
    # Install library (runtime component)
    # Note: Don't use EXPORT here if library links to OBJECT libraries
    # CMake doesn't allow hyphens in target names, so convert to underscores
    string(REPLACE "-" "_" FIN_TARGET_NAME "${FIN_LIBRARY_TARGET}")
    install(TARGETS ${FIN_TARGET_NAME}
        COMPONENT runtime
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
    
    # Install all registered module headers (development component)
    if(SDK_REGISTERED_HEADER_DIRS)
        list(REMOVE_DUPLICATES SDK_REGISTERED_HEADER_DIRS)
        foreach(HEADER_DIR ${SDK_REGISTERED_HEADER_DIRS})
            if(EXISTS ${HEADER_DIR})
                install(DIRECTORY ${HEADER_DIR}/
                    COMPONENT development
                    DESTINATION include/${FIN_INCLUDE_PREFIX}
                    FILES_MATCHING PATTERN "*.h"
                )
            endif()
        endforeach()
        message(STATUS "[Install] Registered ${CMAKE_MATCH_COUNT} module header directories")
    endif()
    
    # Install 3rd party headers (development component)
    if(SDK_REGISTERED_3RDPARTY_HEADERS)
        list(REMOVE_DUPLICATES SDK_REGISTERED_3RDPARTY_HEADERS)
        foreach(ITEM ${SDK_REGISTERED_3RDPARTY_HEADERS})
            if(IS_DIRECTORY ${ITEM})
                get_filename_component(DIR_NAME ${ITEM} NAME)
                install(DIRECTORY ${ITEM}/
                    COMPONENT development
                    DESTINATION include/${FIN_INCLUDE_PREFIX}/${DIR_NAME}
                    FILES_MATCHING PATTERN "*.h"
                )
            else()
                install(FILES ${ITEM}
                    COMPONENT development
                    DESTINATION include/${FIN_INCLUDE_PREFIX}
                )
            endif()
        endforeach()
    endif()
    
    # Install pkg-config (development component)
    if(DEFINED FIN_PKGCONFIG_TEMPLATE AND EXISTS ${FIN_PKGCONFIG_TEMPLATE})
        get_filename_component(PC_NAME ${FIN_PKGCONFIG_TEMPLATE} NAME_WE)
        configure_file(
            ${FIN_PKGCONFIG_TEMPLATE}
            ${CMAKE_CURRENT_BINARY_DIR}/${PC_NAME}.pc
            @ONLY
        )
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PC_NAME}.pc
            COMPONENT development
            DESTINATION lib/pkgconfig
        )
    endif()
    
    # Install CMake config (development component)
    # Create a simple config file without EXPORT (OBJECT libraries can't be exported)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/${FIN_LIBRARY_TARGET}-config.cmake "
# ${FIN_LIBRARY_TARGET} CMake Configuration
# Generated by PackageComponents.cmake

# Provide legacy FindXXX.cmake compatibility
@PACKAGE_INIT@

# Define library location
set(${FIN_LIBRARY_TARGET}_INCLUDE_DIRS \"\${CMAKE_CURRENT_LIST_DIR}/../../../include/${FIN_INCLUDE_PREFIX}\")
set(${FIN_LIBRARY_TARGET}_LIBRARIES ${FIN_LIBRARY_TARGET})

# Create imported target if not exists
if(NOT TARGET ${FIN_LIBRARY_TARGET})
    add_library(${FIN_LIBRARY_TARGET} SHARED IMPORTED)
    set_target_properties(${FIN_LIBRARY_TARGET} PROPERTIES
        IMPORTED_LOCATION \"\${CMAKE_CURRENT_LIST_DIR}/../../../lib/lib${FIN_LIBRARY_TARGET}.so\"
        INTERFACE_INCLUDE_DIRECTORIES \"\${${FIN_LIBRARY_TARGET}_INCLUDE_DIRS}\"
    )
endif()

message(STATUS \"Found ${FIN_LIBRARY_TARGET}: \${${FIN_LIBRARY_TARGET}_LIBRARIES}\")
")
    
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${FIN_LIBRARY_TARGET}-config.cmake
        COMPONENT development
        DESTINATION lib/cmake/${FIN_LIBRARY_TARGET}
    )
    
    # Install examples (documentation component)
    if(DEFINED FIN_EXAMPLES_DIR AND EXISTS ${FIN_EXAMPLES_DIR})
        install(DIRECTORY ${FIN_EXAMPLES_DIR}/
            COMPONENT documentation
            DESTINATION share/doc/${FIN_LIBRARY_TARGET}/examples
            FILES_MATCHING 
            PATTERN "*.c" PATTERN "*.h" PATTERN "*.cpp" 
            PATTERN "*.md" PATTERN "*.txt" PATTERN "CMakeLists.txt"
        )
    endif()
    
    # Install README (documentation component)
    if(DEFINED FIN_README_FILE AND EXISTS ${FIN_README_FILE})
        install(FILES ${FIN_README_FILE}
            COMPONENT documentation
            DESTINATION share/doc/${FIN_LIBRARY_TARGET}
        )
    endif()
    
    message(STATUS "[PackageComponents] Finalized: ${FIN_LIBRARY_TARGET}")
endfunction()

message(STATUS "[PackageComponents] Universal component packaging system loaded")
