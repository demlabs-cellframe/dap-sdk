# Object Library Include Post-Processing
# Post-process approach: After all targets are created, traverse dependency graph
# and add include directories

# Collect all OBJECT libraries and their dependencies
set(DAP_SDK_OBJECT_LIBRARIES "" CACHE INTERNAL "List of all OBJECT libraries")

# Function to register OBJECT library
# IMPORTANT: This must be a macro, not a function, because CACHE variables
# don't persist properly across function calls due to scope issues
macro(register_object_library TARGET_NAME)
    get_target_property(TGT_TYPE_FOR_REG ${TARGET_NAME} TYPE)
    if(TGT_TYPE_FOR_REG STREQUAL "OBJECT_LIBRARY")
        # IMPORTANT: CMake CACHE variables are tricky in macros
        # We use a global property instead of CACHE variable for accumulation
        # Global properties work reliably across macro/function boundaries
        get_property(CURRENT_LIBS_FOR_REG GLOBAL PROPERTY DAP_SDK_OBJECT_LIBRARIES_LIST)
        if(NOT CURRENT_LIBS_FOR_REG)
            set(CURRENT_LIBS_FOR_REG "")
        endif()
        # Append new library if not already in list
        list(FIND CURRENT_LIBS_FOR_REG ${TARGET_NAME} FOUND_IDX_FOR_REG)
        if(FOUND_IDX_FOR_REG EQUAL -1)
            # Append to list
            list(APPEND CURRENT_LIBS_FOR_REG ${TARGET_NAME})
            # Write back to GLOBAL property (reliable across all contexts)
            set_property(GLOBAL PROPERTY DAP_SDK_OBJECT_LIBRARIES_LIST "${CURRENT_LIBS_FOR_REG}")
            # Also update CACHE for compatibility with post_process_object_libraries
            set(DAP_SDK_OBJECT_LIBRARIES "${CURRENT_LIBS_FOR_REG}" CACHE INTERNAL "OBJECT libraries list" FORCE)
        endif()
    endif()
endmacro()

# Function to propagate includes recursively with cycle detection
# Uses global property for cycle detection (much faster than CACHE)
function(propagate_includes_recursive TARGET_NAME VISITED_SET_ID)
    # Check if already visited using global property (faster than CACHE)
    set(PROPERTY_KEY "_DAP_POST_VISITED_${VISITED_SET_ID}_${TARGET_NAME}")
    get_property(IS_VISITED_VALUE GLOBAL PROPERTY ${PROPERTY_KEY})
    if(IS_VISITED_VALUE STREQUAL "VISITED")
        # Cycle detected - stop processing this branch immediately
        return()
    endif()
    
    # Mark as visited in global property (faster than CACHE)
    set_property(GLOBAL PROPERTY ${PROPERTY_KEY} "VISITED")
    
    # Collect all includes first, then apply once (more efficient)
    set(COLLECTED_INTERFACE_INCLUDES "")
    set(COLLECTED_INCLUDES "")
    
    # Get dependencies
    get_target_property(DEPS ${TARGET_NAME} INTERFACE_LINK_LIBRARIES)
    if(DEPS)
        foreach(DEP ${DEPS})
            if(TARGET ${DEP})
                # Recursively process dependency's dependencies FIRST
                propagate_includes_recursive(${DEP} ${VISITED_SET_ID})
                
                # Get include directories from dependency
                get_target_property(DEP_INTERFACE_INCLUDES ${DEP} INTERFACE_INCLUDE_DIRECTORIES)
                get_target_property(DEP_INCLUDES ${DEP} INCLUDE_DIRECTORIES)
                
                # Collect includes (avoid duplicates)
                if(DEP_INTERFACE_INCLUDES AND NOT DEP_INTERFACE_INCLUDES STREQUAL "DEP_INTERFACE_INCLUDES-NOTFOUND")
                    list(APPEND COLLECTED_INTERFACE_INCLUDES ${DEP_INTERFACE_INCLUDES})
                endif()
                
                if(DEP_INCLUDES AND NOT DEP_INCLUDES STREQUAL "DEP_INCLUDES-NOTFOUND")
                    list(APPEND COLLECTED_INCLUDES ${DEP_INCLUDES})
                endif()
            endif()
        endforeach()
    endif()
    
    # Apply collected includes once (more efficient than multiple calls)
    if(COLLECTED_INTERFACE_INCLUDES)
        list(REMOVE_DUPLICATES COLLECTED_INTERFACE_INCLUDES)
        target_include_directories(${TARGET_NAME} PRIVATE ${COLLECTED_INTERFACE_INCLUDES})
    endif()
    
    if(COLLECTED_INCLUDES)
        list(REMOVE_DUPLICATES COLLECTED_INCLUDES)
        target_include_directories(${TARGET_NAME} PRIVATE ${COLLECTED_INCLUDES})
    endif()
endfunction()

# Function to post-process all OBJECT libraries
function(post_process_object_libraries)
    message(STATUS "[SDK] Post-processing OBJECT libraries to propagate include directories...")
    
    # Count total libraries for progress reporting
    list(LENGTH DAP_SDK_OBJECT_LIBRARIES TOTAL_LIBS)
    message(STATUS "[SDK] Processing ${TOTAL_LIBS} OBJECT libraries...")
    
    set(PROCESSED_COUNT 0)
    set(VISITED_SET_COUNTER 0)
    foreach(OBJ_LIB ${DAP_SDK_OBJECT_LIBRARIES})
        if(TARGET ${OBJ_LIB})
            # Create unique visited set ID using simple counter (much faster than timestamp+random)
            math(EXPR VISITED_SET_COUNTER "${VISITED_SET_COUNTER} + 1")
            set(VISITED_SET_ID "${VISITED_SET_COUNTER}")
            
            # Process includes with global property-based cycle detection
            propagate_includes_recursive(${OBJ_LIB} ${VISITED_SET_ID})
            
            # Note: Property cleanup not needed - GLOBAL properties are faster than CACHE
            # Properties with unique VISITED_SET_ID won't conflict between traversals
            # and GLOBAL properties don't have the performance penalty of CACHE variables
            
            math(EXPR PROCESSED_COUNT "${PROCESSED_COUNT} + 1")
            # Show progress for first 10, last, or every 10th library
            math(EXPR MOD_RESULT "${PROCESSED_COUNT} % 10")
            if(PROCESSED_COUNT LESS 10)
                message(STATUS "[SDK] Processed ${PROCESSED_COUNT}/${TOTAL_LIBS} libraries...")
            elseif(PROCESSED_COUNT EQUAL TOTAL_LIBS)
                message(STATUS "[SDK] Processed ${PROCESSED_COUNT}/${TOTAL_LIBS} libraries...")
            elseif(MOD_RESULT EQUAL 0)
                message(STATUS "[SDK] Processed ${PROCESSED_COUNT}/${TOTAL_LIBS} libraries...")
            endif()
        endif()
    endforeach()
    
    message(STATUS "[SDK] Post-processing complete for ${TOTAL_LIBS} OBJECT libraries")
endfunction()

