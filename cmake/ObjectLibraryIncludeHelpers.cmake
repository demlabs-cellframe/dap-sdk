# Object Library Include Helpers
# Alternative approach: Create INTERFACE libraries for include directories only
# This ensures include directories are properly propagated

# Create INTERFACE library wrapper for include directories
function(create_include_interface_library TARGET_NAME)
    # Create INTERFACE library
    add_library(${TARGET_NAME}_includes INTERFACE)
    
    # Copy all include directories from original target
    get_target_property(TGT_INCLUDES ${TARGET_NAME} INTERFACE_INCLUDE_DIRECTORIES)
    if(TGT_INCLUDES)
        target_include_directories(${TARGET_NAME}_includes INTERFACE ${TGT_INCLUDES})
    endif()
    
    get_target_property(TGT_PUBLIC_INCLUDES ${TARGET_NAME} INCLUDE_DIRECTORIES)
    if(TGT_PUBLIC_INCLUDES)
        target_include_directories(${TARGET_NAME}_includes INTERFACE ${TGT_PUBLIC_INCLUDES})
    endif()
endfunction()

# Link OBJECT library to include-only INTERFACE library
function(link_object_to_include_interface OBJ_TARGET INCLUDE_TARGET)
    target_link_libraries(${OBJ_TARGET} INTERFACE ${INCLUDE_TARGET})
    # Copy includes from INTERFACE library to OBJECT library
    get_target_property(INC_INCLUDES ${INCLUDE_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    if(INC_INCLUDES)
        target_include_directories(${OBJ_TARGET} PRIVATE ${INC_INCLUDES})
    endif()
endfunction()

