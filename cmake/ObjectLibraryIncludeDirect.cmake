# Object Library Direct Include Propagation
# Direct approach: Use generator expressions to get includes at compile time
# This uses CMake's generator expressions to propagate includes directly

# Function to add includes from dependency using generator expressions
function(add_object_include_from_dependency OBJ_TARGET DEP_TARGET)
    if(TARGET ${OBJ_TARGET} AND TARGET ${DEP_TARGET})
        # Use generator expression to get INTERFACE_INCLUDE_DIRECTORIES at compile time
        # This doesn't work directly, but we can use it as a workaround
        get_target_property(DEP_INCLUDES ${DEP_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
        if(DEP_INCLUDES)
            target_include_directories(${OBJ_TARGET} PRIVATE ${DEP_INCLUDES})
        endif()
        
        # Also get regular INCLUDE_DIRECTORIES
        get_target_property(DEP_PUBLIC_INCLUDES ${DEP_TARGET} INCLUDE_DIRECTORIES)
        if(DEP_PUBLIC_INCLUDES)
            target_include_directories(${OBJ_TARGET} PRIVATE ${DEP_PUBLIC_INCLUDES})
        endif()
        
        # Recursively process dependencies
        get_target_property(DEP_DEPS ${DEP_TARGET} INTERFACE_LINK_LIBRARIES)
        if(DEP_DEPS)
            foreach(DEP_DEP ${DEP_DEPS})
                if(TARGET ${DEP_DEP})
                    add_object_include_from_dependency(${OBJ_TARGET} ${DEP_DEP})
                endif()
            endforeach()
        endif()
    endif()
endfunction()

# Wrapper for target_link_libraries that automatically adds includes
function(object_target_link_libraries TARGET_NAME)
    # Parse arguments
    cmake_parse_arguments(OBJ_LINK "" "" "" ${ARGN})
    
    # Call original target_link_libraries
    target_link_libraries(${TARGET_NAME} ${ARGN})
    
    # Get target type
    get_target_property(TGT_TYPE ${TARGET_NAME} TYPE)
    if(TGT_TYPE STREQUAL "OBJECT_LIBRARY")
        # Extract libraries from arguments
        set(IN_INTERFACE FALSE)
        foreach(ARG ${ARGN})
            if(ARG STREQUAL "INTERFACE")
                set(IN_INTERFACE TRUE)
            elseif(IN_INTERFACE AND TARGET ${ARG})
                add_object_include_from_dependency(${TARGET_NAME} ${ARG})
            endif()
        endforeach()
    endif()
endfunction()




