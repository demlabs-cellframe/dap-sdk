#!/bin/bash
# DAP Template Processing Library
# Provides functions for processing template files with placeholders and embedded scripts
#
# Usage:
#   source dap_tpl.sh
#   replace_template_placeholders <template_file> <output_file> <var1=value1> <var2=value2> ...

set -e

# Colors for output (if not already defined)
if [ -z "${RED:-}" ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m' # No Color
fi

# Print error message
tpl_print_error() {
    echo -e "${RED}❌ $1${NC}" >&2
}

# Function to replace placeholders in template file
# Usage: replace_template_placeholders <template_file> <output_file> <var1=value1> <var2=value2> ...
# For multi-line values, use a temporary file: replace_template_placeholders <template> <output> <var1=@file1> <var2=value2>
# 
# Environment variables:
#   SCRIPTS_DIR - directory containing AWK scripts for template processing
#   TEMPLATES_DIR - directory containing template files (optional)
replace_template_placeholders() {
    local template_file="$1"
    local output_file="$2"
    # Save original arguments before shifting (needed for placeholder replacement loop)
    local placeholder_args=("$@")
    shift 2
    
    if [ ! -f "$template_file" ]; then
        tpl_print_error "Template file not found: $template_file"
        return 1
    fi
    
    if [ -z "${SCRIPTS_DIR:-}" ]; then
        tpl_print_error "SCRIPTS_DIR is not set"
        return 1
    fi
    
    # Export all variables passed as arguments (VAR=value) to ENVIRON
    # This allows AWK scripts to access them via ENVIRON array
    local var_args=("$@")
    for var_arg in "${var_args[@]}"; do
        if [[ "$var_arg" == *"="* ]]; then
            local var_name="${var_arg%%=*}"
            local var_val="${var_arg#*=}"
            # Export variable to ENVIRON for AWK scripts
            export "$var_name"="$var_val"
        fi
    done
    
    # Ensure environment variables are exported for child processes
    # These are read by AWK scripts via ENVIRON array
    # Always export (even if empty) - AWK scripts check for empty themselves
    # Use direct assignment to preserve multi-line values
    # Debug: check if CUSTOM_MOCKS_LIST is set before exporting
    if [ -n "${CUSTOM_MOCKS_LIST:-}" ]; then
        # Variable is set - export it
        export CUSTOM_MOCKS_LIST="${CUSTOM_MOCKS_LIST}"
    else
        # Variable is not set - don't overwrite with empty
        # This preserves values that were exported before function call
        :
    fi
    export WRAPPER_FUNCTIONS="${WRAPPER_FUNCTIONS:-}"
    export PARAM_COUNTS_ARRAY="${PARAM_COUNTS_ARRAY:-}"
    export MAX_ARGS_COUNT="${MAX_ARGS_COUNT:-0}"
    export NARGS_SEQUENCE="${NARGS_SEQUENCE:-}"
    export NARGS_IMPL_PARAMS="${NARGS_IMPL_PARAMS:-}"
    export MAP_MACROS_DATA="${MAP_MACROS_DATA:-}"
    export MISSING_FUNCTIONS="${MISSING_FUNCTIONS:-}"
    export MAP_MACROS_CONTENT_FILE="${MAP_MACROS_CONTENT_FILE:-}"
    export RETURN_TYPE_MACROS_FILE="${RETURN_TYPE_MACROS_FILE:-}"
    export SIMPLE_WRAPPER_MACROS_FILE="${SIMPLE_WRAPPER_MACROS_FILE:-}"
    export MAP_COUNT_PARAMS_BY_COUNT_DATA="${MAP_COUNT_PARAMS_BY_COUNT_DATA:-}"
    export MAP_COUNT_PARAMS_HELPER_DATA="${MAP_COUNT_PARAMS_HELPER_DATA:-}"
    export MAP_IMPL_COND_1_DATA="${MAP_IMPL_COND_1_DATA:-}"
    export MAP_IMPL_COND_DATA="${MAP_IMPL_COND_DATA:-}"
    # Export arrays for template constructs (set by shell scripts in templates)
    export POINTER_TYPES_ARRAY="${POINTER_TYPES_ARRAY:-}"
    export NON_POINTER_TYPES_ARRAY="${NON_POINTER_TYPES_ARRAY:-}"
    # Export normalization macros data for AWK scripts
    export NORMALIZATION_MACROS_DATA="${NORMALIZATION_MACROS_DATA:-}"
    
    # Extract all sections before processing
    local sections_file=$(mktemp)
    awk -f "${SCRIPTS_DIR}/extract_sections.awk" "$template_file" > "$sections_file"
    
    # Start with original template file
    local temp_file=$(mktemp)
    cp "$template_file" "$temp_file"
    
    # Process template constructs (if/for/set/var/awk) first
    # This allows constructs to be used within generation sections
    local constructs_file=$(mktemp)
    awk -f "${SCRIPTS_DIR}/parse_template_constructs.awk" "$temp_file" > "$constructs_file" 2>/dev/null || true
    
    if [ -s "$constructs_file" ]; then
        # Process constructs and replace in template
        local constructs_processed=$(mktemp)
        awk -f "${SCRIPTS_DIR}/process_template_constructs.awk" "$constructs_file" > "$constructs_processed" 2>/dev/null || true
        
        if [ -s "$constructs_processed" ]; then
            # Replace constructs in template file
            # Use AWK to replace construct markers with processed content
            awk -v constructs_file="$constructs_file" \
                -v processed_file="$constructs_processed" \
                -f "${SCRIPTS_DIR}/replace_template_constructs.awk" \
                "$temp_file" > "${temp_file}.constructs" 2>/dev/null || cp "$temp_file" "${temp_file}.constructs"
            
            if [ -s "${temp_file}.constructs" ]; then
                mv "${temp_file}.constructs" "$temp_file"
            fi
        fi
        rm -f "$constructs_processed"
    fi
    rm -f "$constructs_file"
    
    # Process content generation sections first ({{AWK:...}} and {{#/bin/sh:...}})
    if [ -s "$sections_file" ]; then
        # Use awk to filter gen sections while preserving multi-line content
        # This replaces grep which would break multi-line sections
        local gen_sections_file=$(mktemp)
        awk '
            BEGIN {
                in_gen_section = 0
            }
            /^awk_gen\|/ {
                # Start of gen section
                print
                in_gen_section = 1
                next
            }
            /^sh_gen\|/ {
                # Start of gen section
                print
                in_gen_section = 1
                next
            }
            in_gen_section == 1 {
                # Continue outputting lines until we hit next section header
                if (/^awk_gen\|/ || /^sh_gen\|/) {
                    # New gen section - output and continue
                    print
                    in_gen_section = 1
                } else if (/^awk_postproc\|/) {
                    # Postproc section - stop current gen section
                    in_gen_section = 0
                } else {
                    # Continue current gen section
                    print
                }
                next
            }
        ' "$sections_file" > "$gen_sections_file" || true
        
        if [ -s "$gen_sections_file" ]; then
            # Use awk script to properly parse multi-line sections
            awk -f "${SCRIPTS_DIR}/parse_multiline_sections.awk" "$gen_sections_file" > "${gen_sections_file}.parsed" || true
            
            if [ -s "${gen_sections_file}.parsed" ]; then
                # Process each section using awk script to handle multi-line section_code and execute scripts
                # Re-export variables right before AWK call to ensure they're available
                # AWK reads ENVIRON array at startup, so variables must be exported before AWK starts
                # Always export (even if empty) - AWK scripts check for empty themselves
                # Use direct assignment to preserve multi-line values
                export CUSTOM_MOCKS_LIST="${CUSTOM_MOCKS_LIST}"
                export WRAPPER_FUNCTIONS="${WRAPPER_FUNCTIONS}"
                export PARAM_COUNTS_ARRAY="${PARAM_COUNTS_ARRAY}"
                export MAX_ARGS_COUNT="${MAX_ARGS_COUNT}"
                export MISSING_FUNCTIONS="${MISSING_FUNCTIONS}"
                # Export arrays for template constructs
                export POINTER_TYPES_ARRAY="${POINTER_TYPES_ARRAY:-}"
                export NON_POINTER_TYPES_ARRAY="${NON_POINTER_TYPES_ARRAY:-}"
                # Export file paths for template constructs (set by shell scripts in templates)
                export POINTER_TYPES_FILE="${POINTER_TYPES_FILE:-}"
                export NON_POINTER_TYPES_FILE="${NON_POINTER_TYPES_FILE:-}"
                # Export normalization macros data for AWK scripts
                export NORMALIZATION_MACROS_DATA="${NORMALIZATION_MACROS_DATA:-}"
                # Export SCRIPTS_DIR for @include support in AWK
                export SCRIPTS_DIR="${SCRIPTS_DIR}"
                awk -f "${SCRIPTS_DIR}/process_gen_sections.awk" \
                    -v temp_file="$temp_file" \
                    "${gen_sections_file}.parsed" > "${gen_sections_file}.processed" || true
                
                if [ -s "${gen_sections_file}.processed" ]; then
                    # Debug: save processed file for custom_mocks_main template
                    if [ "$template_file" = "${TEMPLATES_DIR}/custom_mocks_main.h.tpl" ]; then
                        cp "${gen_sections_file}.processed" "/tmp/last_processed_custom_mocks.txt" 2>/dev/null || true
                    fi
                    
                    # Use awk to read processed sections and replace markers
                    # This handles multi-line generated_content properly
                    # Use original template_file to determine line ranges, replace in temp_file
                    awk -f "${SCRIPTS_DIR}/replace_gen_sections.awk" \
                        "${gen_sections_file}.processed" "$template_file" "$temp_file" > "${temp_file}.gen"
                    # Debug: save generated file for inspection
                    cp "${temp_file}.gen" "/tmp/last_replace_gen_output.txt" 2>/dev/null || true
                    if [ -s "${temp_file}.gen" ]; then
                        # Remove postproc sections from generated file using remove_sections.awk
                        # This removes AWK code blocks without markers that replace_gen_sections.awk didn't remove
                        local temp_file_no_postproc=$(mktemp)
                        awk -f "${SCRIPTS_DIR}/remove_sections.awk" "${temp_file}.gen" > "$temp_file_no_postproc"
                        if [ -s "$temp_file_no_postproc" ]; then
                            mv "$temp_file_no_postproc" "${temp_file}.gen"
                        else
                            echo "ERROR: remove_sections.awk produced empty file after replace_gen_sections" >&2
                            rm -f "$temp_file_no_postproc"
                        fi
                        # Debug: check if removal worked
                        if [ "$template_file" = "${TEMPLATES_DIR}/mock_macros_header.h.tpl" ]; then
                            cp "${temp_file}.gen" "/tmp/after_remove_sections_from_gen.txt" 2>/dev/null || true
                        fi
                        mv "${temp_file}.gen" "$temp_file"
                    else
                        # Debug: check what went wrong
                        echo "ERROR: Generated file is empty after replace_gen_sections" >&2
                        echo "Processed sections file:" >&2
                        head -5 "${gen_sections_file}.processed" >&2
                        echo "Template file first 10 lines:" >&2
                        head -10 "$template_file" >&2
                        echo "Temp file first 10 lines:" >&2
                        head -10 "$temp_file" >&2
                        rm -f "${temp_file}.gen"
                    fi
                fi
                rm -f "${gen_sections_file}.processed"
            fi
            rm -f "${gen_sections_file}.parsed" "$gen_sections_file"
        fi
    fi
    
    # Debug: check temp_file before placeholder replacement
    if [ "$template_file" = "${TEMPLATES_DIR}/mock_macros_header.h.tpl" ]; then
        cp "$temp_file" "/tmp/before_placeholder_replace.txt" 2>/dev/null || true
        grep -c "# Post-process" "/tmp/before_placeholder_replace.txt" 2>/dev/null || echo "0"
    fi
    
    # Replace each placeholder (always, not just when sections exist)
    # Use saved arguments (shifted at function start)
    local arg_idx=0
    while [ $arg_idx -lt ${#placeholder_args[@]} ]; do
            local var_value="${placeholder_args[$arg_idx]}"
            local var_name="${var_value%%=*}"
            local var_val="${var_value#*=}"
            
            # Export placeholder variables as environment variables for AWK scripts
            if [ "$var_name" = "MAP_MACROS_CONTENT_FILE" ]; then
                export MAP_MACROS_CONTENT_FILE="$var_val"
            elif [ "$var_name" = "RETURN_TYPE_MACROS_FILE" ]; then
                export RETURN_TYPE_MACROS_FILE="$var_val"
            elif [ "$var_name" = "SIMPLE_WRAPPER_MACROS_FILE" ]; then
                export SIMPLE_WRAPPER_MACROS_FILE="$var_val"
            fi
            
            # Debug: save temp_file before replacement
            if [ "$template_file" = "${TEMPLATES_DIR}/mock_macros_header.h.tpl" ]; then
                cp "$temp_file" "/tmp/before_replace_${var_name}.txt" 2>/dev/null || true
            fi
            
            # Check if value is a file reference (@filename)
            if [ "${var_val:0:1}" = "@" ]; then
                local val_file="${var_val:1}"
                if [ ! -f "$val_file" ]; then
                    tpl_print_error "Value file not found: $val_file"
                    rm -f "$temp_file" "$sections_file"
                    return 1
                fi
                # Use awk script to replace placeholder with file content directly
                awk -v placeholder="{{${var_name}}}" -v val_file="$val_file" \
                    -f "${SCRIPTS_DIR}/replace_template_file.awk" \
                    "$temp_file" > "${temp_file}.new"
            else
                # Simple value replacement using awk script
                awk -v placeholder="{{${var_name}}}" -v val="$var_val" \
                    -f "${SCRIPTS_DIR}/replace_template_value.awk" \
                    "$temp_file" > "${temp_file}.new"
            fi
            
            # Debug: save temp_file after replacement
            if [ "$template_file" = "${TEMPLATES_DIR}/mock_macros_header.h.tpl" ]; then
                cp "${temp_file}.new" "/tmp/after_replace_${var_name}.txt" 2>/dev/null || true
            fi
            
            if [ -s "${temp_file}.new" ]; then
                mv "${temp_file}.new" "$temp_file"
            else
                echo "ERROR: Replacement result is empty for ${var_name}" >&2
                rm -f "${temp_file}.new"
            fi
            arg_idx=$((arg_idx + 1))
        done
        
        # Remove postproc sections from temp_file after placeholder replacement
        # This ensures they don't interfere with post-processing
        local temp_file_no_postproc_after=$(mktemp)
        awk -f "${SCRIPTS_DIR}/remove_sections.awk" "$temp_file" > "$temp_file_no_postproc_after"
        if [ -s "$temp_file_no_postproc_after" ]; then
            mv "$temp_file_no_postproc_after" "$temp_file"
        else
            echo "ERROR: remove_sections.awk produced empty file after placeholder replacement" >&2
            rm -f "$temp_file_no_postproc_after"
        fi
        
        # Process post-processing sections ({{postproc:{{AWK:...}}}})
        # Filter postproc sections using dedicated AWK script
        local postproc_sections_file=$(mktemp)
        awk -f "${SCRIPTS_DIR}/filter_postproc_sections.awk" "$sections_file" > "$postproc_sections_file" || true
        
        if [ -s "$postproc_sections_file" ]; then
            # First, remove postproc section markers from temp_file using remove_sections.awk
            # This removes the entire {{postproc:{{AWK:...}}}} section including markers
            # Updated remove_sections.awk also removes AWK code blocks without markers
            local temp_file_no_postproc=$(mktemp)
            awk -f "${SCRIPTS_DIR}/remove_sections.awk" "$temp_file" > "$temp_file_no_postproc"
            
            # Debug: check if removal worked
            if [ "$template_file" = "${TEMPLATES_DIR}/mock_macros_header.h.tpl" ]; then
                cp "$temp_file_no_postproc" "/tmp/after_remove_sections.txt" 2>/dev/null || true
            fi
            
            if [ -s "$temp_file_no_postproc" ]; then
                # Debug: check if removal worked
                if [ "$template_file" = "${TEMPLATES_DIR}/mock_macros_header.h.tpl" ]; then
                    cp "$temp_file" "/tmp/before_remove_sections.txt" 2>/dev/null || true
                    cp "$temp_file_no_postproc" "/tmp/after_remove_sections.txt" 2>/dev/null || true
                fi
                mv "$temp_file_no_postproc" "$temp_file"
            else
                echo "ERROR: remove_sections.awk produced empty file" >&2
                rm -f "$temp_file_no_postproc"
            fi
            
            # Now execute post-processing AWK scripts
            # filter_postproc_sections.awk outputs code on multiple lines
            # First line contains header + first line of code
            # Following lines contain continuation code
            # We need to combine all lines into single section_code
            section_type=""
            start_line=""
            end_line=""
            section_code=""
            line_num=0
            while IFS= read -r line || [ -n "$line" ]; do
                line_num=$((line_num + 1))
                if [ "$line_num" -eq 1 ]; then
                    # First line: parse header
                    IFS='|' read -r section_type start_line end_line section_code <<< "$line"
                else
                    # Continuation lines: append to code
                    section_code="${section_code}"$'\n'"${line}"
                fi
            done < "$postproc_sections_file"
            
            if [ -n "$section_code" ]; then
                # Create temporary AWK script file for post-processing
                local awk_script_file=$(mktemp)
                echo "$section_code" > "$awk_script_file"
                
                # Debug: save temp_file before post-processing
                if [ "$template_file" = "${TEMPLATES_DIR}/mock_macros_header.h.tpl" ]; then
                    cp "$temp_file" "/tmp/before_postproc.txt" 2>/dev/null || true
                fi
                
                # Execute AWK script on the current file (post-processing modifies existing content)
                # Ensure MAP_MACROS_CONTENT_FILE is exported before AWK execution
                # AWK reads ENVIRON array at startup, so variables must be exported before AWK starts
                export MAP_MACROS_CONTENT_FILE="${MAP_MACROS_CONTENT_FILE}"
                export RETURN_TYPE_MACROS_FILE="${RETURN_TYPE_MACROS_FILE}"
                export SIMPLE_WRAPPER_MACROS_FILE="${SIMPLE_WRAPPER_MACROS_FILE}"
                # Debug: check if files are set
                if [ "$template_file" = "${TEMPLATES_DIR}/mock_macros_header.h.tpl" ]; then
                    echo "DEBUG: MAP_MACROS_CONTENT_FILE = ${MAP_MACROS_CONTENT_FILE}" >&2
                    [ -f "${MAP_MACROS_CONTENT_FILE}" ] && echo "DEBUG: MAP_MACROS_CONTENT_FILE exists" >&2 || echo "DEBUG: MAP_MACROS_CONTENT_FILE does not exist" >&2
                    echo "DEBUG: RETURN_TYPE_MACROS_FILE = ${RETURN_TYPE_MACROS_FILE}" >&2
                    if [ -f "${RETURN_TYPE_MACROS_FILE}" ]; then
                        echo "DEBUG: RETURN_TYPE_MACROS_FILE exists, size=$(wc -c < "${RETURN_TYPE_MACROS_FILE}")" >&2
                        echo "DEBUG: First 5 lines:" >&2
                        head -5 "${RETURN_TYPE_MACROS_FILE}" >&2
                    else
                        echo "DEBUG: RETURN_TYPE_MACROS_FILE does not exist" >&2
                    fi
                    echo "DEBUG: SIMPLE_WRAPPER_MACROS_FILE = ${SIMPLE_WRAPPER_MACROS_FILE}" >&2
                    [ -f "${SIMPLE_WRAPPER_MACROS_FILE}" ] && echo "DEBUG: SIMPLE_WRAPPER_MACROS_FILE exists" >&2 || echo "DEBUG: SIMPLE_WRAPPER_MACROS_FILE does not exist" >&2
                fi
                awk -f "$awk_script_file" "$temp_file" > "${temp_file}.postproc"
                
                # Debug: save temp_file after post-processing
                if [ "$template_file" = "${TEMPLATES_DIR}/mock_macros_header.h.tpl" ]; then
                    cp "${temp_file}.postproc" "/tmp/after_postproc.txt" 2>/dev/null || true
                fi
                
                if [ -s "${temp_file}.postproc" ]; then
                    mv "${temp_file}.postproc" "$temp_file"
                else
                    echo "ERROR: Post-processing result is empty" >&2
                    # Don't fail - use original temp_file
                    rm -f "${temp_file}.postproc"
                fi
                
                rm -f "$awk_script_file"
            fi
        fi
        rm -f "$postproc_sections_file"
    
    # Debug: check final temp_file before writing to output
    if [ "$template_file" = "${TEMPLATES_DIR}/mock_macros_header.h.tpl" ]; then
        cp "$temp_file" "/tmp/final_temp_file.txt" 2>/dev/null || true
    fi
    
    # Final cleanup: remove any remaining postproc sections before writing to output
    local temp_file_final=$(mktemp)
    awk -f "${SCRIPTS_DIR}/remove_sections.awk" "$temp_file" > "$temp_file_final"
    if [ -s "$temp_file_final" ]; then
        mv "$temp_file_final" "$temp_file"
    else
        echo "ERROR: Final remove_sections.awk produced empty file" >&2
        rm -f "$temp_file_final"
    fi
    
    cat "$temp_file" > "$output_file"
    
    # Final cleanup: remove any remaining postproc sections from output file
    # This is a safety net in case postproc sections weren't removed earlier
    local output_file_clean=$(mktemp)
    awk -f "${SCRIPTS_DIR}/remove_sections.awk" "$output_file" > "$output_file_clean"
    if [ -s "$output_file_clean" ]; then
        mv "$output_file_clean" "$output_file"
    else
        echo "ERROR: Final remove_sections.awk produced empty output file" >&2
        rm -f "$output_file_clean"
    fi
    
    rm -f "$temp_file" "$sections_file"
}

