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

# Compute transitive include closure for TARGET_NAME's dependencies.
# Uses bounded fixed-point iteration: cyclic nodes are cached per-pass
# and re-evaluated across passes (max 3) until stable.
function(_dap_collect_transitive_includes TARGET_NAME OUT_VAR)
    get_property(_pass_id GLOBAL PROPERTY "_DAP_PP_CURRENT_PASS")
    get_property(_memo GLOBAL PROPERTY "_DAP_PP_MEMO_${TARGET_NAME}")
    get_property(_has  GLOBAL PROPERTY "_DAP_PP_MEMO_${TARGET_NAME}" SET)
    get_property(_memo_pass GLOBAL PROPERTY "_DAP_PP_PASS_${TARGET_NAME}")

    # Cached acyclic result — always reusable
    if(_has AND NOT _memo STREQUAL "_WIP_" AND NOT _memo STREQUAL "_NOCACHE_")
        set(${OUT_VAR} "${_memo}" PARENT_SCOPE)
        return()
    endif()

    # Cyclic node cached for this pass — reuse within same pass
    if(_memo STREQUAL "_NOCACHE_" AND _memo_pass STREQUAL "${_pass_id}")
        get_property(_cached GLOBAL PROPERTY "_DAP_PP_CACHED_${TARGET_NAME}")
        set(${OUT_VAR} "${_cached}" PARENT_SCOPE)
        return()
    endif()

    # Cycle detection: currently being resolved
    if(_memo STREQUAL "_WIP_")
        set(${OUT_VAR} "" PARENT_SCOPE)
        set_property(GLOBAL PROPERTY "_DAP_PP_IN_CYCLE" TRUE)
        return()
    endif()

    set_property(GLOBAL PROPERTY "_DAP_PP_MEMO_${TARGET_NAME}" "_WIP_")

    get_target_property(_type ${TARGET_NAME} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY")
        set_property(GLOBAL PROPERTY "_DAP_PP_MEMO_${TARGET_NAME}" "")
        set(${OUT_VAR} "" PARENT_SCOPE)
        return()
    endif()

    set(_all "")
    set(_in_cycle FALSE)

    get_target_property(_deps ${TARGET_NAME} INTERFACE_LINK_LIBRARIES)
    if(_deps)
        foreach(_dep ${_deps})
            if(TARGET ${_dep})
                get_target_property(_dep_iface ${_dep} INTERFACE_INCLUDE_DIRECTORIES)
                if(_dep_iface AND NOT _dep_iface MATCHES "-NOTFOUND$")
                    list(APPEND _all ${_dep_iface})
                endif()

                get_target_property(_dep_inc ${_dep} INCLUDE_DIRECTORIES)
                if(_dep_inc AND NOT _dep_inc MATCHES "-NOTFOUND$")
                    list(APPEND _all ${_dep_inc})
                endif()

                set_property(GLOBAL PROPERTY "_DAP_PP_IN_CYCLE" FALSE)
                _dap_collect_transitive_includes(${_dep} _trans)
                get_property(_dep_cycle GLOBAL PROPERTY "_DAP_PP_IN_CYCLE")
                if(_dep_cycle)
                    set(_in_cycle TRUE)
                endif()

                if(_trans)
                    list(APPEND _all ${_trans})
                endif()
            endif()
        endforeach()
    endif()

    if(_all)
        list(REMOVE_DUPLICATES _all)
    endif()

    if(_in_cycle)
        set_property(GLOBAL PROPERTY "_DAP_PP_MEMO_${TARGET_NAME}" "_NOCACHE_")
        set_property(GLOBAL PROPERTY "_DAP_PP_PASS_${TARGET_NAME}" "${_pass_id}")
        set_property(GLOBAL PROPERTY "_DAP_PP_CACHED_${TARGET_NAME}" "${_all}")
        set_property(GLOBAL PROPERTY "_DAP_PP_IN_CYCLE" TRUE)
    else()
        set_property(GLOBAL PROPERTY "_DAP_PP_MEMO_${TARGET_NAME}" "${_all}")
    endif()

    set(${OUT_VAR} "${_all}" PARENT_SCOPE)
endfunction()

function(post_process_object_libraries)
    message(STATUS "[SDK] Post-processing OBJECT libraries to propagate include directories...")

    list(LENGTH DAP_SDK_OBJECT_LIBRARIES TOTAL_LIBS)
    message(STATUS "[SDK] Processing ${TOTAL_LIBS} OBJECT libraries...")

    set(_MAX_PASSES 3)
    set(_pass 0)
    set(_changed TRUE)
    while(_changed AND _pass LESS _MAX_PASSES)
        math(EXPR _pass "${_pass} + 1")
        set_property(GLOBAL PROPERTY "_DAP_PP_CURRENT_PASS" "${_pass}")
        set(_changed FALSE)

        set(PROCESSED_COUNT 0)
        foreach(OBJ_LIB ${DAP_SDK_OBJECT_LIBRARIES})
            if(TARGET ${OBJ_LIB})
                _dap_collect_transitive_includes(${OBJ_LIB} _includes)

                get_target_property(_current_inc ${OBJ_LIB} INCLUDE_DIRECTORIES)
                if(NOT _current_inc OR _current_inc MATCHES "-NOTFOUND$")
                    set(_current_inc "")
                endif()

                if(_includes)
                    set(_new_dirs "")
                    foreach(_inc ${_includes})
                        list(FIND _current_inc "${_inc}" _idx)
                        if(_idx EQUAL -1)
                            list(APPEND _new_dirs "${_inc}")
                        endif()
                    endforeach()
                    if(_new_dirs)
                        target_include_directories(${OBJ_LIB} PRIVATE ${_new_dirs})
                        set(_changed TRUE)
                    endif()
                endif()
                math(EXPR PROCESSED_COUNT "${PROCESSED_COUNT} + 1")
            endif()
        endforeach()
        message(STATUS "[SDK] Pass ${_pass}: processed ${PROCESSED_COUNT}/${TOTAL_LIBS}, changed=${_changed}")
    endwhile()

    message(STATUS "[SDK] Post-processing complete after ${_pass} pass(es): ${PROCESSED_COUNT}/${TOTAL_LIBS} OBJECT libraries")
endfunction()

