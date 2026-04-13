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
            # macOS: Generate typed override functions
            # =================================================================
            # Apple ld doesn't support --wrap, so we generate override functions
            # that are linked directly into the test executable.
            # Using proper types is CRITICAL for ARM64 ABI (struct returns!)
            
            local interpose_c="${wrap_dir}/mock_interpose.c"
            local dylib_path_file="${wrap_dir}/mock_dylib_path.txt"
            
            # Build lookup file from custom_mocks_file for type lookup
            # Format: return_type|func_name|param_list|macro_type|param_count
            # Note: Using file-based lookup instead of associative arrays for bash 3.x compatibility
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
                # Format: return_type|func_name|param_list|macro_type|param_count
                # Need to match |func_name| in the middle
                grep "|${func_name}|" "$lookup_file" 2>/dev/null | head -1 | cut -d'|' -f"$field"
            }
            
            # Helper to extract __wrap_func signature from source files
            # Returns: return_type|param_list
            # BSD-compatible (no gawk-specific features, uses plain awk)
            # Searches for:
            # 1. Explicit __wrap_func_name definitions
            # 2. DAP_MOCK_WRAPPER_DEFAULT(ret_type, func_name, (params), ...)
            _extract_wrap_signature() {
                local func_name="$1"
                shift
                local source_files=("$@")
                
                for src_file in "${source_files[@]}"; do
                    [ ! -f "$src_file" ] && continue
                    
                    # Method 1: Look for DAP_MOCK_WRAPPER_DEFAULT
                    # Format: DAP_MOCK_WRAPPER_DEFAULT(return_type, func_name, (params_decl), (param_names))
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
                        # Parse: "return_type func_name(type1 arg1, type2 arg2)"
                        local ret_type=$(echo "$sig" | sed -E "s/^(.+)[[:space:]]+${func_name}[[:space:]]*\(.*/\1/" | sed 's/^[[:space:]]*//' | sed 's/[[:space:]]*$//')
                        local params=$(echo "$sig" | sed -E "s/.*${func_name}[[:space:]]*\(([^)]*)\).*/\1/" | sed 's/^[[:space:]]*//' | sed 's/[[:space:]]*$//')
                        [ -z "$params" ] && params="void"
                        echo "${ret_type}|${params}"
                        return 0
                    fi
                done
                echo ""
            }
            
            # Collect #include directives from test source files
            local _src_includes=""
            local source_files_str_tmp="${DAP_MOCK_SOURCE_FILES:-}"
            if [ -n "$source_files_str_tmp" ]; then
                local IFS_OLD="$IFS"
                IFS=';'
                for _sf in $source_files_str_tmp; do
                    IFS="$IFS_OLD"
                    if [ -f "$_sf" ]; then
                        local _file_includes
                        _file_includes=$(grep -E '^\s*#\s*include\s+"' "$_sf" 2>/dev/null | \
                            grep -v '_custom_mocks\|_mock_macros\|mock_linker_wrapper' || true)
                        if [ -n "$_file_includes" ]; then
                            _src_includes="${_src_includes}${_file_includes}"$'\n'
                        fi
                    fi
                done
                IFS="$IFS_OLD"
            fi
            # Deduplicate includes
            _src_includes=$(echo "$_src_includes" | sort -u | grep -v '^$' || true)

            # Generate C file as DYLD interpose dylib source
            {
                cat << 'INTERPOSE_HEADER'
// Auto-generated DYLD interpose library for macOS
// Built as a shared library (dylib), loaded via DYLD_INSERT_LIBRARIES
// Uses __DATA,__interpose section for runtime function replacement
#ifdef __APPLE__
#include <stddef.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

INTERPOSE_HEADER

                # Add includes extracted from test sources
                if [ -n "$_src_includes" ]; then
                    echo "// Headers from test source files"
                    echo "$_src_includes"
                else
                    echo '#include "dap_common.h"'
                fi
                echo ""

                cat << 'INTERPOSE_TYPES'
typedef struct {
    const void *replacement;
    const void *replacee;
} dap_interpose_t;

INTERPOSE_TYPES
            } > "$interpose_c"

            # Get source files from environment (passed from CMake)
            local source_files_str="${DAP_MOCK_SOURCE_FILES:-}"
            local -a source_files_arr=()
            if [ -n "$source_files_str" ]; then
                IFS=';' read -ra source_files_arr <<< "$source_files_str"
            fi
            
            # Generate typed override functions for each mock
            # NOTE: Use while loop with redirect (not pipe) to preserve variables
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
                        # Count params
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
                
                # Determine if this is a pointer return type (safe for variadic cast)
                # or a struct return type (needs proper typing)
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
                    # Extract parameter names from "type1 name1, type2 name2, ..."
                    # Use awk to get last word from each comma-separated part, removing leading *
                    param_names=$(echo "$param_list" | awk -F',' '{for(i=1;i<=NF;i++){gsub(/^[[:space:]]+|[[:space:]]+$/,"",$i); n=split($i,a," "); name=a[n]; gsub(/^\*+/,"",name); printf "%s%s", name, (i<NF?", ":"")}}')
                fi
                
                if [ "$ret_type" = "void" ]; then
                    # Void return type
                    cat >> "$interpose_c" << VOID_FUNC_TPL

// === DYLD interpose for: ${func} (void return) ===
typedef void (*${func}_func_t)(${param_decl});

// __real_${func} for DAP_MOCK_WRAPPER_* macros
__attribute__((visibility("default")))
void __real_${func}(${param_decl}) {
    static ${func}_func_t s_real = NULL;
    if (!s_real) {
        s_real = (${func}_func_t)dlsym(RTLD_NEXT, "${func}");
        if (!s_real) {
            fprintf(stderr, "FATAL: dlsym(RTLD_NEXT, \\"${func}\\") failed: %s\\n", dlerror());
            abort();
        }
    }
    s_real(${param_names});
}

// Interpose replacement - redirects ${func} to __wrap_${func}
__attribute__((used))
static void _dap_interpose_${func}(${param_decl}) {
    static ${func}_func_t s_wrap = NULL;
    if (!s_wrap) {
        s_wrap = (${func}_func_t)dlsym(RTLD_DEFAULT, "__wrap_${func}");
        if (!s_wrap) {
            ${func}_func_t s_orig = (${func}_func_t)dlsym(RTLD_NEXT, "${func}");
            if (s_orig) { s_orig(${param_names}); return; }
            fprintf(stderr, "FATAL: __wrap_${func} not found\\n");
            abort();
        }
    }
    s_wrap(${param_names});
}

__attribute__((used, section("__DATA,__interpose")))
static const dap_interpose_t _dap_ip_${func} = {
    (const void *)_dap_interpose_${func},
    (const void *)${func}
};

VOID_FUNC_TPL
                else
                    # Non-void return type (both scalar and struct)
                    cat >> "$interpose_c" << TYPED_FUNC_TPL

// === DYLD interpose for: ${func} (return: ${ret_type}) ===
typedef ${ret_type} (*${func}_func_t)(${param_decl});

// __real_${func} for DAP_MOCK_WRAPPER_* macros
__attribute__((visibility("default")))
${ret_type} __real_${func}(${param_decl}) {
    static ${func}_func_t s_real = NULL;
    if (!s_real) {
        s_real = (${func}_func_t)dlsym(RTLD_NEXT, "${func}");
        if (!s_real) {
            fprintf(stderr, "FATAL: dlsym(RTLD_NEXT, \\"${func}\\") failed: %s\\n", dlerror());
            abort();
        }
    }
    return s_real(${param_names});
}

// Interpose replacement - redirects ${func} to __wrap_${func}
__attribute__((used))
static ${ret_type} _dap_interpose_${func}(${param_decl}) {
    static ${func}_func_t s_wrap = NULL;
    if (!s_wrap) {
        s_wrap = (${func}_func_t)dlsym(RTLD_DEFAULT, "__wrap_${func}");
        if (!s_wrap) {
            ${func}_func_t s_orig = (${func}_func_t)dlsym(RTLD_NEXT, "${func}");
            if (s_orig) return s_orig(${param_names});
            fprintf(stderr, "FATAL: __wrap_${func} not found\\n");
            abort();
        }
    }
    return s_wrap(${param_names});
}

__attribute__((used, section("__DATA,__interpose")))
static const dap_interpose_t _dap_ip_${func} = {
    (const void *)_dap_interpose_${func},
    (const void *)${func}
};

TYPED_FUNC_TPL
                fi
            done <<< "$mock_functions"
            echo "#endif // __APPLE__" >> "$interpose_c"
            
            # Save C file path for CMake to compile (CMake has proper include paths)
            echo "$interpose_c" > "$dylib_path_file"
            
            print_success "Generated $func_count DYLD interpose entries (macOS dylib source)"
            print_info "C file: $interpose_c (will be compiled as shared library by CMake)"
            
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
    # NOTE: prepare_map_macros_data() is NO LONGER USED - we generate macros directly now
    # prepare_map_macros_data "${PARAM_COUNTS_ARRAY[@]}"
    
    # Generate mock_map_macros content with template language constructs
    RETURN_TYPE_MACROS_FILE="$return_type_macros_file" \
    SIMPLE_WRAPPER_MACROS_FILE="$simple_wrapper_macros_file" \
    # Convert PARAM_COUNTS_ARRAY to pipe-separated string for dap_tpl for loop
    # dap_tpl for_evaluator expects pipe-separated or newline-separated arrays
    PARAM_COUNTS_ARRAY_PIPE=$(IFS='|'; echo "${PARAM_COUNTS_ARRAY[*]}")
    
    # NOTE: DO NOT set PARAM_COUNTS_ARRAY in environment here - it will corrupt the global array!
    # The template only needs PARAM_COUNTS_ARRAY_PIPE (passed as argument below)
    MAX_ARGS_COUNT="$MAX_ARGS_COUNT" \
    MAP_COUNT_PARAMS_BY_COUNT_DATA="$MAP_COUNT_PARAMS_BY_COUNT_DATA" \
    MAP_COUNT_PARAMS_HELPER_DATA="$MAP_COUNT_PARAMS_HELPER_DATA" \
    MAP_IMPL_COND_1_DATA="$MAP_IMPL_COND_1_DATA" \
    MAP_IMPL_COND_DATA="$MAP_IMPL_COND_DATA" \
    replace_template_placeholders_with_mocking \
        "${TEMPLATES_DIR}/mock_map_macros.h.tpl" \
        "${macros_file}.map_content" \
        "RETURN_TYPE_MACROS_FILE=$return_type_macros_file" \
        "SIMPLE_WRAPPER_MACROS_FILE=$simple_wrapper_macros_file" \
        "PARAM_COUNTS_ARRAY=$PARAM_COUNTS_ARRAY_PIPE" \
        "MAX_ARGS_COUNT=$MAX_ARGS_COUNT" \
        "MAP_COUNT_PARAMS_BY_COUNT_DATA=$MAP_COUNT_PARAMS_BY_COUNT_DATA" \
        "MAP_COUNT_PARAMS_HELPER_DATA=$MAP_COUNT_PARAMS_HELPER_DATA" \
        "MAP_IMPL_COND_1_DATA=$MAP_IMPL_COND_1_DATA" \
        "MAP_IMPL_COND_DATA=$MAP_IMPL_COND_DATA"
    
    # Export file paths for template processing
    export RETURN_TYPE_MACROS_FILE="$return_type_macros_file"
    export SIMPLE_WRAPPER_MACROS_FILE="$simple_wrapper_macros_file"
    export FUNCTION_WRAPPERS_FILE="$function_wrappers_file"
    export MAP_MACROS_CONTENT_FILE="${macros_file}.map_content"
    
    # Verify files exist before template processing
    if [ ! -f "$RETURN_TYPE_MACROS_FILE" ]; then
        print_error "RETURN_TYPE_MACROS_FILE does not exist: $RETURN_TYPE_MACROS_FILE"
        return 1
    fi
    
    # Save file paths before they might be overwritten in function calls
    local saved_return_type_macros_file="$RETURN_TYPE_MACROS_FILE"
    local saved_simple_wrapper_macros_file="$SIMPLE_WRAPPER_MACROS_FILE"
    
    # DIRECT GENERATION - bypass dap_tpl for huge MAP_MACROS_DATA
    # Generate header manually to avoid passing 805+ lines through AWK (mawk buffer overflow)
    
    # Write file header
    cat > "$macros_file" << 'EOF_HEADER'
/**
 * Auto-generated mock macros for DAP_MOCK_WRAPPER_CUSTOM
 * Generated by dap_mock_autowrap.sh
 * 
 * This file contains only the macros needed for this specific test target.
 * Do not modify manually - it will be regenerated.
 * 
 * This file is included via CMake's -include flag before dap_mock.h
 * No include guards needed - file is included unconditionally via -include
 * 
 * Note: dap_mock_linker_wrapper.h is included via #include in dap_mock.h
 */

// Include standard headers for size_t and other basic types
#include <stddef.h>

// Include base macros we need from dap_mock_linker_wrapper.h
// Since this file is included first, we need the base macros here
#ifndef _DAP_MOCK_NARGS_DEFINED
#define _DAP_MOCK_NARGS_DEFINED

EOF_HEADER

    # Generate NARGS macros
    echo "// Dynamically generated _DAP_MOCK_NARGS supporting up to $MAX_ARGS_COUNT arguments" >> "$macros_file"
    echo "// Full implementation - always uses parameter list (never simplified version)" >> "$macros_file"
    echo "#define _DAP_MOCK_NARGS_IMPL($NARGS_IMPL_PARAMS, N, ...) N" >> "$macros_file"
    echo "#define _DAP_MOCK_NARGS(...) _DAP_MOCK_NARGS_IMPL(__VA_ARGS__$NARGS_SEQUENCE)" >> "$macros_file"
    echo "" >> "$macros_file"
    echo "#define _DAP_MOCK_IS_EMPTY(...) \\" >> "$macros_file"
    echo "    (_DAP_MOCK_NARGS(__VA_ARGS__) == 0)" >> "$macros_file"
    echo "#endif // _DAP_MOCK_NARGS_DEFINED" >> "$macros_file"
    echo "" >> "$macros_file"
    echo "" >> "$macros_file"
    
    # Generate core _DAP_MOCK_MAP infrastructure macros
    generate_map_core_macros "$macros_file" "${local_param_counts[@]}"
    echo "" >> "$macros_file"
    
    # Generate MAP_N macros directly - NO dap_tpl, NO AWK
    # This is the key fix for mawk buffer overflow (no sprintf limits)
    for count in "${local_param_counts[@]}"; do
        [ -z "$count" ] && continue
        generate_single_map_macro "$count" >> "$macros_file"
        echo "" >> "$macros_file"
    done
    
    # Append return type macros if they exist
    if [ -n "$saved_return_type_macros_file" ] && [ -f "$saved_return_type_macros_file" ] && [ -s "$saved_return_type_macros_file" ]; then
        cat "$saved_return_type_macros_file" >> "$macros_file"
    fi
    
    # Append simple wrapper macros if they exist
    if [ -n "$saved_simple_wrapper_macros_file" ] && [ -f "$saved_simple_wrapper_macros_file" ] && [ -s "$saved_simple_wrapper_macros_file" ]; then
        cat "$saved_simple_wrapper_macros_file" >> "$macros_file"
    fi
    
    # Append function wrappers if they exist
    if [ -n "$function_wrappers_file" ] && [ -f "$function_wrappers_file" ] && [ -s "$function_wrappers_file" ]; then
        cat "$function_wrappers_file" >> "$macros_file"
    fi
    
    # Clean up temporary files AFTER template processing is complete
    # Note: map_content file is included via {{#include}}, so it must exist during template processing
    # It will be cleaned up after the final template is generated
    rm -f "$return_type_macros_file" "$simple_wrapper_macros_file"
    # Keep map_content file for now - it's needed for include processing
    # It will be cleaned up later if needed
    
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
                echo "   ⚠️  $func"
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
            echo "   ⚠️  $func"
        done
        
        print_success "Template generated with $func_count function stubs"
    fi
}

# Generate custom mock headers for each custom mock declaration
# Usage: generate_custom_mock_headers <output_dir> <basename> <custom_mocks_file> <wrapper_functions>
generate_custom_mock_headers() {
    local output_dir="$1"
    local basename="$2"
    local custom_mocks_file="$3"
    local wrapper_functions="$4"
    local temp_files=()  # Track temporary files for cleanup
    
    if [ ! -f "$custom_mocks_file" ] || [ ! -s "$custom_mocks_file" ]; then
        print_info "No custom mocks found - creating custom mocks header with macros only"
        local main_custom_mocks_file="${output_dir}/${basename}_custom_mocks.h"
        
        # Generate empty main include file manually (no template needed)
        cat > "$main_custom_mocks_file" <<EOF
// Auto-generated main include file for all custom mocks
// Generated by dap_mock_autowrap.sh
// Do not modify manually

#include "${basename}_mock_macros.h"

// No custom mocks found - only includes macros header

EOF
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
                param_array_parts+=("({ void *_p = NULL; __builtin_memcpy(&_p, &$param_name, sizeof($param_name)); _p; })")
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
            echo "        { void *_rr = NULL; __builtin_memcpy(&_rr, &__wrap_result, sizeof(__wrap_result)); dap_mock_record_call(__wrap_mock_state, __wrap_args, __wrap_args_count, _rr); }" > "$record_call_file"
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
    
    # Generate main include file directly (template has issues with complex data)
    {
        echo "// Auto-generated main include file for all custom mocks"
        echo "// Generated by dap_mock_autowrap.sh"
        echo "// Do not modify manually"
        echo ""
        echo "#include \"${basename}_mock_macros.h\""
        echo ""
        
        # Include each custom mock header (unless already has wrapper)
        while IFS='|' read -r return_type func_name param_list macro_type; do
            [ -z "$func_name" ] && continue
            
            # Skip if function already has a wrapper defined in source files
            if echo "$wrapper_functions" | grep -q "^${func_name}$"; then
                continue
            fi
            
            echo "#include \"custom_mocks/${func_name}_mock.h\""
        done < "$custom_mocks_file"
        
        echo ""
    } > "$main_custom_mocks_file"
    
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
    
    # POSIX-compatible set membership (no associative arrays — bash 3.2 on macOS)
    _has_param_count_str=""
    for count in "${param_counts_array[@]}"; do
        [ -z "$count" ] && continue
        _has_param_count_str="${_has_param_count_str}|${count}|"
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
        if echo "$_has_param_count_str" | grep -q "|${param_count}|" || [ "$param_count" -eq 0 ]; then
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

# Generate single MAP macro definition using pure bash (no AWK)
# Avoids mawk sprintf buffer limit (8KB) for large parameter counts
# Usage: generate_single_map_macro COUNT
# Output: Two-level macro for forced expansion of PARAM(type, name) before processing
#   #define _DAP_MOCK_MAP_N(macro, ...) _DAP_MOCK_MAP_N_IMPL(macro, __VA_ARGS__)
#   #define _DAP_MOCK_MAP_N_IMPL(macro, type1, name1, ...) macro(type1, name1), ...
generate_single_map_macro() {
    local count="$1"
    
    if [ "$count" -eq 0 ]; then
        # Special case for 0 params - accept and ignore extra args (e.g. 'void')
        echo "#define _DAP_MOCK_MAP_0(macro) \\"
        echo "    "
        return
    fi
    
    # First level: forces expansion of __VA_ARGS__ (PARAM macros expand here)
    echo -n "#define _DAP_MOCK_MAP_${count}(macro, ...) "
    echo "_DAP_MOCK_MAP_${count}_IMPL(macro, __VA_ARGS__)"
    
    # Second level: receives expanded arguments
    echo -n "#define _DAP_MOCK_MAP_${count}_IMPL(macro"
    
    # Generate parameter list: , type1, name1, type2, name2, ...
    for ((i=1; i<=count; i++)); do
        echo -n ", type${i}, name${i}"
    done
    
    # Start macro body
    echo -n ") \\"
    echo ""
    echo -n "    "
    
    # Generate macro invocations: macro(type1, name1), macro(type2, name2), ...
    for ((i=1; i<=count; i++)); do
        if [ "$i" -gt 1 ]; then
            echo -n ", "
        fi
        echo -n "macro(type${i}, name${i})"
    done
    echo ""
}

# Generate core _DAP_MOCK_MAP infrastructure macros
# These are the routing macros that dispatch to _DAP_MOCK_MAP_N based on parameter count
# Usage: generate_map_core_macros OUTPUT_FILE PARAM_COUNTS...
generate_map_core_macros() {
    local output_file="$1"
    shift
    local param_counts=("$@")
    
    cat >> "$output_file" << 'EOF_CORE_HEADER'
// ============================================================================
// Core _DAP_MOCK_MAP macros - Routes to _DAP_MOCK_MAP_N based on parameter count
// ============================================================================
// Main entry point for mapping macros over PARAM entries
// Each PARAM expands to 2 arguments (type, name), so we need to divide arg count by 2

#define _DAP_MOCK_MAP(macro, ...) \
    _DAP_MOCK_MAP_EXPAND_1(macro, __VA_ARGS__)

// Helper to extract first argument
#define _DAP_MOCK_GET_FIRST(first, ...) first

EOF_CORE_HEADER

    # Generate expansion levels (6 levels for proper macro expansion)
    local max_level=6
    for ((i=1; i<=max_level; i++)); do
        if [ "$i" -lt "$max_level" ]; then
            echo "#define _DAP_MOCK_MAP_EXPAND_${i}(macro, ...) \\" >> "$output_file"
            echo "    _DAP_MOCK_MAP_EXPAND_$((i+1))(macro, __VA_ARGS__)" >> "$output_file"
        else
            echo "#define _DAP_MOCK_MAP_EXPAND_${i}(macro, ...) \\" >> "$output_file"
            echo "    _DAP_MOCK_MAP_CHECK_VOID(_DAP_MOCK_NARGS(__VA_ARGS__), _DAP_MOCK_MAP_COUNT_PARAMS(__VA_ARGS__), macro, __VA_ARGS__)" >> "$output_file"
        fi
    done
    echo "" >> "$output_file"
    
    cat >> "$output_file" << 'EOF_CHECK_VOID'
// CHECK_VOID receives arguments in safe order
#define _DAP_MOCK_MAP_CHECK_VOID(arg_count, param_count, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_IMPL(arg_count, param_count, macro, _DAP_MOCK_GET_FIRST(__VA_ARGS__), __VA_ARGS__)

// IMPL receives extracted first_arg
#define _DAP_MOCK_MAP_CHECK_VOID_IMPL(arg_count, param_count, macro, first_arg, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND(param_count, first_arg, macro, __VA_ARGS__)

// Multi-level expansion to ensure param_count is fully expanded before routing
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND2(param_count, first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND2(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND3(param_count, first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND3(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND4(param_count, first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND4(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE(param_count, first_arg, macro, __VA_ARGS__)

// Route to specific param_count handler
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE_EXPAND(param_count, first_arg, macro, __VA_ARGS__)

// Expand to ensure numeric value is pasted
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE_EXPAND(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE_##param_count(param_count, first_arg, macro, __VA_ARGS__)

EOF_CHECK_VOID

    # Generate routing macros for each param_count value
    for count in "${param_counts[@]}"; do
        [ -z "$count" ] && continue
        if [ "$count" -eq 0 ]; then
            echo "// Routing for param_count=0 — routes through first-arg check for void handling" >> "$output_file"
            echo "#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE_0(param_count_val, first_arg, macro, ...) \\" >> "$output_file"
            echo "    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK(first_arg, macro, __VA_ARGS__)" >> "$output_file"
        else
            echo "// Routing for param_count=${count}" >> "$output_file"
            echo "#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE_${count}(param_count_val, first_arg, macro, ...) \\" >> "$output_file"
            echo "    _DAP_MOCK_MAP_IMPL_COND_${count}(macro, __VA_ARGS__)" >> "$output_file"
        fi
    done
    echo "" >> "$output_file"

    # Generate first-arg routing macros (for param_count=0 case: void vs empty)
    cat >> "$output_file" << 'EOF_FIRST_ARG_ROUTING'
// ============================================================================
// First-arg routing for param_count=0: handles "void" vs empty
// ============================================================================
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK(first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE(first_arg, macro, __VA_ARGS__)

#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE(first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE_EXPAND(first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE_EXPAND(first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE_IMPL_##first_arg(first_arg, macro, __VA_ARGS__)

// first_arg="void" — dispatch by macro name to produce correct expansion
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE_IMPL_void(first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO_##macro()

// first_arg="" (empty) — just call _DAP_MOCK_MAP_0
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE_IMPL_(first_arg, macro, ...) \
    _DAP_MOCK_MAP_0(macro)

// Per-macro void expansions
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO__DAP_MOCK_PARAM_DECL() void
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO__DAP_MOCK_PARAM_NAME()
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO__DAP_MOCK_PARAM_CAST()

EOF_FIRST_ARG_ROUTING

    # Generate _DAP_MOCK_MAP_IMPL_COND macros for non-zero param counts
    echo "// ============================================================================" >> "$output_file"
    echo "// Implementation conditional macros" >> "$output_file"
    echo "// ============================================================================" >> "$output_file"
    for count in "${param_counts[@]}"; do
        [ -z "$count" ] && continue
        [ "$count" -eq 0 ] && continue
        echo "#define _DAP_MOCK_MAP_IMPL_COND_${count}(macro, ...) \\" >> "$output_file"
        echo "    _DAP_MOCK_MAP_${count}(macro, __VA_ARGS__)" >> "$output_file"
    done
    echo "" >> "$output_file"

    # Generate _DAP_MOCK_MAP_COUNT_PARAMS with preprocessor-level dispatch
    # (NOT C-level arithmetic — must produce a token usable in ## pasting)
    cat >> "$output_file" << 'EOF_COUNT_PARAMS_HEADER'
// ============================================================================
// Parameter counting macros — preprocessor-level dispatch
// ============================================================================
#define _DAP_MOCK_MAP_COUNT_PARAMS(...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND(__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND(...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND2(__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND2(...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL(_DAP_MOCK_NARGS(__VA_ARGS__), __VA_ARGS__)

#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL(arg_count, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_EXPAND(arg_count, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_EXPAND(arg_count, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_EXPAND2(arg_count, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_EXPAND2(arg_count, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_##arg_count(arg_count, ##__VA_ARGS__)

// arg_count=0 → 0 params
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_0(arg_count_val, ...) 0

// arg_count=1 → check for "void" keyword
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1(arg_count_val, first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK(first_arg)
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK(first_arg) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK_EXPAND(first_arg)
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK_EXPAND(first_arg) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK_##first_arg

#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK_void 0
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK_ 0

EOF_COUNT_PARAMS_HEADER

    # Generate arg_count→param_count mappings for actual param counts
    # Each PARAM(type, name) expands to 2 args, so arg_count = param_count * 2
    for count in "${param_counts[@]}"; do
        [ -z "$count" ] && continue
        [ "$count" -eq 0 ] && continue
        local arg_count=$((count * 2))
        echo "#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_${arg_count}(arg_count_val, ...) ${count}" >> "$output_file"
    done
    echo "" >> "$output_file"
}

# Prepare MAP_MACROS_DATA for template generation
# Uses pure bash generation to avoid mawk sprintf buffer overflow (8KB limit)
# NOTE: This function is DEPRECATED - macros are now generated directly in generate_macros_file()
# Kept for backward compatibility but does NOT produce pipe-separated data
prepare_map_macros_data() {
    local param_counts_array=("$@")
    
    # Do nothing - direct generation is used now
    # This function is kept to avoid breaking old code that might call it
    MAP_MACROS_DATA=""
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
    # This ensures we always have a proper _DAP_MOCK_NARGS_IMPL definition
    # Minimum needed: _1, _2 for proper macro expansion
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
    # Format: single line with comma-separated parameters: _1, _2, _3, ...
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
