# Object Library Include Post-Processing
# Post-process approach: After all targets are created, traverse dependency graph
# and add include directories

# Collect all OBJECT libraries and their dependencies
set(DAP_SDK_OBJECT_LIBRARIES "" CACHE INTERNAL "List of all OBJECT libraries")

# Function to register OBJECT library
function(register_object_library TARGET_NAME)
    get_target_property(TGT_TYPE ${TARGET_NAME} TYPE)
    if(TGT_TYPE STREQUAL "OBJECT_LIBRARY")
        list(APPEND DAP_SDK_OBJECT_LIBRARIES ${TARGET_NAME})
        set(DAP_SDK_OBJECT_LIBRARIES ${DAP_SDK_OBJECT_LIBRARIES} CACHE INTERNAL "OBJECT libraries list")
    endif()
endfunction()

# Function to propagate includes recursively with cycle detection
# Uses global cache for cycle detection to handle recursive calls correctly
function(propagate_includes_recursive TARGET_NAME VISITED_SET_ID)
    # Check if already visited using global cache
    set(CACHE_KEY "_DAP_POST_VISITED_${VISITED_SET_ID}_${TARGET_NAME}")
    get_property(IS_VISITED_VALUE CACHE ${CACHE_KEY} PROPERTY VALUE)
    if(IS_VISITED_VALUE STREQUAL "VISITED")
        # Cycle detected - stop processing this branch immediately
        # Already added includes up to this point will be used
        # Further includes should be specified directly by the modules
        return()
    endif()
    
    # Mark as visited in global cache
    set(${CACHE_KEY} "VISITED" CACHE INTERNAL "Cycle detection marker for post-processing")
    
    # Get dependencies
    get_target_property(DEPS ${TARGET_NAME} INTERFACE_LINK_LIBRARIES)
    if(DEPS)
        foreach(DEP ${DEPS})
            if(TARGET ${DEP})
                # Recursively process dependency's dependencies FIRST
                # This ensures transitive dependencies are processed before adding includes
                propagate_includes_recursive(${DEP} ${VISITED_SET_ID})
                
                # Get include directories from dependency
                get_target_property(DEP_INTERFACE_INCLUDES ${DEP} INTERFACE_INCLUDE_DIRECTORIES)
                get_target_property(DEP_INCLUDES ${DEP} INCLUDE_DIRECTORIES)
                
                # Add includes to the current target
                # All transitive includes will be collected recursively
                if(DEP_INTERFACE_INCLUDES)
                    target_include_directories(${TARGET_NAME} PRIVATE ${DEP_INTERFACE_INCLUDES})
                endif()
                
                if(DEP_INCLUDES)
                    target_include_directories(${TARGET_NAME} PRIVATE ${DEP_INCLUDES})
                endif()
            endif()
        endforeach()
    endif()
endfunction()

# Function to post-process all OBJECT libraries
function(post_process_object_libraries)
    message(STATUS "[SDK] Post-processing OBJECT libraries to propagate include directories...")
    
    # Count total libraries for progress reporting
    list(LENGTH DAP_SDK_OBJECT_LIBRARIES TOTAL_LIBS)
    message(STATUS "[SDK] Processing ${TOTAL_LIBS} OBJECT libraries...")
    
    set(PROCESSED_COUNT 0)
    foreach(OBJ_LIB ${DAP_SDK_OBJECT_LIBRARIES})
        if(TARGET ${OBJ_LIB})
            # Create unique visited set ID for this library's traversal
            # Use timestamp and random to ensure uniqueness
            string(TIMESTAMP TIMESTAMP_VALUE "%Y%m%d%H%M%S")
            string(RANDOM RANDOM_VALUE LENGTH 8)
            set(VISITED_SET_ID "${OBJ_LIB}_${TIMESTAMP_VALUE}_${RANDOM_VALUE}")
            
            # Process includes with global cache-based cycle detection
            propagate_includes_recursive(${OBJ_LIB} ${VISITED_SET_ID})
            
            # Cleanup cache for this traversal
            # Find all keys matching this visited set and remove them
            set(CACHE_PATTERN "_DAP_POST_VISITED_${VISITED_SET_ID}_")
            get_cmake_property(CACHE_VARS CACHE_VARIABLES)
            foreach(CACHE_VAR ${CACHE_VARS})
                if(CACHE_VAR MATCHES "^${CACHE_PATTERN}")
                    unset(${CACHE_VAR} CACHE)
                endif()
            endforeach()
            
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

