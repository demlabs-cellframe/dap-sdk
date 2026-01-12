# Object Library Include Mapping
# Centralized mapping of which OBJECT libraries need which include directories
# This approach uses explicit mapping instead of automatic propagation

# Define include directory mappings
set(DAP_SDK_INCLUDE_MAP "" CACHE INTERNAL "Include directory mappings for OBJECT libraries")

# Function to register include directory mapping
function(register_object_include_mapping OBJ_TARGET DEP_TARGET)
    set(CURRENT_MAP ${DAP_SDK_INCLUDE_MAP})
    list(APPEND CURRENT_MAP "${OBJ_TARGET}:${DEP_TARGET}")
    set(DAP_SDK_INCLUDE_MAP ${CURRENT_MAP} CACHE INTERNAL "Include directory mappings")
endfunction()

# Function to apply all registered mappings
function(apply_object_include_mappings)
    foreach(MAPPING ${DAP_SDK_INCLUDE_MAP})
        string(REPLACE ":" ";" MAPPING_PARTS ${MAPPING})
        list(GET MAPPING_PARTS 0 OBJ_TARGET)
        list(GET MAPPING_PARTS 1 DEP_TARGET)
        
        if(TARGET ${OBJ_TARGET} AND TARGET ${DEP_TARGET})
            get_target_property(DEP_INCLUDES ${DEP_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
            if(DEP_INCLUDES)
                target_include_directories(${OBJ_TARGET} PRIVATE ${DEP_INCLUDES})
            endif()
        endif()
    endforeach()
endfunction()




