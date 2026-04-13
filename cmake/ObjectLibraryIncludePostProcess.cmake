# Object Library Include Post-Processing
# Single-pass DFS with memoization to propagate INTERFACE_INCLUDE_DIRECTORIES
# transitively to OBJECT libraries (which CMake doesn't do automatically).

set(DAP_SDK_OBJECT_LIBRARIES "" CACHE INTERNAL "List of all OBJECT libraries")

macro(register_object_library TARGET_NAME)
    get_target_property(TGT_TYPE_FOR_REG ${TARGET_NAME} TYPE)
    if(TGT_TYPE_FOR_REG STREQUAL "OBJECT_LIBRARY")
        get_property(CURRENT_LIBS_FOR_REG GLOBAL PROPERTY DAP_SDK_OBJECT_LIBRARIES_LIST)
        if(NOT CURRENT_LIBS_FOR_REG)
            set(CURRENT_LIBS_FOR_REG "")
        endif()
        list(FIND CURRENT_LIBS_FOR_REG ${TARGET_NAME} FOUND_IDX_FOR_REG)
        if(FOUND_IDX_FOR_REG EQUAL -1)
            list(APPEND CURRENT_LIBS_FOR_REG ${TARGET_NAME})
            set_property(GLOBAL PROPERTY DAP_SDK_OBJECT_LIBRARIES_LIST "${CURRENT_LIBS_FOR_REG}")
            set(DAP_SDK_OBJECT_LIBRARIES "${CURRENT_LIBS_FOR_REG}" CACHE INTERNAL "OBJECT libraries list" FORCE)
        endif()
    endif()
endmacro()

# Single-pass DFS: collect transitive INTERFACE_INCLUDE_DIRECTORIES for a target.
# States per node (global property _PP_S_<name>):
#   <unset>  = not visited
#   WIP      = currently on the DFS stack (back-edge detection)
#   DONE     = fully resolved, result in _PP_R_<name>
#   CYCLIC   = resolved but participates in a cycle, needs fixup
function(_pp_dfs TARGET_NAME)
    get_property(_state GLOBAL PROPERTY "_PP_S_${TARGET_NAME}")

    if(_state STREQUAL "DONE" OR _state STREQUAL "CYCLIC")
        return()
    endif()

    if(_state STREQUAL "WIP")
        set_property(GLOBAL PROPERTY "_PP_CYCLE_HIT" TRUE)
        return()
    endif()

    set_property(GLOBAL PROPERTY "_PP_S_${TARGET_NAME}" "WIP")

    if(NOT TARGET ${TARGET_NAME})
        set_property(GLOBAL PROPERTY "_PP_S_${TARGET_NAME}" "DONE")
        set_property(GLOBAL PROPERTY "_PP_R_${TARGET_NAME}" "")
        return()
    endif()

    get_target_property(_type ${TARGET_NAME} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY")
        set_property(GLOBAL PROPERTY "_PP_S_${TARGET_NAME}" "DONE")
        set_property(GLOBAL PROPERTY "_PP_R_${TARGET_NAME}" "")
        return()
    endif()

    set(_all "")
    set(_has_cycle FALSE)

    get_target_property(_deps ${TARGET_NAME} INTERFACE_LINK_LIBRARIES)
    if(_deps)
        foreach(_dep ${_deps})
            if(TARGET ${_dep})
                get_target_property(_dep_iface ${_dep} INTERFACE_INCLUDE_DIRECTORIES)
                if(_dep_iface AND NOT _dep_iface MATCHES "-NOTFOUND$")
                    list(APPEND _all ${_dep_iface})
                endif()

                set_property(GLOBAL PROPERTY "_PP_CYCLE_HIT" FALSE)
                _pp_dfs(${_dep})
                get_property(_hit GLOBAL PROPERTY "_PP_CYCLE_HIT")
                if(_hit)
                    set(_has_cycle TRUE)
                endif()

                get_property(_dep_result GLOBAL PROPERTY "_PP_R_${_dep}")
                if(_dep_result)
                    list(APPEND _all ${_dep_result})
                endif()
            endif()
        endforeach()
    endif()

    if(_all)
        list(REMOVE_DUPLICATES _all)
    endif()

    set_property(GLOBAL PROPERTY "_PP_R_${TARGET_NAME}" "${_all}")

    if(_has_cycle)
        set_property(GLOBAL PROPERTY "_PP_S_${TARGET_NAME}" "CYCLIC")
        set_property(GLOBAL PROPERTY "_PP_CYCLE_HIT" TRUE)
        get_property(_cyc_list GLOBAL PROPERTY "_PP_CYCLIC_NODES")
        list(APPEND _cyc_list ${TARGET_NAME})
        set_property(GLOBAL PROPERTY "_PP_CYCLIC_NODES" "${_cyc_list}")
    else()
        set_property(GLOBAL PROPERTY "_PP_S_${TARGET_NAME}" "DONE")
    endif()
endfunction()

function(post_process_object_libraries)
    string(TIMESTAMP _PP_START "%s")
    message(STATUS "[SDK] Post-processing OBJECT libraries to propagate include directories...")

    list(LENGTH DAP_SDK_OBJECT_LIBRARIES _total)
    message(STATUS "[SDK] Processing ${_total} OBJECT libraries...")

    set_property(GLOBAL PROPERTY "_PP_CYCLIC_NODES" "")

    # --- Pass 1: single DFS over all OBJECT libraries ---
    foreach(_lib ${DAP_SDK_OBJECT_LIBRARIES})
        _pp_dfs(${_lib})
    endforeach()

    # --- Cycle fixup: re-resolve cyclic nodes with now-populated neighbor results ---
    get_property(_cyc_nodes GLOBAL PROPERTY "_PP_CYCLIC_NODES")
    list(LENGTH _cyc_nodes _cyc_count)
    if(_cyc_count GREATER 0)
        list(REMOVE_DUPLICATES _cyc_nodes)
        list(LENGTH _cyc_nodes _cyc_count)
        foreach(_lib ${_cyc_nodes})
            set(_all "")
            get_target_property(_deps ${_lib} INTERFACE_LINK_LIBRARIES)
            if(_deps)
                foreach(_dep ${_deps})
                    if(TARGET ${_dep})
                        get_target_property(_dep_iface ${_dep} INTERFACE_INCLUDE_DIRECTORIES)
                        if(_dep_iface AND NOT _dep_iface MATCHES "-NOTFOUND$")
                            list(APPEND _all ${_dep_iface})
                        endif()
                        get_property(_dep_r GLOBAL PROPERTY "_PP_R_${_dep}")
                        if(_dep_r)
                            list(APPEND _all ${_dep_r})
                        endif()
                    endif()
                endforeach()
            endif()
            if(_all)
                list(REMOVE_DUPLICATES _all)
            endif()
            set_property(GLOBAL PROPERTY "_PP_R_${_lib}" "${_all}")
        endforeach()
        message(STATUS "[SDK] Cycle fixup: ${_cyc_count} nodes re-resolved")
    endif()

    # --- Apply: add missing includes to each OBJECT library ---
    set(_changed_count 0)
    foreach(_lib ${DAP_SDK_OBJECT_LIBRARIES})
        if(NOT TARGET ${_lib})
            continue()
        endif()

        get_property(_includes GLOBAL PROPERTY "_PP_R_${_lib}")
        if(NOT _includes)
            continue()
        endif()

        get_target_property(_cur ${_lib} INCLUDE_DIRECTORIES)
        if(NOT _cur OR _cur MATCHES "-NOTFOUND$")
            set(_cur "")
        endif()

        set(_new "")
        foreach(_inc ${_includes})
            list(FIND _cur "${_inc}" _idx)
            if(_idx EQUAL -1)
                list(APPEND _new "${_inc}")
            endif()
        endforeach()

        if(_new)
            list(REMOVE_DUPLICATES _new)
            target_include_directories(${_lib} PRIVATE ${_new})
            math(EXPR _changed_count "${_changed_count} + 1")
        endif()
    endforeach()

    string(TIMESTAMP _PP_END "%s")
    math(EXPR _PP_ELAPSED "${_PP_END} - ${_PP_START}")
    message(STATUS "[SDK] Post-processing: ${_changed_count}/${_total} libraries updated, ${_cyc_count} cyclic nodes, ${_PP_ELAPSED}s")
endfunction()
