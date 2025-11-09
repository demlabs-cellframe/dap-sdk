#!/bin/bash
# DAP Template Processing Library
# Provides helper functions for preparing data structures for dap_tpl template engine
#
# Usage:
#   source dap_tpl_lib.sh
#   prepare_map_count_params_by_count_data MAX_ARGS_COUNT
#   prepare_map_count_params_helper_data MAX_ARGS_COUNT
#   prepare_map_impl_cond_1_data MAX_ARGS_COUNT PARAM_COUNTS_ARRAY
#   prepare_map_impl_cond_data PARAM_COUNTS_ARRAY
#   prepare_map_macros_data PARAM_COUNTS_ARRAY

# Helper function to append to newline-separated data
# Usage: append_data <var_name> <value>
append_data() {
    local var_name="$1"
    local value="$2"
    local current_value="${!var_name}"
    
    if [ -n "$current_value" ]; then
        eval "${var_name}=\"\${${var_name}}\${value}\""
    else
        eval "${var_name}=\"\${value}\""
    fi
}

# Prepare MAP_COUNT_PARAMS_BY_COUNT_DATA for template
# Generates data for _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_N macros (arg_count >= 2)
# Usage: prepare_map_count_params_by_count_data MAX_ARGS_COUNT
# Output: Sets MAP_COUNT_PARAMS_BY_COUNT_DATA variable (newline-separated arg_count values)
prepare_map_count_params_by_count_data() {
    local max_args_count="$1"
    [ -z "$max_args_count" ] && max_args_count=0
    [ "$max_args_count" -lt 0 ] && max_args_count=0
    
    MAP_COUNT_PARAMS_BY_COUNT_DATA=""
    for i in $(seq 2 "$max_args_count"); do
        if [ -n "$MAP_COUNT_PARAMS_BY_COUNT_DATA" ]; then
            MAP_COUNT_PARAMS_BY_COUNT_DATA="${MAP_COUNT_PARAMS_BY_COUNT_DATA}"$'\n'"${i}"
        else
            MAP_COUNT_PARAMS_BY_COUNT_DATA="${i}"
        fi
    done
}

# Prepare MAP_COUNT_PARAMS_HELPER_DATA for template
# Generates data for _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_N macros
# Usage: prepare_map_count_params_helper_data MAX_ARGS_COUNT
# Output: Sets MAP_COUNT_PARAMS_HELPER_DATA variable (newline-separated: arg_count|param_count)
prepare_map_count_params_helper_data() {
    local max_args_count="$1"
    [ -z "$max_args_count" ] && max_args_count=0
    [ "$max_args_count" -lt 0 ] && max_args_count=0
    
    MAP_COUNT_PARAMS_HELPER_DATA=""
    for i in $(seq 0 "$max_args_count"); do
        param_count=$((i / 2))
        if [ -n "$MAP_COUNT_PARAMS_HELPER_DATA" ]; then
            MAP_COUNT_PARAMS_HELPER_DATA="${MAP_COUNT_PARAMS_HELPER_DATA}"$'\n'"${i}|${param_count}"
        else
            MAP_COUNT_PARAMS_HELPER_DATA="${i}|${param_count}"
        fi
    done
}

# Prepare MAP_IMPL_COND_1_DATA for template
# Generates data for _DAP_MOCK_MAP_IMPL_COND_1_N macros
# Usage: prepare_map_impl_cond_1_data MAX_ARGS_COUNT PARAM_COUNTS_ARRAY
# Output: Sets MAP_IMPL_COND_1_DATA variable (newline-separated: arg_count|param_count|has_count|fallback_count|macro_params)
prepare_map_impl_cond_1_data() {
    local max_args_count="$1"
    shift
    local param_counts_array=("$@")
    
    [ -z "$max_args_count" ] && max_args_count=0
    [ "$max_args_count" -lt 0 ] && max_args_count=0
    
    # Track which param counts we have
    declare -A HAS_PARAM_COUNT
    for count in "${param_counts_array[@]}"; do
        [ -z "$count" ] && continue
        HAS_PARAM_COUNT["$count"]=1
    done
    
    MAP_IMPL_COND_1_DATA=""
    for arg_count in $(seq 1 "$max_args_count"); do
        param_count=$((arg_count / 2))
        
        # Build macro parameters list
        macro_params="macro"
        for j in $(seq 1 "$arg_count"); do
            macro_params="${macro_params}, p${j}"
        done
        
        # Determine if we have this param_count or need fallback
        has_count=0
        if [ "${HAS_PARAM_COUNT[$param_count]:-0}" = "1" ] || [ "$param_count" -eq 0 ]; then
            has_count=1
            fallback_count="$param_count"
        else
            # Find fallback count (largest available count <= param_count)
            fallback_count=0
            for count in "${param_counts_array[@]}"; do
                [ -z "$count" ] && continue
                if [ "$count" -le "$param_count" ] && [ "$count" -gt "$fallback_count" ]; then
                    fallback_count="$count"
                fi
            done
        fi
        
        if [ -n "$MAP_IMPL_COND_1_DATA" ]; then
            MAP_IMPL_COND_1_DATA="${MAP_IMPL_COND_1_DATA}"$'\n'"${arg_count}|${param_count}|${has_count}|${fallback_count}|${macro_params}"
        else
            MAP_IMPL_COND_1_DATA="${arg_count}|${param_count}|${has_count}|${fallback_count}|${macro_params}"
        fi
    done
}

# Prepare MAP_IMPL_COND_DATA for template
# Generates data for _DAP_MOCK_MAP_IMPL_COND_N macros for count > 1
# Usage: prepare_map_impl_cond_data PARAM_COUNTS_ARRAY
# Output: Sets MAP_IMPL_COND_DATA variable (newline-separated param_count values)
prepare_map_impl_cond_data() {
    local param_counts_array=("$@")
    
    MAP_IMPL_COND_DATA=""
    for count in "${param_counts_array[@]}"; do
        [ -z "$count" ] && continue
        if [ "$count" -gt 1 ]; then
            if [ -n "$MAP_IMPL_COND_DATA" ]; then
                MAP_IMPL_COND_DATA="${MAP_IMPL_COND_DATA}"$'\n'"${count}"
            else
                MAP_IMPL_COND_DATA="${count}"
            fi
        fi
    done
}

# Prepare MAP_MACROS_DATA for template
# Generates macro definitions for _DAP_MOCK_MAP_N macros
# Usage: prepare_map_macros_data PARAM_COUNTS_ARRAY
# Output: Sets MAP_MACROS_DATA variable (newline-separated: count|macro_def) and has_count_1 flag
prepare_map_macros_data() {
    local param_counts_array=("$@")
    
    MAP_MACROS_DATA=""
    has_count_1=0
    for count in "${param_counts_array[@]}"; do
        [ -z "$count" ] && continue
        [ "$count" = "1" ] && has_count_1=1
        
        if [ "$count" -eq 0 ]; then
            macro_def="// Macro for 0 parameter(s) (PARAM entries)"$'\n'"#define _DAP_MOCK_MAP_0(macro, ...) \\"$'\n'""
        else
            total_args=$((count * 2))
            macro_def="// Macro for $count parameter(s) (PARAM entries)"$'\n'"#define _DAP_MOCK_MAP_${count}(macro"
            for j in $(seq 1 $total_args); do
                macro_def="${macro_def}, p${j}"
            done
            macro_def="${macro_def}, ...) \\"$'\n'"    macro(p1, p2)"
            for j in $(seq 2 $count); do
                type_idx=$((j * 2 - 1))
                name_idx=$((j * 2))
                macro_def="${macro_def}, macro(p${type_idx}, p${name_idx})"
            done
            macro_def="${macro_def}"$'\n'
        fi
        
        if [ -n "$MAP_MACROS_DATA" ]; then
            MAP_MACROS_DATA="${MAP_MACROS_DATA}"$'\n'"${count}|${macro_def}"
        else
            MAP_MACROS_DATA="${count}|${macro_def}"
        fi
    done
    
    # Always add _DAP_MOCK_MAP_1 if needed
    if [ "$has_count_1" -eq 0 ]; then
        map_1_def="// Macro for 1 parameter(s) - needed for _DAP_MOCK_MAP_IMPL_COND_1_0"$'\n'"#define _DAP_MOCK_MAP_1(macro, p1, p2, ...) \\"$'\n'"    macro(p1, p2)"$'\n'
        if [ -n "$MAP_MACROS_DATA" ]; then
            MAP_MACROS_DATA="${MAP_MACROS_DATA}"$'\n'"1|${map_1_def}"
        else
            MAP_MACROS_DATA="1|${map_1_def}"
        fi
    fi
}

# Prepare NARGS_SEQUENCE and NARGS_IMPL_PARAMS for template
# Generates data for _DAP_MOCK_NARGS macro generation
# Usage: prepare_nargs_data MAX_ARGS_COUNT
# Output: Sets NARGS_SEQUENCE and NARGS_IMPL_PARAMS variables
prepare_nargs_data() {
    local max_args_count="$1"
    [ -z "$max_args_count" ] && max_args_count=0
    [ "$max_args_count" -lt 0 ] && max_args_count=0
    
    # Prepare NARGS_SEQUENCE
    NARGS_SEQUENCE=""
    for i in $(seq $max_args_count -1 0); do
        if [ -n "$NARGS_SEQUENCE" ]; then
            NARGS_SEQUENCE="${NARGS_SEQUENCE}, $i"
        else
            NARGS_SEQUENCE=", $i"
        fi
    done
    
    # Prepare NARGS_IMPL_PARAMS
    NARGS_IMPL_PARAMS=""
    for i in $(seq 1 $max_args_count); do
        if [ -n "$NARGS_IMPL_PARAMS" ]; then
            NARGS_IMPL_PARAMS="${NARGS_IMPL_PARAMS}, _$i"
        else
            NARGS_IMPL_PARAMS="_$i"
        fi
    done
}

