# Object Library Helpers for CMake 3.12+
# Automatic propagation of INTERFACE_INCLUDE_DIRECTORIES for OBJECT libraries
# Author: QEVM Team
# Date: 2025-10-21

# =========================================
# OVERRIDE target_link_libraries FOR OBJECT LIBRARIES
# =========================================
# CMake 3.12+ supports target_link_libraries for OBJECT libraries,
# but doesn't automatically propagate INTERFACE_INCLUDE_DIRECTORIES during compilation.
# This override fixes that by automatically copying include directories from dependencies.

# Save original command
if(NOT COMMAND _original_tll)
    macro(_original_tll)
        _target_link_libraries(${ARGN})
    endmacro()
endif()

# Override with auto-propagating version
macro(target_link_libraries TARGET_NAME)
    # Call original
    _target_link_libraries(${TARGET_NAME} ${ARGN})
    
    # For OBJECT libraries, copy include directories from dependencies
    get_target_property(TGT_TYPE ${TARGET_NAME} TYPE)
    if(TGT_TYPE STREQUAL "OBJECT_LIBRARY")
        foreach(ARG ${ARGN})
            # Skip scope keywords
            if(NOT ARG MATCHES "^(PUBLIC|PRIVATE|INTERFACE)$")
                if(TARGET ${ARG})
                    get_target_property(DEP_INCS ${ARG} INTERFACE_INCLUDE_DIRECTORIES)
                    if(DEP_INCS)
                        target_include_directories(${TARGET_NAME} PRIVATE ${DEP_INCS})
                    endif()
                endif()
            endif()
        endforeach()
    endif()
endmacro()

message(STATUS "[ObjectLibraryHelpers] Automatic include propagation enabled for OBJECT libraries")

