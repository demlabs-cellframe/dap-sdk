#!/bin/bash
# Generate output files (wrap, cmake, macros, templates)

# Source common utilities (but don't initialize yet - that's done in main script)
LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${LIB_DIR}/dap_mock_common.sh"

# Generate linker response file with platform-specific options
# macOS: uses dyld interposition (proper way for runtime function replacement)
# Linux/other: uses --wrap=func (GNU ld feature)
generate_wrap_file() {
    local wrap_file="$1"
    local mock_functions="$2"
    local custom_mocks_file="${3:-}"  # Optional: file with return_type|func_name|param_list|macro_type|param_count
    
    > "$wrap_file"  # Clear file
    if [ -n "$mock_functions" ]; then
        local func_count=$(echo "$mock_functions" | wc -l)
        
        # Determine platform from CMake (not from $OSTYPE!)
        # CMAKE_SYSTEM_NAME is set by CMake and passed via environment
        local system_name="${CMAKE_SYSTEM_NAME:-Linux}"
        
        local wrap_dir="$(dirname "$wrap_file")"
        
        if [ "$system_name" == "Darwin" ]; then
            # =================================================================
            # macOS: Generate typed override functions via template
            # =================================================================
            # Apple ld doesn't support --wrap, so we generate override functions
            # that are linked directly into the test executable.
            # Using proper types is CRITICAL for ARM64 ABI (struct returns!)
            
            local interpose_c="${wrap_dir}/mock_interpose.c"
            local dylib_path_file="${wrap_dir}/mock_dylib_path.txt"
            
            # Build lookup file from custom_mocks_file for type lookup
            local type_lookup_file="${wrap_dir}/mock_type_lookup.txt"
            if [ -n "$custom_mocks_file" ] && [ -f "$custom_mocks_file" ] && [ -s "$custom_mocks_file" ]; then
                cp "$custom_mocks_file" "$type_lookup_file"
            else
                > "$type_lookup_file"
            fi
            
            # Helper function to lookup type info for a function
            _get_func_info() {
                local lookup_file="$1"
                local func_name="$2"
                local field="$3"  # 1=return_type, 3=param_list, 5=param_count
                grep "|${func_name}|" "$lookup_file" 2>/dev/null | head -1 | cut -d'|' -f"$field"
            }
            
            # Helper to extract __wrap_func signature from source files
            # Returns: return_type|param_list
            # BSD-compatible (no gawk-specific features)
            _extract_wrap_signature() {
                local func_name="$1"
                shift
                local source_files=("$@")
                
                for src_file in "${source_files[@]}"; do
                    [ ! -f "$src_file" ] && continue
                    
                    # Method 1: Look for DAP_MOCK_WRAPPER_DEFAULT
                    local default_sig=$(grep -A 3 "DAP_MOCK_WRAPPER_DEFAULT[[:space:]]*([^,]*,[[:space:]]*${func_name}[[:space:]]*," "$src_file" 2>/dev/null | \
                        tr '\n' ' ' | \
                        sed -E "s/.*DAP_MOCK_WRAPPER_DEFAULT[[:space:]]*\\([[:space:]]*([^,]+)[[:space:]]*,[[:space:]]*${func_name}[[:space:]]*,[[:space:]]*\\(([^)]+)\\).*/\\1|\\2/" | \
                        sed 's/^[[:space:]]*//' | \
                        sed 's/[[:space:]]*$//')
                    
                    if [ -n "$default_sig" ] && echo "$default_sig" | grep -q "|"; then
                        echo "$default_sig"
                        return 0
                    fi
                    
                    # Method 2: Look for explicit __wrap_func definition
                    local sig=$(grep -A 5 "__wrap_${func_name}[[:space:]]*(" "$src_file" 2>/dev/null | \
                        tr '\n' ' ' | \
                        sed -E 's/^([^{]*\)).*{.*/\1/' | \
                        sed 's/__wrap_//g' | \
                        sed 's/[[:space:]]+/ /g' | \
                        sed 's/^[[:space:]]*//' | \
                        sed 's/[[:space:]]*$//')
                    
                    if [ -n "$sig" ] && echo "$sig" | grep -q "${func_name}"; then
                        local ret_type=$(echo "$sig" | sed -E "s/^(.+)[[:space:]]+${func_name}[[:space:]]*\(.*/\1/" | sed 's/^[[:space:]]*//' | sed 's/[[:space:]]*$//')
                        local params=$(echo "$sig" | sed -E "s/.*${func_name}[[:space:]]*\(([^)]*)\).*/\1/" | sed 's/^[[:space:]]*//' | sed 's/[[:space:]]*$//')
                        [ -z "$params" ] && params="void"
                        echo "${ret_type}|${params}"
                        return 0
                    fi
                done
                echo ""
            }
            
            # Get source files from environment (passed from CMake)
            local source_files_str="${DAP_MOCK_SOURCE_FILES:-}"
            local -a source_files_arr=()
            if [ -n "$source_files_str" ]; then
                IFS=';' read -ra source_files_arr <<< "$source_files_str"
            fi
            
            # Pre-process all functions: collect data for template
            local FUNCTIONS_DATA=""
            while read func; do
                [ -z "$func" ] && continue
                
                # Lookup type info from file (bash 3.x compatible)
                local ret_type=$(_get_func_info "$type_lookup_file" "$func" 1)
                local param_list=$(_get_func_info "$type_lookup_file" "$func" 3)
                local param_count=$(_get_func_info "$type_lookup_file" "$func" 5)
                
                # Fallback: if type not found in custom_mocks, try to parse __wrap_ definition
                if [ -z "$ret_type" ] || [ "$ret_type" = "void*" ]; then
                    local sig=$(_extract_wrap_signature "$func" "${source_files_arr[@]}")
                    if [ -n "$sig" ]; then
                        ret_type=$(echo "$sig" | cut -d'|' -f1)
                        param_list=$(echo "$sig" | cut -d'|' -f2)
                        if [ "$param_list" = "void" ] || [ -z "$param_list" ]; then
                            param_count=0
                        else
                            param_count=$(echo "$param_list" | tr ',' '\n' | wc -l | tr -d ' ')
                        fi
                    fi
                fi
                
                # Default values if still not found
                [ -z "$ret_type" ] && ret_type="void*"
                [ -z "$param_count" ] && param_count=0
                
                # Determine if this is a struct return type
                local is_struct_return=0
                case "$ret_type" in
                    *"*"|void|int|long|short|char|size_t|ssize_t|intptr_t|uintptr_t|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|bool|_Bool)
                        is_struct_return=0
                        ;;
                    *)
                        is_struct_return=1
                        ;;
                esac
                
                # Generate parameter names for calling
                local param_names=""
                local param_decl=""
                if [ -z "$param_list" ] || [ "$param_list" = "void" ]; then
                    param_decl="void"
                    param_names=""
                else
                    param_decl="$param_list"
                    param_names=$(echo "$param_list" | awk -F',' '{for(i=1;i<=NF;i++){gsub(/^[[:space:]]+|[[:space:]]+$/,"",$i); n=split($i,a," "); name=a[n]; gsub(/^\*+/,"",name); printf "%s%s", name, (i<NF?", ":"")}}')
                fi
                
                # Append to FUNCTIONS_DATA (newline-separated, pipe-delimited fields)
                local entry="${func}|${ret_type}|${param_decl}|${param_names}|${is_struct_return}"
                if [ -n "$FUNCTIONS_DATA" ]; then
                    FUNCTIONS_DATA="${FUNCTIONS_DATA}"$'\n'"${entry}"
                else
                    FUNCTIONS_DATA="${entry}"
                fi
            done <<< "$mock_functions"
            
            # Generate interpose C file from template
            replace_template_placeholders_with_mocking \
                "${TEMPLATES_DIR}/interpose_typed.c.tpl" \
                "$interpose_c" \
                "FUNCTIONS_DATA=$FUNCTIONS_DATA"
            
            # Save C file path for CMake to compile (CMake has proper include paths)
            echo "$interpose_c" > "$dylib_path_file"
            
            print_success "Generated $func_count typed mock override functions (macOS .c)"
            print_info "C file: $interpose_c (will be compiled by CMake)"
            
            # wrap file stays empty on macOS - no linker options needed
            > "$wrap_file"
        else
            # Linux/BSD: Use --wrap=func (GNU ld)
            # GNU ld renames symbols at link time:
            #   func -> __real_func (original)
            #   __wrap_func -> func (wrapper becomes the function)
            echo "$mock_functions" | while read func; do
                # Note: For -Wl,@file usage, we need just --wrap=func (without -Wl,)
                echo "--wrap=$func" >> "$wrap_file"
            done
            print_success "Generated $func_count --wrap options (GNU ld)"
        fi
    else
        # Create truly empty file - linker response files cannot contain comments
        > "$wrap_file"
        print_info "Created empty wrap file (no mocks to wrap)"
    fi
}

# Generate CMake integration file
generate_cmake_file() {
    local cmake_file="$1"
    local mock_functions="$2"
    
    cat > "$cmake_file" << EOF
# Auto-generated mock configuration
# Generated by dap_mock_autowrap.sh

# Wrapped functions:
EOF

    if [ -n "$mock_functions" ]; then
        echo "$mock_functions" | while read func; do
            echo "#   - $func" >> "$cmake_file"
        done
    else
        echo "#   (none - no mocks declared)" >> "$cmake_file"
    fi
    
    print_success "Generated CMake integration"
}

# Generate macros header file
# Usage: generate_macros_file <macros_file> [custom_mocks_file]
generate_macros_file() {
    # CRITICAL: Save PARAM_COUNTS_ARRAY to local variable IMMEDIATELY to avoid corruption
    # Global PARAM_COUNTS_ARRAY may be corrupted by template processing functions
    local -a local_param_counts=("${PARAM_COUNTS_ARRAY[@]}")
    
    local macros_file="$1"
    local custom_mocks_file="${2:-${TMP_CUSTOM_MOCKS}}"
    local return_type_macros_file="${macros_file}.return_types"
    local simple_wrapper_macros_file="${macros_file}.simple_wrappers"
    local function_wrappers_file="${macros_file}.function_wrappers"
    
    # Generate return type macros using types module
    source "${LIB_DIR}/dap_mock_types.sh"
    generate_return_type_macros "$return_type_macros_file"
    
    # Generate function-specific wrapper macros (if custom_mocks_file is available)
    if [ -n "$custom_mocks_file" ] && [ -f "$custom_mocks_file" ]; then
        generate_function_wrappers "$function_wrappers_file" "$custom_mocks_file"
    else
        > "$function_wrappers_file"
    fi
    
    # Generate simple wrapper macros (if needed)
    > "$simple_wrapper_macros_file"
    
    # Prepare template data
    prepare_nargs_data "$MAX_ARGS_COUNT"
    prepare_map_count_params_by_count_data "$MAX_ARGS_COUNT"
    prepare_map_count_params_helper_data "$MAX_ARGS_COUNT"
    prepare_map_impl_cond_1_data "$MAX_ARGS_COUNT" "${PARAM_COUNTS_ARRAY[@]}"
    prepare_map_impl_cond_data "${PARAM_COUNTS_ARRAY[@]}"
    
    # Convert PARAM_COUNTS_ARRAY to pipe-separated string for dap_tpl
    PARAM_COUNTS_ARRAY_PIPE=$(IFS='|'; echo "${local_param_counts[*]}")
    
    # Generate macros header from master template
    # All sub-templates (mock_map_core, mock_map_n, etc.) are included via {{#include}}
    replace_template_placeholders_with_mocking \
        "${TEMPLATES_DIR}/mock_macros_header.h.tpl" \
        "$macros_file" \
        "RETURN_TYPE_MACROS_FILE=$return_type_macros_file" \
        "SIMPLE_WRAPPER_MACROS_FILE=$simple_wrapper_macros_file" \
        "FUNCTION_WRAPPERS_FILE=$function_wrappers_file" \
        "PARAM_COUNTS_ARRAY=$PARAM_COUNTS_ARRAY_PIPE" \
        "PARAM_COUNTS_ARRAY_PIPE=$PARAM_COUNTS_ARRAY_PIPE" \
        "MAX_ARGS_COUNT=$MAX_ARGS_COUNT" \
        "NARGS_IMPL_PARAMS=$NARGS_IMPL_PARAMS" \
        "NARGS_SEQUENCE=$NARGS_SEQUENCE" \
        "MAP_COUNT_PARAMS_BY_COUNT_DATA=$MAP_COUNT_PARAMS_BY_COUNT_DATA" \
        "MAP_COUNT_PARAMS_HELPER_DATA=$MAP_COUNT_PARAMS_HELPER_DATA" \
        "MAP_IMPL_COND_1_DATA=$MAP_IMPL_COND_1_DATA" \
        "MAP_IMPL_COND_DATA=$MAP_IMPL_COND_DATA"
    
    # Clean up temporary files
    rm -f "$return_type_macros_file" "$simple_wrapper_macros_file"
    
    print_success "Generated macros header with ${#local_param_counts[@]} parameter count(s)"
    if [ -n "$RETURN_TYPES" ]; then
        local return_types_count=$(echo "$RETURN_TYPES" | wc -w)
        print_success "Generated specialized macros for $return_types_count return type(s): $RETURN_TYPES"
    fi
}

# Generate dap_mock_linker_wrapper.h from template
generate_linker_wrapper_header() {
    local linker_wrapper_file="$1"
    
    # Prepare ORIGINAL_TYPES_DATA as string for template (normalized|original pairs)
    local ORIGINAL_TYPES_DATA=""
    if [ -n "$RETURN_TYPES_PAIRS" ]; then
        ORIGINAL_TYPES_DATA="$RETURN_TYPES_PAIRS"
        # Debug: verify ORIGINAL_TYPES_DATA contains expected types
        [ "$VERBOSE" = "1" ] && echo "DEBUG generate_linker_wrapper_header: RETURN_TYPES_PAIRS=[$RETURN_TYPES_PAIRS]" >&2
        [ "$VERBOSE" = "1" ] && echo "DEBUG generate_linker_wrapper_header: ORIGINAL_TYPES_DATA=[$ORIGINAL_TYPES_DATA]" >&2
    fi
    
    # Generate linker wrapper header from template
    # The template contains only base macros - dispatcher macros are generated separately
    replace_template_placeholders_with_mocking \
        "${TEMPLATES_DIR}/dap_mock_linker_wrapper.h.tpl" \
        "$linker_wrapper_file"
    
    print_success "Generated linker wrapper header"
}

# Generate wrapper template file
generate_template_file() {
    local template_file="$1"
    local mock_functions="$2"
    local wrapper_functions="$3"
    
    if [ -z "$mock_functions" ]; then
        print_info "No mocks declared - skipping template generation"
        return 0
    elif [ -n "$wrapper_functions" ]; then
        local missing_functions=$(comm -23 <(echo "$mock_functions" | sort) <(echo "$wrapper_functions" | sort))
        
        if [ -z "$missing_functions" ]; then
            print_success "All wrappers are defined"
            return 0
        else
            local missing_count=$(echo "$missing_functions" | wc -l)
            print_warning "Missing wrappers for $missing_count functions"
            print_info "Generating template: $template_file"
            
            # Generate template file using main template
            MISSING_FUNCTIONS="$missing_functions" \
            replace_template_placeholders_with_mocking \
                "${TEMPLATES_DIR}/wrapper_template.h.tpl" \
                "$template_file"
            
            # Show missing functions
            echo "$missing_functions" | while read func; do
                [ -z "$func" ] && continue
                echo "   WARNING: $func"
            done
            
            print_success "Template generated with $missing_count function stubs"
        fi
    else
        # No wrappers found but mocks exist - generate template for all
        local func_count=$(echo "$mock_functions" | wc -l)
        print_warning "No wrappers found for $func_count functions"
        print_info "Generating template: $template_file"
        
        # Generate template file using main template
        MISSING_FUNCTIONS="$mock_functions" \
        replace_template_placeholders_with_mocking \
            "${TEMPLATES_DIR}/wrapper_template.h.tpl" \
            "$template_file"
        
        # Show missing functions
        echo "$mock_functions" | while read func; do
            [ -z "$func" ] && continue
            echo "   WARNING: $func"
        done
        
        print_success "Template generated with $func_count function stubs"
    fi
}

# Generate custom mock headers for each custom mock declaration
# Usage: generate_custom_mock_headers <output_dir> <basename> <custom_mocks_file> <wrapper_functions> <mock_functions>
generate_custom_mock_headers() {
    local output_dir="$1"
    local basename="$2"
    local custom_mocks_file="$3"
    local wrapper_functions="$4"
    local temp_files=()  # Track temporary files for cleanup
    
    if [ ! -f "$custom_mocks_file" ] || [ ! -s "$custom_mocks_file" ]; then
        print_info "No custom mocks found - creating custom mocks header with macros only"
        local main_custom_mocks_file="${output_dir}/${basename}_custom_mocks.h"
        
        # Generate empty main include file from template
        replace_template_placeholders_with_mocking \
            "${TEMPLATES_DIR}/custom_mocks_main_empty.h.tpl" \
            "$main_custom_mocks_file" \
            "BASENAME=$basename"
        return 0
    fi
    
    local custom_mocks_count=$(wc -l < "$custom_mocks_file" | tr -d ' ')
    
    # Create directory for custom mock headers
    local custom_mocks_dir="${output_dir}/custom_mocks"
    mkdir -p "$custom_mocks_dir"
    
    # Process each custom mock declaration
    while IFS='|' read -r return_type func_name param_list macro_type; do
        [ -z "$func_name" ] && continue
        
        # Skip if function already has a wrapper defined in source files
        if echo "$wrapper_functions" | grep -q "^${func_name}$"; then
            continue
        fi
        
        # Create header file name based on function name
        local header_file="${custom_mocks_dir}/${func_name}_mock.h"
        
        # Parse parameters from param_list
        local param_decl=""
        local param_names=""
        local param_array=""
        local param_count=0
        
        if [ "$param_list" = "void" ] || [ -z "$param_list" ]; then
            param_decl="void"
            param_names=""
            param_array="NULL"
            param_count=0
        else
            # AWK script already processed PARAM(...) into "type name, type2 name2" format
            # Split by comma to get individual parameters
            local param_decl_parts=()
            local param_name_parts=()
            local param_array_parts=()
            
            # Split param_list by commas
            IFS=',' read -ra PARAMS <<< "$param_list"
            for param in "${PARAMS[@]}"; do
                # Trim whitespace
                param=$(echo "$param" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
                [ -z "$param" ] && continue
                
                # Extract last word as parameter name
                local param_name=$(echo "$param" | awk '{print $NF}')
                
                param_decl_parts+=("$param")
                param_name_parts+=("$param_name")
                param_array_parts+=("(void*)(intptr_t)$param_name")
            done
            
            # Join parameters with proper formatting
            if [ ${#param_decl_parts[@]} -gt 0 ]; then
                param_decl=$(IFS=', '; echo "${param_decl_parts[*]}")
                param_names=$(IFS=', '; echo "${param_name_parts[*]}")
                param_array="((void*[]){$(IFS=', '; echo "${param_array_parts[*]}")})"
                param_count=${#param_decl_parts[@]}
            else
                param_decl="void"
                param_names=""
                param_array="NULL"
                param_count=0
            fi
        fi
        
        # Prepare template variables
        local guard_name="${func_name^^}_MOCK_H"
        local wrapper_signature=""
        local result_declaration=""
        local mock_impl_call=""
        local real_function_call=""
        local return_value_override=""
        local record_call=""
        local return_statement=""
        local return_value_override_file=""
        local record_call_file=""
        
        if [ "$return_type" = "void" ]; then
            wrapper_signature="void __wrap_${func_name}($param_decl)"
            result_declaration=""
            if [ "$param_count" -eq 0 ]; then
                mock_impl_call="        __mock_impl_${func_name}();"
                real_function_call="        __real_${func_name}();"
            else
                mock_impl_call="        __mock_impl_${func_name}($param_names);"
                real_function_call="        __real_${func_name}($param_names);"
            fi
            return_value_override=""
            record_call=""
            return_statement=""
        else
            wrapper_signature="$return_type __wrap_${func_name}($param_decl)"
            result_declaration="    $return_type __wrap_result = ($return_type){0};"
            if [ "$param_count" -eq 0 ]; then
                mock_impl_call="        __wrap_result = __mock_impl_${func_name}();"
                real_function_call="        __wrap_result = __real_${func_name}();"
            else
                mock_impl_call="        __wrap_result = __mock_impl_${func_name}($param_names);"
                real_function_call="        __wrap_result = __real_${func_name}($param_names);"
            fi
            # Create temporary files for multi-line values
            return_value_override_file=$(create_temp_file "return_value_override_${func_name}")
            record_call_file=$(create_temp_file "record_call_${func_name}")
            temp_files+=("$return_value_override_file" "$record_call_file")
            {
                echo "        if (__wrap_mock_state && __wrap_mock_state->return_value.ptr) {"
                echo "            __wrap_result = *($return_type*)__wrap_mock_state->return_value.ptr;"
                echo "        }"
            } > "$return_value_override_file"
            echo "        dap_mock_record_call(__wrap_mock_state, __wrap_args, __wrap_args_count, (void*)(intptr_t)__wrap_result);" > "$record_call_file"
            return_value_override="@$return_value_override_file"
            record_call="@$record_call_file"
            return_statement="    return __wrap_result;"
        fi
        
        # Generate header file using template
        replace_template_placeholders_with_mocking \
            "${TEMPLATES_DIR}/custom_mock_header.h.tpl" \
            "$header_file" \
            "FUNC_NAME=$func_name" \
            "RETURN_TYPE=$return_type" \
            "PARAM_DECL=$param_decl" \
            "PARAM_NAMES=$param_names" \
            "PARAM_ARRAY=$param_array" \
            "PARAM_COUNT=$param_count" \
            "GUARD_NAME=$guard_name" \
            "WRAPPER_FUNCTION_SIGNATURE=$wrapper_signature" \
            "RESULT_DECLARATION=$result_declaration" \
            "MOCK_IMPL_CALL=$mock_impl_call" \
            "RETURN_VALUE_OVERRIDE=$return_value_override" \
            "RECORD_CALL=$record_call" \
            "REAL_FUNCTION_CALL=$real_function_call" \
            "RETURN_STATEMENT=$return_statement" \
            "ADDITIONAL_INCLUDES=${DAP_MOCK_ADDITIONAL_INCLUDES:-}"
        
        print_success "Generated mock header: $header_file"
    done < "$custom_mocks_file"
    
    # Clean up temporary files
    cleanup_temp_files "${temp_files[@]}"
    
    # Create main include file that includes all custom mock headers
    local main_custom_mocks_file="${output_dir}/${basename}_custom_mocks.h"
    
    # Read custom mocks list for template
    local CUSTOM_MOCKS_LIST=""
    CUSTOM_MOCKS_LIST=$(cat "$custom_mocks_file" 2>/dev/null | grep -v '^$' || true)
    
    # Generate main include file from template
    replace_template_placeholders_with_mocking \
        "${TEMPLATES_DIR}/custom_mocks_main.h.tpl" \
        "$main_custom_mocks_file" \
        "BASENAME=$basename" \
        "CUSTOM_MOCKS_LIST=$CUSTOM_MOCKS_LIST" \
        "WRAPPER_FUNCTIONS=$wrapper_functions"
    
    print_success "Generated main custom mocks include: $main_custom_mocks_file"
}

# Prepare MAP_IMPL_COND_1_DATA for template generation
# Format: arg_count|param_count|has_count|fallback_count|macro_params
# macro_params should NOT include "macro" - it's already in the macro parameter list
prepare_map_impl_cond_1_data() {
    local max_args_count="$1"
    shift
    local param_counts_array=("$@")
    
    [ -z "$max_args_count" ] && max_args_count=0
    [ "$max_args_count" -lt 0 ] && max_args_count=0
    
    declare -A HAS_PARAM_COUNT
    for count in "${param_counts_array[@]}"; do
        [ -z "$count" ] && continue
        HAS_PARAM_COUNT["$count"]=1
    done
    
    MAP_IMPL_COND_1_DATA=""
    for arg_count in $(seq 1 "$max_args_count"); do
        param_count=$((arg_count / 2))
        
        # Build macro_params starting with p1 (NOT macro - macro is already in parameter list)
        macro_params="p1"
        for j in $(seq 2 "$arg_count"); do
            macro_params="${macro_params}, p${j}"
        done
        
        has_count=0
        if [ "${HAS_PARAM_COUNT[$param_count]:-0}" = "1" ] || [ "$param_count" -eq 0 ]; then
            has_count=1
            fallback_count="$param_count"
        else
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

# Prepare MAP_IMPL_COND_DATA for template generation
prepare_map_impl_cond_data() {
    local param_counts_array=("$@")
    MAP_IMPL_COND_DATA=""
    for count in "${param_counts_array[@]}"; do
        [ -z "$count" ] && continue
        if [ -n "$MAP_IMPL_COND_DATA" ]; then
            MAP_IMPL_COND_DATA="${MAP_IMPL_COND_DATA}"$'\n'"${count}"
        else
            MAP_IMPL_COND_DATA="${count}"
        fi
    done
}

# Prepare NARGS data for template generation
# Generates data for _DAP_MOCK_NARGS macro generation
# Usage: prepare_nargs_data MAX_ARGS_COUNT
# Output: Sets NARGS_SEQUENCE and NARGS_IMPL_PARAMS variables
# Always generates full implementation (never simplified version)
prepare_nargs_data() {
    local max_args_count="$1"
    [ -z "$max_args_count" ] && max_args_count=0
    [ "$max_args_count" -lt 0 ] && max_args_count=0
    
    # Always ensure minimum of 2 args for full implementation
    local effective_max_args=$max_args_count
    [ "$effective_max_args" -lt 2 ] && effective_max_args=2
    
    # Prepare NARGS_SEQUENCE (always includes at least 2, 1, 0)
    NARGS_SEQUENCE=""
    for i in $(seq $effective_max_args -1 0); do
        if [ -n "$NARGS_SEQUENCE" ]; then
            NARGS_SEQUENCE="${NARGS_SEQUENCE}, $i"
        else
            NARGS_SEQUENCE=", $i"
        fi
    done
    
    # Prepare NARGS_IMPL_PARAMS (always includes at least _1, _2)
    NARGS_IMPL_PARAMS=""
    for i in $(seq 1 $effective_max_args); do
        if [ -n "$NARGS_IMPL_PARAMS" ]; then
            NARGS_IMPL_PARAMS="${NARGS_IMPL_PARAMS}, _$i"
        else
            NARGS_IMPL_PARAMS="_$i"
        fi
    done
}

# Prepare MAP_COUNT_PARAMS_BY_COUNT_DATA for template generation
prepare_map_count_params_by_count_data() {
    local max_args_count="$1"
    [ -z "$max_args_count" ] && max_args_count=2
    [ "$max_args_count" -lt 2 ] && max_args_count=2
    
    MAP_COUNT_PARAMS_BY_COUNT_DATA=""
    for arg_count in $(seq 0 "$max_args_count"); do
        if [ -n "$MAP_COUNT_PARAMS_BY_COUNT_DATA" ]; then
            MAP_COUNT_PARAMS_BY_COUNT_DATA="${MAP_COUNT_PARAMS_BY_COUNT_DATA}"$'\n'"${arg_count}"
        else
            MAP_COUNT_PARAMS_BY_COUNT_DATA="${arg_count}"
        fi
    done
}

# Prepare MAP_COUNT_PARAMS_HELPER_DATA for template generation
# Format: arg_count|param_count
prepare_map_count_params_helper_data() {
    local max_args_count="$1"
    [ -z "$max_args_count" ] && max_args_count=2
    [ "$max_args_count" -lt 2 ] && max_args_count=2
    
    MAP_COUNT_PARAMS_HELPER_DATA=""
    for arg_count in $(seq 0 "$max_args_count"); do
        param_count=$((arg_count / 2))
        if [ -n "$MAP_COUNT_PARAMS_HELPER_DATA" ]; then
            MAP_COUNT_PARAMS_HELPER_DATA="${MAP_COUNT_PARAMS_HELPER_DATA}"$'\n'"${arg_count}|${param_count}"
        else
            MAP_COUNT_PARAMS_HELPER_DATA="${arg_count}|${param_count}"
        fi
    done
}
